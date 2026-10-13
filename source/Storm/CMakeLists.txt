
D2MOO_prepare_targets(Storm)

target_link_libraries(${StormImplName} PUBLIC D2CommonDefinitions squall::Storm)
target_compile_definitions(${StormImplName} PUBLIC WITH_SQUALL=1)

target_sources(${StormImplName}
  PRIVATE
    src/Storm.cpp

    include/Storm.h
    include/StormHandles.h
)

D2MOO_target_source_group(Storm)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  # Native x64 Storm file API: MPQ archives through StormLib (the original Storm.dll is 32-bit only).
  target_sources(${StormImplName}
    PRIVATE
      src/x64/Mpq.h
      src/x64/Mpq.cpp
      src/x64/StormFiles.cpp
      src/x64/StormUnicode.cpp
      src/x64/StormMisc.cpp
  )
  target_link_libraries(${StormImplName} PRIVATE StormLib::storm)
  target_compile_features(${StormImplName} PRIVATE cxx_std_17)
endif()
