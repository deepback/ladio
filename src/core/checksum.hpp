#pragma once
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace ladio {
/* Accidental-corruption checksum for derived PCM; source/model identity is SHA256.
   This is deliberately not represented as a cryptographic signature. */
inline std::string checksumFile(const std::filesystem::path&path){
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Checksum input unavailable");
    std::array<char,65536>b{};uint64_t h=14695981039346656037ULL;
    while(f){f.read(b.data(),b.size());for(std::streamsize i=0;i<f.gcount();i++){h^=static_cast<uint8_t>(b[size_t(i)]);h*=1099511628211ULL;}}
    if(!f.eof())throw std::runtime_error("Checksum read failed");std::ostringstream text;text<<std::hex<<std::setw(16)<<std::setfill('0')<<h;return text.str();
}
}
