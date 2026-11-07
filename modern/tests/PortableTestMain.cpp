// UTF-8 entry point for existing wide-argument integration tests on Android/Linux.
#include "ProgramPath.h"
#include "AssetTiming.h"
#include "NativeTown.h"
#include <filesystem>
#include <string>
#include <vector>
#include <iostream>
#ifdef __ANDROID__
#include <android/api-level.h>
#include <malloc.h>
#endif
// Headless tests have no client scene; the optional local driver is disabled below.
int wmain(int,wchar_t**);
int main(int argc,char** argv) {
#ifdef __ANDROID__
    // Same requirement as the app manifest's allowNativeHeapPointerTagging=false:
    // squall's legacy intrusive lists encode sentinels in pointer bits.
    if(android_get_device_api_level()>=30&&!mallopt(M_BIONIC_SET_HEAP_TAGGING_LEVEL,M_HEAP_TAGGING_LEVEL_NONE)) {
        std::cerr<<"Cannot configure untagged heap for legacy Storm lists\n";return 2;
    }
#endif
    const auto directory=std::filesystem::absolute(argv[0]).parent_path();
    d2::setProgramDirectory(directory);
    std::filesystem::create_directories(directory/"Logs");
    std::filesystem::create_directories(directory/"Save");
    d2::setEnvironment("D2NEXT_SAVE_DIR",(directory/"Save").string().c_str());
    std::vector<std::wstring> wide;wide.reserve(argc);
    for(int i=0;i<argc;++i)wide.push_back(std::filesystem::path(argv[i]).wstring());
    std::vector<wchar_t*> pointers;for(auto& text:wide)pointers.push_back(text.data());
    pointers.push_back(nullptr);
    const int result=wmain(argc,pointers.data());
    d2::reportAssetTimes(std::cout,"headless-test");
    return result;
}
