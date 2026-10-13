
# Disable clang-tidy for external dependencies.
# This should really be avoided but that's what we get for including deps directly...
set(CMAKE_CXX_CLANG_TIDY "")
set(CMAKE_FOLDER external)

set(BUILD_SHARED_LIBS_BACKUP ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF) # Build external libs as static libs (or their default settings) for now 
  




#Doctest for unit tests
add_library(doctest INTERFACE)
target_include_directories(doctest SYSTEM
    INTERFACE
    doctest/doctest # note : will expose the parts/ folder...
)
add_library(doctest::doctest ALIAS doctest)


# Note: we only use squall for a few headers
# We may want to use the whole library at some point though.
add_library(squallstorm
	squall/storm/thread/CCritSect.cpp
)
target_include_directories(squallstorm SYSTEM PUBLIC squall)
if(WIN32)
  target_compile_definitions(squallstorm PUBLIC WHOA_SYSTEM_WIN)
elseif(APPLE)
  target_compile_definitions(squallstorm PUBLIC WHOA_SYSTEM_MAC)
else()
  # Android, Linux and the web (Emscripten) use the POSIX threads code.
  target_compile_definitions(squallstorm PUBLIC WHOA_SYSTEM_LINUX)
  if(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(squallstorm PUBLIC Threads::Threads)
  endif()
endif()
add_library(squall::Storm ALIAS squallstorm)

#STB for image writing
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM
    INTERFACE
    stb # note : will expose the parts/ folder...
)
add_library(stb::stb ALIAS stb)

# Restore BUILD_SHARED_LIBS

# StormLib reads the real MPQ archives for the native x64 Storm (see source/Storm/src/x64). The original 32-bit
# Storm.dll cannot be loaded into a 64-bit process. This copy is shared with the standalone engine in modern/.
# It must be a static library: as a DLL it would export the same SFile* names as Storm.dll.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(D2MOO_BUILD_SHARED_LIBS_BEFORE_STORMLIB ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)
  set(STORM_USE_BUNDLED_LIBRARIES ON CACHE BOOL "" FORCE)
  set(STORM_SKIP_INSTALL ON CACHE BOOL "" FORCE)
  # Wide-character paths on Windows; UTF-8 paths elsewhere.
  if(WIN32)
    set(STORM_UNICODE ON CACHE BOOL "" FORCE)
  else()
    set(STORM_UNICODE OFF CACHE BOOL "" FORCE)
  endif()
  add_subdirectory(${PROJECT_SOURCE_DIR}/modern/external/StormLib ${CMAKE_CURRENT_BINARY_DIR}/StormLib EXCLUDE_FROM_ALL)
  set(BUILD_SHARED_LIBS ${D2MOO_BUILD_SHARED_LIBS_BEFORE_STORMLIB})
  if(MSVC)
    # Fourteen StormLib C function names equal names Storm.dll exports; rename StormLib's copies (see StormLibRename.h)
    target_compile_options(storm PRIVATE "/FI${PROJECT_SOURCE_DIR}/source/Storm/src/x64/StormLibRename.h")
  else()
    target_compile_options(storm PRIVATE -include "${PROJECT_SOURCE_DIR}/source/Storm/src/x64/StormLibRename.h")
  endif()
endif()
