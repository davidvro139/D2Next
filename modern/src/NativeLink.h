#pragma once
// How the native code finds D2MOO's functions: by export number in the x64 DLLs it loads, or, in the in-process
// build (D2MODERN_INPROCESS), in D2MOO's static libraries linked into the program (NativeExports.cpp lists them).
#include <filesystem>
#include <stdexcept>
#include <string>
#ifndef D2MODERN_INPROCESS
#include <windows.h>
#endif
namespace d2::native {
enum class Library {Storm,Fog,D2Lang,D2CMP,D2Common,D2Game,Net};
#ifdef D2MODERN_INPROCESS
constexpr bool inProcess=true;
struct Module {Library library;};
void* linkedExport(Library library,int ordinal);        // nullptr: not in the table
void* linkedNamed(Library library,const char* name);
inline Module loadModule(const std::filesystem::path&,Library library) {return {library};}
inline void unloadModule(Module) {}
inline void* findExport(Module m,int ordinal) {return linkedExport(m.library,ordinal);}
inline void* findNamed(Module m,const char* name) {return linkedNamed(m.library,name);}
#else
constexpr bool inProcess=false;
struct Module {HMODULE handle=nullptr;};
inline Module loadModule(const std::filesystem::path& path,Library) {
    auto m=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!m)throw std::runtime_error("Cannot load "+path.string()+" (Windows error "+std::to_string(GetLastError())+")");
    return {m};
}
inline void unloadModule(Module m) {FreeLibrary(m.handle);}
inline void* findExport(Module m,int ordinal) {return reinterpret_cast<void*>(GetProcAddress(m.handle,MAKEINTRESOURCEA(ordinal)));}
inline void* findNamed(Module m,const char* name) {return reinterpret_cast<void*>(GetProcAddress(m.handle,name));}
#endif
template<class T> T ordinal(Module m,int number) {
    auto p=findExport(m,number);
    if(!p)throw std::runtime_error("Missing native ordinal "+std::to_string(number));
    return reinterpret_cast<T>(p);
}
template<class T> T named(Module m,const char* name) {
    auto p=findNamed(m,name);
    if(!p)throw std::runtime_error(std::string("Missing native function ")+name);
    return reinterpret_cast<T>(p);
}
// In-process only: the game archives and data tables, mounted and loaded once for the whole program and shared by
// the level loader and the game session (D2MOO keeps both in globals).
void useGameData(const std::filesystem::path& gameDirectory);
}
