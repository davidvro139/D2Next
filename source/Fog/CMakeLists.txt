
D2MOO_prepare_targets(Fog)

target_link_libraries(${FogImplName} PUBLIC D2CommonDefinitions Storm)

target_sources(${FogImplName}
  PRIVATE
	src/Fog.cpp
	
	# Official file names
	src/AsyncData.cpp
	src/BitManip/BitManip.cpp
	src/BitManip/StringPack.cpp
	src/D2QSQueue.cpp
	src/D2QSSocket.cpp
	src/DataArrays.cpp
	src/ErrorManager.cpp
	src/ErrorManager.cpp
	src/Excel/Excel.cpp
	src/Exceptions.cpp
	src/Gamma.cpp
	src/IFF.cpp
	src/LogManager.cpp
	src/Mem/mem.cpp
	src/QServer/QServer.cpp
	src/QServer/QServer98.cpp
	src/QServer/QServerNT.cpp
	src/Safesock.cpp
	src/String/FogString.cpp
	src/System/System.cpp
	
	# Made up filename
	src/Calc.cpp
	
	# Headers
	include/Fog.h
	include/D2BitManip.h
	include/Safesock.h
	include/System/System.h
)

if(NOT WIN32)
  # Winsock and the Windows thread pools: only D2Net's real network uses them, and builds for other systems (Android,
  # Linux, the web) run the game in-process with the stand-in transport (tools/x64/MockD2Net.cpp).
  get_target_property(FOG_SOURCES ${FogImplName} SOURCES)
  list(REMOVE_ITEM FOG_SOURCES src/D2QSSocket.cpp src/QServer/QServer.cpp src/QServer/QServer98.cpp src/QServer/QServerNT.cpp src/Safesock.cpp)
  set_property(TARGET ${FogImplName} PROPERTY SOURCES ${FOG_SOURCES})
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  # Native x64 implementations of what the original 32-bit Fog.dll provides (see D2FUNC_DLL_X64 in D2Dll.h).
  # On x86 the original DLL is used, so none of this is compiled there.
  target_sources(${FogImplName}
    PRIVATE
      src/x64/File.cpp
      src/x64/Linker.cpp
      src/x64/Log.cpp
      src/x64/Memory.cpp
      src/x64/Utility.cpp
      src/x64/FogState.h
      src/x64/SinTable.inc
      include/FogHost.h
  )
  target_compile_features(${FogImplName} PRIVATE cxx_std_20)
endif()

if(D2MOO_WITH_STATIC_TESTS)
  target_sources(Fog
    PRIVATE
      statictests/Fog_staticchecks.cpp
  )
endif()

D2MOO_target_source_group(Fog)
if(D2MOO_MODERN_WINDOWS_COMPAT AND D2MOO_ORDINALS_VERSION STREQUAL "1.10f")
  target_compile_definitions(Fog PRIVATE D2MOO_MODERN_WINDOWS_COMPAT)
endif()
