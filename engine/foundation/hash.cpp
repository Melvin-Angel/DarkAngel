#include <darkangel/hash.hpp>
#include <Windows.h>
#include <bcrypt.h>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace darkangel {
std::string sha256(std::span<const std::byte> bytes) {
    if(bytes.size()>0xffffffffULL)throw std::runtime_error("SHA-256 input limit");
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};
    auto check=[](NTSTATUS status){if(status<0)throw std::runtime_error("Windows SHA-256 failed");};
    try {
        check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0));
        check(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0));
        check(BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())),static_cast<ULONG>(bytes.size()),0));
        unsigned char digest[32];check(BCryptFinishHash(hash,digest,32,0));
        BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
        constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(64);
        for(auto c:digest){result+=hex[c>>4];result+=hex[c&15];}return result;
    }catch(...){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);throw;}
}
std::string sha256(std::string_view bytes){return sha256(std::as_bytes(std::span(bytes.data(),bytes.size())));}
std::string file_sha256(const std::filesystem::path& path,std::size_t limit) {
    std::ifstream in(path,std::ios::binary|std::ios::ate);if(!in)throw std::runtime_error("Cannot open hash input");
    auto size=in.tellg();if(size<0 || static_cast<std::uint64_t>(size)>limit)throw std::runtime_error("Hash file exceeds limit");
    std::string bytes(static_cast<std::size_t>(size),'\0');in.seekg(0);in.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    if(!in)throw std::runtime_error("Hash input read failed");return sha256(bytes);
}
}
