#pragma once
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <Security/Security.h>
#include <CoreFoundation/CoreFoundation.h>
namespace Bazzalt::Game {
inline bool KeychainProvisionAllowed=true;
inline std::string MacPlayerHash(const std::filesystem::path& path){
    auto text=Read(path,512*1024*1024);std::vector<unsigned char> data(text.begin(),text.end());Require(data.size()>=32,"Invalid Mach-O player");
    auto u32=[&](std::size_t at){Require(at+4<=data.size(),"Truncated Mach-O");std::uint32_t value;std::memcpy(&value,data.data()+at,4);return value;};Require(u32(0)==0xfeedfacf,"Invalid Mach-O player");
    const auto count=u32(16),size=u32(20);std::size_t at=32,end=32+std::size_t(size),cut=0;Require(end<=data.size()&&count<=65536,"Invalid load commands");
    for(std::uint32_t i=0;i<count;++i){Require(at+8<=end,"Truncated load command");const auto command=u32(at),length=u32(at+4);Require(length>=8&&at+length<=end,"Invalid load command");
        if(command==0x1d){Require(length==16&&!cut,"Invalid code signature command");cut=u32(at+8);Require(cut>=end&&cut+std::size_t(u32(at+12))<=data.size(),"Invalid signature range");std::fill_n(data.begin()+at+8,8,0);}
        if(command==0x19&&length>=72&&std::memcmp(data.data()+at+8,"__LINKEDIT",10)==0){std::fill_n(data.begin()+at+32,8,0);std::fill_n(data.begin()+at+48,8,0);}at+=length;
    }Require(at==end&&cut,"Missing Mach-O code signature");data.resize(cut);const auto digest=Hash(data);const char* digits="0123456789abcdef";std::string result;for(auto byte:digest){result+=digits[byte>>4];result+=digits[byte&15];}return result;
}
inline std::filesystem::path ExecutablePath(){std::uint32_t size=0;_NSGetExecutablePath(nullptr,&size);Require(size&&size<1024*1024,"Invalid executable path size");std::vector<char> data(size);Require(_NSGetExecutablePath(data.data(),&size)==0,"Cannot locate player");return std::filesystem::canonical(data.data());}
inline void CheckMacSignature(const std::filesystem::path& executable){
    const auto bundle=executable.parent_path().parent_path().parent_path();auto name=bundle.string();
    CFURLRef url=CFURLCreateFromFileSystemRepresentation(nullptr,reinterpret_cast<const UInt8*>(name.data()),name.size(),true);Require(url,"Invalid bundle path");SecStaticCodeRef code=nullptr;auto status=SecStaticCodeCreateWithPath(url,kSecCSDefaultFlags,&code);CFRelease(url);
    if(status==errSecSuccess){status=SecStaticCodeCheckValidity(code,kSecCSStrictValidate|kSecCSCheckNestedCode|kSecCSCheckAllArchitectures,nullptr);CFRelease(code);}Require(status==errSecSuccess,"Game application signature is invalid or content was modified");
}
inline std::vector<unsigned char> KeychainKey(const std::string& service,const std::string& account,const std::string& environment){
    Require(!service.empty()&&service.size()<=512&&account.size()==32,"Invalid Keychain identity");
    auto env=[&](){const auto* value=std::getenv(environment.c_str());Require(value&&std::strlen(value)==64,"Provision this game's key through its configured environment variable on first launch");return DecodeHex(value,32);};
    if(!KeychainProvisionAllowed)return env();
    auto serviceRef=CFStringCreateWithBytes(nullptr,reinterpret_cast<const UInt8*>(service.data()),service.size(),kCFStringEncodingUTF8,false);auto accountRef=CFStringCreateWithBytes(nullptr,reinterpret_cast<const UInt8*>(account.data()),account.size(),kCFStringEncodingUTF8,false);
    const void* keys[]={kSecClass,kSecAttrService,kSecAttrAccount,kSecAttrSynchronizable,kSecReturnData,kSecMatchLimit};const void* values[]={kSecClassGenericPassword,serviceRef,accountRef,kCFBooleanFalse,kCFBooleanTrue,kSecMatchLimitOne};
    auto query=CFDictionaryCreate(nullptr,keys,values,6,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);CFTypeRef found=nullptr;auto status=SecItemCopyMatching(query,&found);CFRelease(query);CFRelease(serviceRef);CFRelease(accountRef);
    if(status==errSecSuccess){Require(found&&CFGetTypeID(found)==CFDataGetTypeID(),"Invalid Keychain value");auto data=static_cast<CFDataRef>(found);const auto size=CFDataGetLength(data);std::vector<unsigned char> result(CFDataGetBytePtr(data),CFDataGetBytePtr(data)+size);CFRelease(found);Require(result.size()==32,"Invalid Keychain key size");return result;}
    Require(status==errSecItemNotFound,"Cannot access the game Keychain item");return env();
}
inline void StoreKeychainKey(const std::string& service,const std::string& account,const std::vector<unsigned char>& key){
    if(!KeychainProvisionAllowed)return;
    auto serviceRef=CFStringCreateWithCString(nullptr,service.c_str(),kCFStringEncodingUTF8);auto accountRef=CFStringCreateWithCString(nullptr,account.c_str(),kCFStringEncodingUTF8);auto data=CFDataCreate(nullptr,key.data(),key.size());
    const void* keys[]={kSecClass,kSecAttrService,kSecAttrAccount,kSecAttrSynchronizable,kSecValueData};const void* values[]={kSecClassGenericPassword,serviceRef,accountRef,kCFBooleanFalse,data};auto attributes=CFDictionaryCreate(nullptr,keys,values,5,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);const auto status=SecItemAdd(attributes,nullptr);CFRelease(attributes);CFRelease(serviceRef);CFRelease(accountRef);CFRelease(data);Require(status==errSecSuccess||status==errSecDuplicateItem,"Cannot store game key in the player's Keychain");
}
}
#endif
