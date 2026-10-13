// Standalone, disposable native session host: the game session (NativeSession.cpp) behind stdin/stdout pipes, for
// the default build, which loads the x64 DLLs in this separate process. The in-process build does not use it.
#include "NativeSession.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
void* __stdcall SMemAlloc(size_t size, const char*, int, unsigned int flags)
{
    (void)flags;
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}
int __stdcall SMemFree(void* p, const char*, int, unsigned int)
{
    return p ? HeapFree(GetProcessHeap(), 0, p) : 1;
}
void* __stdcall SMemReAlloc(void* p, int size, const char*, int, unsigned int)
{
    return p ? HeapReAlloc(GetProcessHeap(), 0, p, size) : HeapAlloc(GetProcessHeap(), 0, size);
}
void __cdecl SErrDisplayAppFatal(char* format, ...)
{
    std::fprintf(stderr, "fatal: %s\n", format);
    std::exit(9);
}
int __cdecl SErrDisplayErrorFmt(unsigned int, const char*, int, int, unsigned int, char* format, ...)
{
    std::fprintf(stderr, "error: %s\n", format);
    return 0;
}
int __stdcall SErrPrepareAppFatal(const char*, int)
{
    return 0;
}

namespace {
void transfer(HANDLE pipe,void* data,DWORD size,bool writing) {
    DWORD done=0;
    if(!(writing?WriteFile(pipe,data,size,&done,nullptr):ReadFile(pipe,data,size,&done,nullptr))||done!=size)
        throw std::runtime_error("Native session pipe closed");
}
int run(int argc,wchar_t** argv) {
    if(argc!=5&&argc!=6)throw std::runtime_error("Usage: NativeGameHost DLL_DIR GAME_DIR SAVE SEED [SAVE_OUT]");
    d2::native::Session::Settings settings;
    settings.dlls=argv[1];settings.game=argv[2];settings.save=argv[3];settings.seed=uint32_t(std::stoul(argv[4]));
    if(argc==6)settings.saveOut=argv[5];
    static d2::native::Reply reply;
    auto session=d2::native::Session::start(settings,reply);
    transfer(GetStdHandle(STD_OUTPUT_HANDLE),&reply,sizeof(reply),true);
    for(;;) {
        d2::native::Request request;transfer(GetStdHandle(STD_INPUT_HANDLE),&request,sizeof(request),false);
        if(!session->handle(request,reply))break;
        transfer(GetStdHandle(STD_OUTPUT_HANDLE),&reply,sizeof(reply),true);
    }
    session.release(); // Disposable process owns all native globals; nothing is torn down across the DLL allocator boundary.
    return 0;
}
}
int wmain(int argc,wchar_t** argv) {
    int result=1;try{result=run(argc,argv);}catch(const std::exception& e){std::fprintf(stderr,"NativeGameHost: %s\n",e.what());}
    std::fflush(stderr);
    ExitProcess(UINT(result));
}
