#include "Bazzalt/SaveGame.h"
#include "Platform/AndroidPlatform.h"
#include "Persistence/SaveAccess.h"
#include "Bazzalt/Jobs.h"
#include "Bazzalt/Profiler.h"
#include <SDL3/SDL.h>
#include <sodium.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace Bazzalt::Detail {
struct SaveOperation {
    std::atomic<SaveOperationStatus> Status{SaveOperationStatus::Queued};
    std::atomic<float> Progress{0};
    mutable std::mutex Mutex;
    std::condition_variable Done;
    bool Cancelled=false;
    bool Write=false;
    SaveLoadResult Result;
    std::filesystem::path Directory;
    std::string Binding,Slot;
    SaveDocument Document;
    ByteBuffer Encoded;
    SaveOptions Options;
    ~SaveOperation(){if(!Options.EncryptionKey.empty())sodium_memzero(Options.EncryptionKey.data(),Options.EncryptionKey.size());}
};
}
namespace Bazzalt {
namespace {
constexpr std::size_t MaxPayload=16u*1024u*1024u,MaxFile=MaxPayload+4096;
constexpr std::uint64_t Magic=0x3130455641535a42ull;
struct StorageError:std::runtime_error {SaveError Code;StorageError(SaveError code,const std::string& text):std::runtime_error(text),Code(code){}};
struct Manager {
    std::thread::id Thread;
    UUID Project;
    bool Configured=false,Editor=false;
    std::filesystem::path Root,TestRoot;
    std::deque<std::shared_ptr<Detail::SaveOperation>> Queue;
    std::shared_ptr<Detail::SaveOperation> Active;
    JobHandle ActiveJob;
};
Manager& State(){static Manager state;return state;}
void Owner(){auto& state=State();if(!state.Configured)throw std::logic_error("SaveGame is available after application boot");if(state.Thread!=std::this_thread::get_id())throw std::logic_error("SaveGame submissions must run on the engine thread");}
void Name(const std::string& name){if(name.empty()||name.size()>64||!std::all_of(name.begin(),name.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-';}))throw StorageError(SaveError::InvalidArgument,"Save slot/profile names must be 1..64 ASCII letters, digits, underscores or hyphens");}
std::string Identifier(const std::string& text){Name(text);auto result=text;for(auto& c:result)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return result;}
void Crypto(){if(sodium_init()<0)throw StorageError(SaveError::IOError,"Cryptography initialization failed");}
void Options(const SaveOptions& options){if(options.Title.size()>256)throw StorageError(SaveError::InvalidArgument,"Save title exceeds 256 bytes");if(!options.EncryptionKey.empty()&&options.EncryptionKey.size()!=32)throw StorageError(SaveError::InvalidArgument,"Save encryption key must be exactly 32 bytes");}
std::filesystem::path Directory(const std::string& profile){
    Owner();Name(profile);auto& state=State();
    if(state.Root.empty()){
        if(!state.TestRoot.empty())state.Root=state.TestRoot;
        else {
            auto* text=SDL_GetPrefPath("BazzaltGames",state.Project.ToString().c_str());
            if(!text)throw StorageError(SaveError::IOError,"Could not resolve the OS save directory");
            state.Root=std::filesystem::u8path(text);SDL_free(text);
#ifdef __ANDROID__
            // SDL returns the application files directory and ignores org/app on Android.
            state.Root/=state.Project.ToString();
#endif
            state.Root/=state.Editor?"EditorPreview":"Player";state.Root/="Saves";
        }
        std::filesystem::create_directories(state.Root);state.Root=std::filesystem::canonical(state.Root);
    }
    auto directory=state.Root/("profile_"+Identifier(profile));std::filesystem::create_directories(directory);
    if(std::filesystem::is_symlink(std::filesystem::symlink_status(directory)))throw StorageError(SaveError::IOError,"Save profile cannot be a symbolic link");
    auto relative=std::filesystem::canonical(directory).lexically_relative(state.Root);if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")throw StorageError(SaveError::IOError,"Invalid save profile directory");
    return directory;
}
std::string Binding(const std::string& profile,const std::string& slot){return State().Project.ToString()+(State().Editor?":editor:":":player:")+Identifier(profile)+":"+Identifier(slot);}
std::filesystem::path Path(const std::filesystem::path& directory,const std::string& slot){Name(slot);return directory/("slot_"+Identifier(slot)+".bsave");}
void RejectLink(const std::filesystem::path& path){std::error_code error;auto status=std::filesystem::symlink_status(path,error);if(!error&&std::filesystem::is_symlink(status))throw StorageError(SaveError::IOError,"Save files cannot be symbolic links");}
struct Lock {
#if defined(_WIN32)
    HANDLE Handle=INVALID_HANDLE_VALUE;OVERLAPPED Region{};
    explicit Lock(const std::filesystem::path& path){RejectLink(path);Handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(Handle==INVALID_HANDLE_VALUE)throw StorageError(SaveError::IOError,"Could not open save lock");if(!LockFileEx(Handle,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,MAXDWORD,MAXDWORD,&Region)){CloseHandle(Handle);Handle=INVALID_HANDLE_VALUE;throw StorageError(SaveError::Busy,"Save slot is in use by another process");}}
    ~Lock(){if(Handle!=INVALID_HANDLE_VALUE){UnlockFileEx(Handle,0,MAXDWORD,MAXDWORD,&Region);CloseHandle(Handle);}}
#else
    int Handle=-1;
    explicit Lock(const std::filesystem::path& path){Handle=open(path.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW,0600);if(Handle<0)throw StorageError(SaveError::IOError,"Could not open save lock");if(flock(Handle,LOCK_EX|LOCK_NB)!=0){close(Handle);Handle=-1;throw StorageError(SaveError::Busy,"Save slot is in use by another process");}}
    ~Lock(){if(Handle>=0){flock(Handle,LOCK_UN);close(Handle);}}
#endif
};
ByteBuffer ReadFile(const std::filesystem::path& path){
#ifdef __ANDROID__
    const auto name=path.filename().string();
    if(name.ends_with(".bsave")||name.ends_with(".bsave.bak")){
        auto data=Runtime::AndroidPlatform::ReadSave(path);
        if(!data)throw StorageError(SaveError::NotFound,"Save slot does not exist");
        if(data->size()>MaxFile)throw StorageError(SaveError::Corrupt,"Save slot exceeds its size limit");
        return *data;
    }
#endif
    RejectLink(path);std::error_code error;auto size=std::filesystem::file_size(path,error);if(error){if(!std::filesystem::exists(path))throw StorageError(SaveError::NotFound,"Save slot does not exist");throw StorageError(SaveError::IOError,"Could not inspect save slot");}if(size>MaxFile)throw StorageError(SaveError::Corrupt,"Save file exceeds its size limit");std::ifstream file(path,std::ios::binary);if(!file)throw StorageError(SaveError::IOError,"Could not read save slot");ByteBuffer data(static_cast<std::size_t>(size));file.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(data.size()));if(!file||file.peek()!=std::char_traits<char>::eof())throw StorageError(SaveError::IOError,"Save slot changed while reading");return data;}
std::filesystem::path Temporary(const std::filesystem::path& target){Crypto();std::uint64_t random;randombytes_buf(&random,sizeof(random));auto path=target;path+=".tmp-"+std::to_string(random);return path;}
void WriteFile(const std::filesystem::path& path,const ByteBuffer& data){
#if defined(_WIN32)
    auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);if(handle==INVALID_HANDLE_VALUE)throw StorageError(SaveError::IOError,"Could not create save temporary file");
    bool good=true;std::size_t offset=0;while(offset<data.size()){DWORD written=0;auto count=static_cast<DWORD>(std::min<std::size_t>(1048576,data.size()-offset));if(!::WriteFile(handle,data.data()+offset,count,&written,nullptr)||written!=count){good=false;break;}offset+=written;}if(good)good=FlushFileBuffers(handle)!=0;CloseHandle(handle);
#else
    auto handle=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(handle<0)throw StorageError(SaveError::IOError,"Could not create save temporary file");
    bool good=true;std::size_t offset=0;while(offset<data.size()){auto written=write(handle,data.data()+offset,data.size()-offset);if(written<0&&errno==EINTR)continue;if(written<=0){good=false;break;}offset+=static_cast<std::size_t>(written);}if(good)good=fsync(handle)==0;if(close(handle)!=0)good=false;
#endif
    if(!good){std::error_code ignored;std::filesystem::remove(path,ignored);throw StorageError(SaveError::IOError,"Could not durably write save temporary file");}
}
void Replace(const std::filesystem::path& source,const std::filesystem::path& destination){
#if defined(__ANDROID__)
    Runtime::AndroidPlatform::WriteSave(destination,ReadFile(source));
    std::filesystem::remove(source);
#elif defined(_WIN32)
    if(!MoveFileExW(source.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw StorageError(SaveError::IOError,"Could not atomically replace save file");
#else
    if(rename(source.c_str(),destination.c_str())!=0)throw StorageError(SaveError::IOError,"Could not atomically replace save file");
#endif
}
void SyncDirectory(const std::filesystem::path& directory){
#if !defined(_WIN32)
    auto handle=open(directory.c_str(),O_RDONLY|O_DIRECTORY);if(handle<0)throw StorageError(SaveError::IOError,"Could not open save directory for synchronization");auto result=fsync(handle);close(handle);if(result!=0)throw StorageError(SaveError::IOError,"Could not synchronize save directory");
#else
    (void)directory;
#endif
}
struct TemporaryGuard {std::filesystem::path File;~TemporaryGuard(){if(!File.empty()){std::error_code ignored;std::filesystem::remove(File,ignored);}}};
struct Header {SaveSlotInfo Info;ByteBuffer Nonce;std::string Binding;};
Header ReadHeader(const ByteBuffer& data,const std::string& slot){BinaryReader reader(data,2048);Header header;header.Info.Slot=Identifier(slot);auto flags=reader.ReadUInt16();if(flags>1)throw StorageError(SaveError::UnsupportedVersion,"Unsupported save flags");header.Info.Encrypted=flags!=0;header.Info.SchemaVersion=reader.ReadUInt32();header.Info.Generation=reader.ReadUInt64();header.Info.SavedAtUnixMilliseconds=reader.ReadUInt64();header.Info.Title=reader.ReadString(256);header.Binding=reader.ReadString(256);header.Nonce=reader.ReadBytes(24);if(!reader.IsAtEnd()||!header.Info.SchemaVersion||!header.Info.Generation||header.Nonce.size()!=(header.Info.Encrypted?24u:0u))throw StorageError(SaveError::Corrupt,"Invalid save metadata");return header;}
SaveLoadResult DecodeFile(const ByteBuffer& file,const std::string& slot,const std::string& binding,const SaveOptions& options){
    try {
        if(file.size()<22)throw StorageError(SaveError::Corrupt,"Truncated save file");
        BinaryReader reader(file,MaxFile);if(reader.ReadUInt64()!=Magic)throw StorageError(SaveError::Corrupt,"Invalid save signature");if(reader.ReadUInt16()!=1)throw StorageError(SaveError::UnsupportedVersion,"Unsupported save file version");
        const auto aad=reader.ReadBytes(2048);auto payload=reader.ReadBytes(MaxPayload+16);const auto checksum=reader.ReadUInt32();if(!reader.IsAtEnd()||SDL_crc32(0,file.data(),file.size()-4)!=checksum)throw StorageError(SaveError::Corrupt,"Save checksum mismatch");
        auto header=ReadHeader(aad,slot);if(header.Binding!=binding)throw StorageError(SaveError::Corrupt,"Save belongs to a different project, profile or slot");
        if(header.Info.Encrypted){if(options.EncryptionKey.empty())throw StorageError(SaveError::KeyRequired,"An encryption key is required for this save");Crypto();if(payload.size()<16)throw StorageError(SaveError::Corrupt,"Truncated encrypted save");ByteBuffer plain(payload.size()-16);unsigned long long size=0;if(crypto_aead_xchacha20poly1305_ietf_decrypt(plain.data(),&size,nullptr,payload.data(),payload.size(),aad.data(),aad.size(),header.Nonce.data(),options.EncryptionKey.data())!=0)throw StorageError(SaveError::AuthenticationFailed,"Save authentication failed");plain.resize(static_cast<std::size_t>(size));payload=std::move(plain);}
        auto document=SaveGame::Decode(payload);if(document.SchemaVersion!=header.Info.SchemaVersion)throw StorageError(SaveError::Corrupt,"Save schema metadata mismatch");
        SaveLoadResult result;result.Success=true;result.Info=std::move(header.Info);result.Info.SizeBytes=file.size();result.Document=std::move(document);return result;
    }catch(const StorageError&){throw;}catch(const std::exception& error){throw StorageError(SaveError::Corrupt,error.what());}
}
SaveLoadResult LoadFile(const std::filesystem::path& path,const std::string& slot,const std::string& binding,const SaveOptions& options){return DecodeFile(ReadFile(path),slot,binding,options);}
SaveLoadResult LoadLocked(const std::filesystem::path& path,const std::string& slot,const std::string& binding,const SaveOptions& options){
    try{return LoadFile(path,slot,binding,options);}catch(const StorageError& error){
        if(!options.RecoverFromBackup||error.Code==SaveError::UnsupportedVersion||error.Code==SaveError::KeyRequired||error.Code==SaveError::AuthenticationFailed)throw;
        auto backup=path;backup+=".bak";try{auto result=LoadFile(backup,slot,binding,options);result.RecoveredFromBackup=true;return result;}catch(...){throw error;}
    }
}
SaveLoadResult Failure(SaveError code,const std::string& error){SaveLoadResult result;result.Code=code;result.Error=error;return result;}
std::set<std::string> Names(const std::filesystem::path& directory){std::vector<std::string> files;
#ifdef __ANDROID__
    files=Runtime::AndroidPlatform::SaveFiles(directory);
#else
    for(const auto& entry:std::filesystem::directory_iterator(directory)){
        auto text=entry.path().filename().u8string();
        files.emplace_back(reinterpret_cast<const char*>(text.data()),text.size());
    }
#endif
    std::set<std::string> names;unsigned visited=0;for(const auto& name:files){if(++visited>4096)throw StorageError(SaveError::QuotaExceeded,"Too many files in save profile");if(!name.starts_with("slot_"))continue;std::size_t suffix=0;if(name.ends_with(".bsave"))suffix=6;else if(name.ends_with(".bsave.bak"))suffix=10;else continue;auto slot=name.substr(5,name.size()-5-suffix);try{Name(slot);names.insert(slot);}catch(const StorageError&){}if(names.size()>256)throw StorageError(SaveError::QuotaExceeded,"Save profile exceeds 256 slots");}return names;}
ByteBuffer EncodeFile(const SaveDocument& document,const SaveOptions& options,const std::string& binding,std::uint64_t generation,SaveSlotInfo& info,std::optional<std::uint64_t> timestamp={}){
    auto payload=SaveGame::Encode(document);BinaryWriter header(2048);const bool encrypted=!options.EncryptionKey.empty();
    info.Title=options.Title;info.SchemaVersion=document.SchemaVersion;info.Generation=generation;info.SavedAtUnixMilliseconds=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());if(timestamp)info.SavedAtUnixMilliseconds=*timestamp;info.Encrypted=encrypted;
    header.WriteUInt16(encrypted?1:0);header.WriteUInt32(info.SchemaVersion);header.WriteUInt64(generation);header.WriteUInt64(info.SavedAtUnixMilliseconds);header.WriteString(info.Title);header.WriteString(binding);ByteBuffer nonce;
    if(encrypted){Crypto();nonce.resize(24);randombytes_buf(nonce.data(),nonce.size());}header.WriteBytes(nonce);
    if(encrypted){ByteBuffer cipher(payload.size()+16);unsigned long long size=0;if(crypto_aead_xchacha20poly1305_ietf_encrypt(cipher.data(),&size,payload.data(),payload.size(),header.GetData().data(),header.GetSize(),nullptr,nonce.data(),options.EncryptionKey.data())!=0)throw StorageError(SaveError::IOError,"Save encryption failed");cipher.resize(static_cast<std::size_t>(size));sodium_memzero(payload.data(),payload.size());payload=std::move(cipher);}
    BinaryWriter writer(MaxFile);writer.WriteUInt64(Magic);writer.WriteUInt16(1);writer.WriteBytes(header.GetData());writer.WriteBytes(payload);writer.WriteUInt32(SDL_crc32(0,writer.GetData().data(),writer.GetSize()));auto bytes=writer.TakeData();info.SizeBytes=bytes.size();return bytes;
}
void CommitGate(const std::shared_ptr<Detail::SaveOperation>& operation){if(!operation)return;std::lock_guard lock(operation->Mutex);if(operation->Cancelled)throw StorageError(SaveError::Cancelled,"Save operation cancelled");operation->Status=SaveOperationStatus::Committing;operation->Progress=.75f;}
SaveLoadResult WriteLocked(const std::filesystem::path& path,const std::string& slot,const std::string& binding,const SaveDocument& document,const SaveOptions& options,const std::shared_ptr<Detail::SaveOperation>& operation={}){
    SaveLoadResult result;result.Info.Slot=Identifier(slot);std::optional<SaveLoadResult> previousSave;std::uint64_t generation=0;
    try{auto previous=LoadFile(path,slot,binding,options);generation=previous.Info.Generation;previousSave=std::move(previous);}catch(const StorageError& error){
        if(error.Code==SaveError::UnsupportedVersion||error.Code==SaveError::KeyRequired||error.Code==SaveError::AuthenticationFailed)throw;
        auto backup=path;backup+=".bak";try{auto previous=LoadFile(backup,slot,binding,options);generation=previous.Info.Generation;previousSave=std::move(previous);}catch(const StorageError& fallback){if(fallback.Code==SaveError::UnsupportedVersion||fallback.Code==SaveError::KeyRequired||fallback.Code==SaveError::AuthenticationFailed)throw;if(error.Code!=SaveError::NotFound&&!options.AllowOverwriteCorrupt)throw error;}
    }
    if(options.ExpectedGeneration&&*options.ExpectedGeneration!=generation)throw StorageError(SaveError::Conflict,"Save generation changed; reload before overwriting");
    if(generation==UINT64_MAX)throw StorageError(SaveError::UnsupportedVersion,"Save generation overflow");
    if(generation==0&&!Names(path.parent_path()).contains(slot)&&Names(path.parent_path()).size()>=256)throw StorageError(SaveError::QuotaExceeded,"Save profile exceeds 256 slots");
    auto bytes=EncodeFile(document,options,binding,generation+1,result.Info);TemporaryGuard temporary{Temporary(path)};WriteFile(temporary.File,bytes);
    if(operation)operation->Progress=.6f;CommitGate(operation);
    if(previousSave){auto backup=path;backup+=".bak";auto backupOptions=options;backupOptions.Title=previousSave->Info.Title;SaveSlotInfo backupInfo;auto backupData=EncodeFile(*previousSave->Document,backupOptions,binding,generation,backupInfo,previousSave->Info.SavedAtUnixMilliseconds);TemporaryGuard staged{Temporary(backup)};WriteFile(staged.File,backupData);Replace(staged.File,backup);staged.File.clear();SyncDirectory(path.parent_path());}
    Replace(temporary.File,path);temporary.File.clear();result.Committed=true;
    try{SyncDirectory(path.parent_path());result.Success=true;}catch(const StorageError& error){result.Code=error.Code;result.Error=error.what();}
    return result;
}
SaveLoadResult Work(const std::filesystem::path& directory,const std::string& slot,const std::string& binding,const SaveOptions& options,const SaveDocument* document,const std::shared_ptr<Detail::SaveOperation>& operation={}){
    try {Options(options);auto path=Path(directory,slot);auto lockPath=path;lockPath+=".lock";Lock lock(lockPath);if(operation){std::lock_guard guard(operation->Mutex);if(operation->Cancelled)return Failure(SaveError::Cancelled,"Save operation cancelled");operation->Progress=.2f;}
        return document?WriteLocked(path,slot,binding,*document,options,operation):LoadLocked(path,slot,binding,options);
    }catch(const StorageError& error){return Failure(error.Code,error.what());}catch(const std::invalid_argument& error){return Failure(SaveError::InvalidArgument,error.what());}catch(const std::length_error& error){return Failure(SaveError::QuotaExceeded,error.what());}catch(const std::exception& error){return Failure(SaveError::IOError,error.what());}catch(...){return Failure(SaveError::IOError,"Save operation failed");}
}
void StartNext(){auto& state=State();if(state.Active||state.Queue.empty())return;state.Active=state.Queue.front();state.Queue.pop_front();auto operation=state.Active;
    try {state.ActiveJob=Jobs::Schedule([operation]{
        struct Cleanup {Detail::SaveOperation& Value;~Cleanup(){Value.Document.Clear();ByteBuffer{}.swap(Value.Encoded);if(!Value.Options.EncryptionKey.empty()){sodium_memzero(Value.Options.EncryptionKey.data(),Value.Options.EncryptionKey.size());ByteBuffer{}.swap(Value.Options.EncryptionKey);}}} cleanup{*operation};
        try {
        ProfileScope profile(operation->Write?"SaveGame.Write":"SaveGame.Read");
        {std::lock_guard lock(operation->Mutex);if(operation->Cancelled){operation->Result=Failure(SaveError::Cancelled,"Save operation cancelled");operation->Status=SaveOperationStatus::Cancelled;operation->Done.notify_all();return;}operation->Status=SaveOperationStatus::Working;}
        if(operation->Write){operation->Document=SaveGame::Decode(operation->Encoded);ByteBuffer{}.swap(operation->Encoded);}
        auto result=Work(operation->Directory,operation->Slot,operation->Binding,operation->Options,operation->Write?&operation->Document:nullptr,operation);
        operation->Document.Clear();if(!operation->Options.EncryptionKey.empty()){sodium_memzero(operation->Options.EncryptionKey.data(),operation->Options.EncryptionKey.size());operation->Options.EncryptionKey.clear();}
        {std::lock_guard lock(operation->Mutex);if(operation->Cancelled&&!result.Committed)result=Failure(SaveError::Cancelled,"Save operation cancelled");operation->Result=std::move(result);operation->Progress=1;operation->Status=operation->Result.Success?SaveOperationStatus::Complete:operation->Result.Code==SaveError::Cancelled?SaveOperationStatus::Cancelled:SaveOperationStatus::Failed;}
        operation->Done.notify_all();
        } catch(...) {
            // Completion must be published even if allocation/profiling fails.
            {std::lock_guard lock(operation->Mutex);operation->Result.Success=false;operation->Result.Code=SaveError::IOError;operation->Status=SaveOperationStatus::Failed;}
            operation->Done.notify_all();
        }
    });}catch(const std::exception& error){std::lock_guard lock(operation->Mutex);operation->Result=Failure(SaveError::IOError,error.what());operation->Status=SaveOperationStatus::Failed;operation->Done.notify_all();}
}
std::shared_ptr<Detail::SaveOperation> Queue(const std::string& slot,const SaveOptions& options,const std::string& profile){Owner();Name(slot);Options(options);auto& state=State();if(state.Queue.size()>=8)throw std::length_error("Save operation queue is full");auto operation=std::make_shared<Detail::SaveOperation>();operation->Directory=Directory(profile);operation->Slot=Identifier(slot);operation->Binding=Binding(profile,slot);operation->Options=options;return operation;}
}
std::filesystem::path SaveGame::GetDirectory(const std::string& profile){return Directory(profile);}
ByteBuffer SaveGame::CreateEncryptionKey(){Crypto();ByteBuffer key(32);randombytes_buf(key.data(),key.size());return key;}
SaveResult SaveGame::Save(const std::string& slot,const SaveDocument& document,const SaveOptions& options,const std::string& profile){Owner();Flush();try{return Work(Directory(profile),slot,Binding(profile,slot),options,&document);}catch(const StorageError& error){return Failure(error.Code,error.what());}catch(const std::exception& error){return Failure(SaveError::IOError,error.what());}}
SaveLoadResult SaveGame::Load(const std::string& slot,const SaveOptions& options,const std::string& profile){Owner();Flush();try{return Work(Directory(profile),slot,Binding(profile,slot),options,nullptr);}catch(const StorageError& error){return Failure(error.Code,error.what());}catch(const std::exception& error){return Failure(SaveError::IOError,error.what());}}
SaveHandle SaveGame::SaveAsync(const std::string& slot,SaveDocument document,SaveOptions options,const std::string& profile){auto operation=Queue(slot,options,profile);operation->Write=true;operation->Encoded=Encode(document);State().Queue.push_back(operation);SaveHandle result;result.m_state=std::move(operation);return result;}
SaveHandle SaveGame::LoadAsync(const std::string& slot,SaveOptions options,const std::string& profile){auto operation=Queue(slot,options,profile);State().Queue.push_back(operation);SaveHandle result;result.m_state=std::move(operation);return result;}
void SaveGame::Flush(){Owner();auto& state=State();while(state.Active||!state.Queue.empty()){StartNext();auto operation=state.Active;{std::unique_lock lock(operation->Mutex);operation->Done.wait(lock,[&]{auto status=operation->Status.load();return status==SaveOperationStatus::Complete||status==SaveOperationStatus::Cancelled||status==SaveOperationStatus::Failed;});}state.ActiveJob.Wait();state.ActiveJob={};state.Active.reset();}}
SaveResult SaveGame::Delete(const std::string& slot,const std::string& profile){Owner();Flush();try{auto path=Path(Directory(profile),slot);auto lockPath=path;lockPath+=".lock";Lock lock(lockPath);bool removed=false;for(auto suffix:{"",".bak"}){auto file=path;file+=suffix;RejectLink(file);
#ifdef __ANDROID__
removed=Runtime::AndroidPlatform::RemoveSave(file)||removed;
#else
removed=std::filesystem::remove(file)||removed;
#endif
}SyncDirectory(path.parent_path());SaveResult result;result.Success=true;result.Committed=removed;result.Info.Slot=Identifier(slot);return result;}catch(const StorageError& error){return Failure(error.Code,error.what());}catch(const std::exception& error){return Failure(SaveError::IOError,error.what());}}
std::vector<SaveSlotInfo> SaveGame::ListSlots(const std::string& profile){
    Owner();Flush();auto directory=Directory(profile);std::vector<SaveSlotInfo> result;
    for(const auto& slot:Names(directory)){
        SaveSlotInfo info;info.Slot=slot;
        try{
            ByteBuffer data;
#ifdef __ANDROID__
            try{data=ReadFile(Path(directory,slot));}
            catch(const StorageError& error){if(error.Code!=SaveError::NotFound)throw;auto backup=Path(directory,slot);backup+=".bak";data=ReadFile(backup);}
            const auto fileSize=data.size();
#else
            auto path=Path(directory,slot);if(!std::filesystem::exists(path))path+=".bak";
            RejectLink(path);const auto fileSize=std::filesystem::file_size(path);
            std::ifstream file(path,std::ios::binary);data.resize(14);file.read(reinterpret_cast<char*>(data.data()),14);
            if(!file)throw std::runtime_error("Truncated save header");
            BinaryReader prefix(data);prefix.ReadUInt64();prefix.ReadUInt16();auto metadataSize=prefix.ReadUInt32();
            if(metadataSize>2048||fileSize<14+metadataSize)throw std::runtime_error("Invalid save metadata length");
            data.resize(14+metadataSize);file.read(reinterpret_cast<char*>(data.data()+14),metadataSize);
            if(!file)throw std::runtime_error("Truncated save metadata");
#endif
            if(data.size()<14)throw std::runtime_error("Truncated save header");
            BinaryReader reader(data);
            if(reader.ReadUInt64()!=Magic||reader.ReadUInt16()!=1)throw std::runtime_error("Unsupported save header");
            auto size=reader.ReadUInt32();
            if(size>2048||size>data.size()-14)throw std::runtime_error("Invalid save metadata length");
            ByteBuffer header(data.begin()+14,data.begin()+14+size);
            info=ReadHeader(header,slot).Info;info.SizeBytes=fileSize;
        }catch(const std::exception& error){info.Error=error.what();}
        result.push_back(std::move(info));
    }
    return result;
}

SaveOperationStatus SaveHandle::GetStatus()const{return m_state?m_state->Status.load():SaveOperationStatus::Failed;}
bool SaveHandle::IsDone()const{auto status=GetStatus();return status==SaveOperationStatus::Complete||status==SaveOperationStatus::Failed||status==SaveOperationStatus::Cancelled;}
float SaveHandle::GetProgress()const{return m_state?m_state->Progress.load():0;}
bool SaveHandle::Cancel()const{if(!m_state)return false;std::lock_guard lock(m_state->Mutex);if(IsDone()||m_state->Status==SaveOperationStatus::Committing)return false;m_state->Cancelled=true;return true;}
SaveLoadResult SaveHandle::GetResult()const{if(!m_state)return Failure(SaveError::NotReady,"Invalid save operation");std::lock_guard lock(m_state->Mutex);if(!IsDone())return Failure(SaveError::NotReady,"Save operation is not complete");return m_state->Result;}
void Runtime::SaveAccess::Configure(UUID project,bool editor){auto& state=State();if(state.Configured)SaveGame::Flush();if(!project)throw std::invalid_argument("SaveGame requires a project UUID");state.Thread=std::this_thread::get_id();state.Project=project;state.Editor=editor;state.Configured=true;state.Root.clear();state.TestRoot.clear();}
void Runtime::SaveAccess::Pump(){auto& state=State();if(!state.Configured)return;Owner();if(state.Active){auto status=state.Active->Status.load();if(status==SaveOperationStatus::Complete||status==SaveOperationStatus::Cancelled||status==SaveOperationStatus::Failed){state.ActiveJob.Wait();state.ActiveJob={};state.Active.reset();}}StartNext();}
void Runtime::SaveAccess::Reset(){auto& state=State();if(!state.Configured)return;SaveGame::Flush();state.Configured=false;state.Root.clear();state.TestRoot.clear();}
void Runtime::SaveAccess::SetTestDirectory(const std::filesystem::path& path){Owner();SaveGame::Flush();if(path.empty()||!path.is_absolute())throw std::invalid_argument("Test save directory must be absolute");State().Root.clear();State().TestRoot=path;}
}
