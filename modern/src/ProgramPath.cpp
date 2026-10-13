#include "ProgramPath.h"
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif
namespace d2 {
namespace {
std::filesystem::path& chosen() {static std::filesystem::path directory;return directory;}
}
void setProgramDirectory(const std::filesystem::path& directory) {chosen()=directory;}
std::filesystem::path programDirectory() {
    if(!chosen().empty())return chosen();
#ifdef _WIN32
    std::wstring path(32768,L'\0');
    const DWORD length=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));
    if(!length||length==path.size())throw std::runtime_error("Cannot determine the game executable directory");
    path.resize(length);
    return std::filesystem::path(path).parent_path();
#elif defined(__EMSCRIPTEN__)
    // The HTML page mounts MPQ/ and Save/ at the wasm root before main() runs.
    return std::filesystem::path("/");
#else
    std::error_code error;
    const auto exe=std::filesystem::read_symlink("/proc/self/exe",error);
    return error?std::filesystem::current_path():exe.parent_path();
#endif
}
std::filesystem::path environmentPath(const char* name) {
#ifdef _WIN32
    const std::wstring wide(name,name+std::char_traits<char>::length(name));
    if(const wchar_t* value=_wgetenv(wide.c_str()))return std::filesystem::path(value);
#else
    if(const char* value=std::getenv(name))return std::filesystem::path(value);
#endif
    return {};
}
}
namespace d2 {
std::filesystem::path gameFile(const std::filesystem::path& directory,const std::filesystem::path& name) {
    auto exact=directory/name;
#ifndef _WIN32
    std::error_code error;
    if(std::filesystem::exists(exact,error))return exact;
    // Each part of the name in turn (names like "D2SE/CORES/1.10f/Patch_D2.mpq" have folders).
    auto lower=[](std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;};
    std::filesystem::path found=directory;
    for(const auto& part:name) {
        const auto want=lower(part.string());
        bool matched=false;
        for(const auto& entry:std::filesystem::directory_iterator(found,error)) {
            if(lower(entry.path().filename().string())==want){found=entry.path();matched=true;break;}
        }
        if(!matched)return exact;
    }
    return found;
#else
    return exact;
#endif
}
}
namespace d2 {
void setEnvironment(const char* name,const char* value) {
#ifdef _WIN32
    _putenv_s(name,value);
#else
    setenv(name,value,1);
#endif
}
}
