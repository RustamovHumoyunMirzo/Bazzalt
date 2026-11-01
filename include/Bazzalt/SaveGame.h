#pragma once
#include "Bazzalt/BinaryData.h"
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <variant>
namespace Bazzalt {
struct SaveValue;
using SaveArray=std::vector<SaveValue>;
using SaveObject=std::map<std::string,SaveValue>;
enum class SaveValueType { Null,Boolean,Integer,Number,String,Vec2,Vec3,Vec4,Quaternion,UUID,Bytes,Array,Object };
struct SaveValue {
    using Storage=std::variant<std::monostate,bool,std::int64_t,double,std::string,Vec2,Vec3,Vec4,Quaternion,UUID,ByteBuffer,SaveArray,SaveObject>;
    Storage Data;
    SaveValue()=default;
    SaveValue(bool value):Data(value){}
    SaveValue(int value):Data(std::int64_t(value)){}
    SaveValue(std::int64_t value):Data(value){}
    SaveValue(float value):Data(double(value)){}
    SaveValue(double value):Data(value){}
    SaveValue(const char* value):Data(std::string(value)){}
    SaveValue(std::string value):Data(std::move(value)){}
    SaveValue(Vec2 value):Data(value){}
    SaveValue(Vec3 value):Data(value){}
    SaveValue(Vec4 value):Data(value){}
    SaveValue(Quaternion value):Data(value){}
    SaveValue(UUID value):Data(value){}
    SaveValue(ByteBuffer value):Data(std::move(value)){}
    SaveValue(SaveArray value):Data(std::move(value)){}
    SaveValue(SaveObject value):Data(std::move(value)){}
    SaveValueType GetType() const{return static_cast<SaveValueType>(Data.index());}
};
class BAZZALT_API SaveDocument final {
public:
    std::uint32_t SchemaVersion=1;
    void Set(const std::string& key,SaveValue value);
    std::optional<SaveValue> Get(const std::string& key) const;
    bool Has(const std::string& key) const;
    bool Remove(const std::string& key);
    void Clear();
    const SaveObject& GetFields() const {return m_fields;}
    /// Transactional, caller-thread migration. No downgrade or hidden script execution.
    bool Migrate(std::uint32_t target,const std::function<bool(SaveDocument&,std::uint32_t,std::uint32_t)>& migration);
private:
    SaveObject m_fields;
};
enum class SaveError { None,NotFound,InvalidArgument,Corrupt,UnsupportedVersion,KeyRequired,AuthenticationFailed,Conflict,Busy,IOError,Cancelled,NotReady,QuotaExceeded };
struct SaveOptions {
    std::string Title;
    ByteBuffer EncryptionKey; // Empty: checksummed plaintext. Otherwise exactly 32 bytes.
    std::optional<std::uint64_t> ExpectedGeneration;
    bool RecoverFromBackup=true;
    bool AllowOverwriteCorrupt=false;
};
struct SaveSlotInfo {
    std::string Slot,Title;
    std::uint32_t SchemaVersion=0;
    std::uint64_t Generation=0,SavedAtUnixMilliseconds=0,SizeBytes=0;
    bool Encrypted=false;
    std::string Error;
};
struct SaveResult {
    bool Success=false,RecoveredFromBackup=false,Committed=false;
    SaveError Code=SaveError::None;
    std::string Error;
    SaveSlotInfo Info;
};
struct SaveLoadResult:SaveResult {std::optional<SaveDocument> Document;};
enum class SaveOperationStatus { Queued,Working,Committing,Complete,Failed,Cancelled };
namespace Runtime {struct SaveAccess;}
namespace Detail {struct SaveOperation;}
class BAZZALT_API SaveHandle final {
public:
    SaveHandle()=default;
    SaveOperationStatus GetStatus() const;
    bool IsDone() const;
    float GetProgress() const;
    bool Cancel() const;
    SaveLoadResult GetResult() const;
private:
    friend class SaveGame;
    std::shared_ptr<Detail::SaveOperation> m_state;
};
/// Standalone player progress, separate from project and authoring scene serialization.
class BAZZALT_API SaveGame final {
public:
    SaveGame()=delete;
    static std::filesystem::path GetDirectory(const std::string& profile="default");
    static SaveResult Save(const std::string& slot,const SaveDocument& document,const SaveOptions& options={},const std::string& profile="default");
    static SaveLoadResult Load(const std::string& slot,const SaveOptions& options={},const std::string& profile="default");
    static SaveResult Delete(const std::string& slot,const std::string& profile="default");
    static std::vector<SaveSlotInfo> ListSlots(const std::string& profile="default");
    static SaveHandle SaveAsync(const std::string& slot,SaveDocument document,SaveOptions options={},const std::string& profile="default");
    static SaveHandle LoadAsync(const std::string& slot,SaveOptions options={},const std::string& profile="default");
    /// Drains this application's FIFO save queue; unrelated engine jobs are not joined.
    static void Flush();
    static ByteBuffer Encode(const SaveDocument& document);
    static SaveDocument Decode(const ByteBuffer& data);
    static ByteBuffer CreateEncryptionKey();
};
}
