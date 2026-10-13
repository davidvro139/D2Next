// What Platform.h and PlatformText.h ask of each system, for the systems other than Windows (Android, Linux, the web).
#include "Platform.h"
#include "PlatformText.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <csignal>
#include <cstring>
#ifdef D2MODERN_SDL
#include <SDL3/SDL.h>
#endif
#ifndef __EMSCRIPTEN__
#include <dlfcn.h>
#include <signal.h>
#include <unwind.h>
#endif
namespace d2 {
// Before the game's fonts are loaded there is no system font to draw with here: the text takes its room (the size
// Segoe UI gives it on Windows, roughly) and stays invisible. The game's own fonts draw everything after loading.
Image systemTextImage(const std::string& text,std::array<uint8_t,3>) {
    Image image;
    image.width=uint32_t(std::max<size_t>(1,text.size()*7));image.height=16;
    image.rgba.assign(size_t(image.width)*image.height*4,0);
    return image;
}
void endProgram(int code) {
    std::cout.flush();std::cerr.flush();std::fflush(nullptr);
    std::_Exit(code);
}
#ifndef __EMSCRIPTEN__
namespace {
// A crash prints the signal and the call stack (library, function, offset) to stderr, which on Android is D2Next.log;
// `llvm-addr2line -e out/android-arm64/libmain.so OFFSET` gives the source line. Then the system's own handler runs.
_Unwind_Reason_Code stackFrame(_Unwind_Context* context,void* depth) {
    const uintptr_t pc=_Unwind_GetIP(context);
    if(!pc)return _URC_END_OF_STACK;
    Dl_info info{};
    if(dladdr(reinterpret_cast<void*>(pc),&info)&&info.dli_fname) {
        const char* file=std::strrchr(info.dli_fname,'/');
        std::fprintf(stderr,"  %2d %s+0x%zx %s\n",*static_cast<int*>(depth),file?file+1:info.dli_fname,
            size_t(pc-reinterpret_cast<uintptr_t>(info.dli_fbase)),info.dli_sname?info.dli_sname:"?");
    } else std::fprintf(stderr,"  %2d %p\n",*static_cast<int*>(depth),reinterpret_cast<void*>(pc));
    return ++*static_cast<int*>(depth)<48?_URC_NO_REASON:_URC_END_OF_STACK;
}
struct sigaction previous[32];
void crashed(int signal,siginfo_t* info,void* context) {
    std::fprintf(stderr,"Crash: signal %d at address %p\n",signal,info?info->si_addr:nullptr);
    int depth=0;_Unwind_Backtrace(stackFrame,&depth);
    std::fflush(stderr);
    sigaction(signal,&previous[signal],nullptr); // the system's report (Android's tombstone) follows
    (void)context;raise(signal);
}
}
#endif
#ifndef __ANDROID__ // Android shows it (PlatformAndroid.cpp)
void reportFatal(const std::string& message) {
#ifdef D2MODERN_SDL
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"D2Next stopped",message.c_str(),nullptr);
#else
    (void)message;
#endif
}
void forgetSharedGameFolder() {}
#endif
void installCrashReport() {
#ifdef __EMSCRIPTEN__
    // The browser's own debugger reports the fault; libunwind is not available in the wasm build.
#else
    struct sigaction action{};action.sa_sigaction=crashed;action.sa_flags=SA_SIGINFO|SA_ONSTACK;sigemptyset(&action.sa_mask);
    for(int signal:{SIGSEGV,SIGBUS,SIGILL,SIGFPE,SIGABRT})sigaction(signal,&action,&previous[signal]);
#endif
}
}
