#include "Integrity.h"
#include <iostream>
#include <objbase.h>

int main(){
    using namespace Bazzalt::Game;
    std::filesystem::path folder;
    try{
        std::vector<unsigned char> key(32),salt(32);for(int i=0;i<32;++i){key[i]=static_cast<unsigned char>(i);salt[i]=static_cast<unsigned char>(i+32);}
        // Vector generated independently with cryptography HKDF-SHA256.
        Require(Derive(key,salt,"inventory")==DecodeHex("56ee16882927a541a0d85865a402b1bb4362667c02329331878621280f4268d2",32),"HKDF cross-language vector mismatch");
        GUID id{};Require(SUCCEEDED(CoCreateGuid(&id)),"Test UUID unavailable");wchar_t guid[40]{};StringFromGUID2(id,guid,40);
        folder=std::filesystem::temp_directory_path()/(std::wstring(L"BazzaltSecurityTest-")+guid);Require(std::filesystem::create_directory(folder),"Test directory unavailable");
        const auto path=folder/"verified.bin";{std::ofstream file(path,std::ios::binary);file<<"abc";}
        Require(DigestHex(HashFile(path))=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 known vector");
        {
            FileLeases leases;LockTree(folder,leases);leases.push_back(std::make_unique<FileLease>(path));
            auto writer=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
            Require(writer==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_SHARING_VIOLATION,"Verified file remained writable");
            Require(!DeleteFileW(path.c_str()),"Verified file remained deletable");
            Require(!MoveFileW(folder.c_str(),(folder.wstring()+L"-moved").c_str()),"Verified directory remained replaceable");
        }
        Require(DeleteFileW(path.c_str())!=0,"Verified file lock leaked after release");std::filesystem::remove(folder);return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(!folder.empty()){std::error_code ignored;std::filesystem::remove_all(folder,ignored);}return 1;}
}
