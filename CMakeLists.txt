cmake_minimum_required(VERSION 3.15...3.29)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Do not build in-source. Please remove CMakeCache.txt and the CMakeFiles/ directory. Then build out-of-source.")
endif()

# The game libraries D2Next runs in-process (D2Common, D2Game, Fog, Storm...), from D2MOO's reconstruction of
# Diablo II 1.10f. The application itself is modern/ (its CMakeLists.txt adds this directory).
project("D2NextGame" LANGUAGES C CXX VERSION 0.1.0)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(D2MOO_IS_ROOT_PROJECT TRUE)
endif()

# Standard CMake modules
include(CMakeDependentOption)             # This is a really useful scripts that creates options that depends on other options. It can even be used with generator expressions !
include(GNUInstallDirs)                   # This will define the default values for installation directories (all platforms even if named GNU)
include(CTest)                            # Must be called before adding tests but after calling project(). This automatically calls enable_testing() and configures ctest targets when using Make/Ninja
include(cmake/D2MOO.TargetsHelpers.cmake) # Used to setup targets more easily

#####################
##     OPTIONS     ##
#####################

set(D2MOO_ORDINALS_VERSION "1.10f" CACHE STRING "Name of the version to be used for ordinals and patches")
if(D2MOO_IS_ROOT_PROJECT AND NOT DEFINED BUILD_SHARED_LIBS AND D2MOO_ORDINALS_VERSION VERSION_LESS "1.14")
  set(BUILD_SHARED_LIBS ON CACHE BOOL "Should we build shared libraries (.DLLs) instead of static libraries.")
endif()

option(D2MOO_WITH_STATIC_TESTS "Enable static tests for struct layouts. Use for original game only, not mods." ON)
option(D2MOO_MODERN_WINDOWS_COMPAT "Skip the original 1.10f Fog check of undocumented Windows critical-section internals" OFF)
cmake_dependent_option(D2MOO_BUILD_TESTS
    "Enable D2Moo project tests targets" ON # By default we want tests if CTest is enabled
    "BUILD_TESTING" OFF # Stay coherent with CTest variables
)

if(CMAKE_GENERATOR MATCHES "Visual Studio" AND MSVC)
  option(D2MOO_ENABLE_PARALLEL_BUILD "Force /MP parallel compilation" ${D2MOO_IS_ROOT_PROJECT})
  if(D2MOO_ENABLE_PARALLEL_BUILD)
    add_compile_options("/MP")
  endif()
endif()

if(D2MOO_IS_ROOT_PROJECT)
  # clang-cl
  if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang" AND "${CMAKE_CXX_SIMULATE_ID}" STREQUAL "MSVC")
    add_compile_options("-Wno-microsoft-cast" "-Wno-multichar")
  endif()
endif()


# It is always easier to navigate in an IDE when projects are organized in folders.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Systems without Windows.h (Android, Linux, the web): the part of it the game libraries use (external/win32compat).
if(NOT WIN32)
  set(D2MOO_WIN32COMPAT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/win32compat/include")
  include_directories(BEFORE SYSTEM "${D2MOO_WIN32COMPAT_DIR}")
  # The code was written for MSVC on x86: char is signed there (it is unsigned on ARM, which would change what the game
  # computes), and MSVC lets function pointers convert to void* (the skill and level tables store them so).
  add_compile_options(-fsigned-char -fms-extensions -Wno-microsoft-cast -Wno-microsoft-anon-tag -Wno-ignored-attributes
    -Wno-ignored-pragmas -Wno-unknown-pragmas -Wno-pragma-pack -Wno-multichar)
  if(NOT CMAKE_HOST_WIN32)
    # Some sources write <windows.h>; on case-sensitive file systems that needs its own name.
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/win32compat/windows.h" "#pragma once\n#include \"${D2MOO_WIN32COMPAT_DIR}/Windows.h\"\n")
    include_directories(BEFORE SYSTEM "${CMAKE_CURRENT_BINARY_DIR}/win32compat")
  endif()
endif()

# 3rd party dependencies
add_subdirectory(external)

add_subdirectory(source)

#===========#
#   Tests   #
#===========#

# Checks that the 64-bit DLLs load and export every ordinal their .def files declare.
if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND BUILD_SHARED_LIBS AND BUILD_TESTING AND TARGET D2Game)
  add_executable(D2DllLoadTest tools/x64/D2DllLoadTest.cpp)
  target_compile_features(D2DllLoadTest PRIVATE cxx_std_17)
  add_dependencies(D2DllLoadTest D2Common D2Game)
  add_test(NAME x64.dll_load
    COMMAND D2DllLoadTest "$<TARGET_FILE_DIR:D2Game>" "${CMAKE_CURRENT_SOURCE_DIR}/source" "${D2MOO_ORDINALS_VERSION}")
endif()

if(D2MOO_BUILD_TESTS)
    # Let the user add options to the test runner if needed
    set(TEST_RUNNER_PARAMS "--force-colors=true" CACHE STRING "Options to add to our test runners commands")
    # In a real project you most likely want to exclude test folders
    # list(APPEND CUSTOM_COVERAGE_EXCLUDE "/test/")
    
    # You can setup some custom variables and add them to the CTestCustom.cmake.in template to have custom ctest settings
    # For example, you can exclude some directories from the coverage reports such as third-parties and tests
    configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/cmake/CTestCustom.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/CTestCustom.cmake
        @ONLY
    )
endif()
