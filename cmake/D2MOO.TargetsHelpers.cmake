
function(D2MOO_set_dll_decl DLLTargetName PrivateDllDeclValue InterfaceDllDeclValue)
  if(TARGET ${${DLLTargetName}ImplName})
	target_compile_definitions(${${DLLTargetName}ImplName}
      # Ideally, we would also set it to ${PrivateDllDeclValue} in the interface (targets linking this needs the correct value).
	  # But this would prevent from linking ${DLLTargetName}Static as PUBLIC for ${DLLTargetName}, which means we would need to duplicate all INTERFACE properties correctly.
	  # Instead, we rely on the fact that if you want to link ${DLLTargetName}Static but do not define ${DLLTargetNameUpperCase}_DLL_DECL and include a file requiring it, nothing will build.
	  # In practice if you want to link the static lib, you'd better not set BUILD_SHARED_LIBS anyway and use ${DLLTargetName} instead of ${DLLTargetName}Static since it only really exists for tests.
	  PRIVATE ${DLLTargetNameUpperCase}_DLL_DECL=${PrivateDllDeclValue}
	)
  endif()
  if(TARGET ${DLLTargetName}Tests)
	target_compile_definitions(${DLLTargetName}Tests
	  PRIVATE ${DLLTargetNameUpperCase}_DLL_DECL=${PrivateDllDeclValue}
	)
  endif()
  target_compile_definitions(${DLLTargetName} 
	PRIVATE ${DLLTargetNameUpperCase}_DLL_DECL=${PrivateDllDeclValue}
	INTERFACE ${DLLTargetNameUpperCase}_DLL_DECL=${InterfaceDllDeclValue} # Consuming targets will need to import
  )
endfunction()

function(D2MOO_get_def_file DllTargetName OutDefFile)
  set(${OutDefFile} "${CMAKE_CURRENT_SOURCE_DIR}/definitions/${DLLTargetName}.${D2MOO_ORDINALS_VERSION}.def" PARENT_SCOPE)
endfunction()

function(D2MOO_configure_exports DLLTargetName)
  set(options FORCE_EXPORT IGNORE_MISSING_DEF)
  cmake_parse_arguments(PARSE_ARGV 1 this "${options}" "" "")

  get_target_property(Library_TYPE ${DLLTargetName} TYPE)
  string(TOUPPER "${DLLTargetName}" DLLTargetNameUpperCase)

  set(ShouldExport ${this_FORCE_EXPORT})
  if(Library_TYPE STREQUAL SHARED_LIBRARY)
    set(EXPORT_IF_SHARED_LIB "__declspec( dllexport )")
    set(IMPORT_IF_SHARED_LIB "__declspec( dllimport )")
    D2MOO_get_def_file(${DLLTargetName} DEF_FILE_NAME)
    if(EXISTS "${DEF_FILE_NAME}")
      if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # The .def files are written for x86: they may list C++ names in their 32-bit mangled form (e.g. the
        # 'QAE' thiscall encoding), which does not exist on x64. Drop those lines; such symbols are still
        # exported by name through __declspec(dllexport). Plain C-named ordinals are kept unchanged.
        file(READ "${DEF_FILE_NAME}" D2MOO_DEF_CONTENT)
        string(REGEX REPLACE "(^|\n)[ \t]*\\?[^\n]*" "\\1" D2MOO_DEF_CONTENT "${D2MOO_DEF_CONTENT}")
        set(D2MOO_X64_DEF_FILE "${CMAKE_CURRENT_BINARY_DIR}/${DLLTargetName}.x64.def")
        file(WRITE "${D2MOO_X64_DEF_FILE}" "${D2MOO_DEF_CONTENT}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DEF_FILE_NAME}")
        target_sources(${DLLTargetName} PRIVATE "${D2MOO_X64_DEF_FILE}")
      else()
        target_sources(${DLLTargetName} PRIVATE "${DEF_FILE_NAME}")
      endif()
    else()
      set(ShouldExport TRUE)
	  if(BUILD_SHARED_LIBS AND NOT this_IGNORE_MISSING_DEF)
        message(WARNING "Def file '${DEF_FILE_NAME}' not found.")
	  endif()
    endif()
    else()
  endif()
  
  if(ShouldExport)
    D2MOO_set_dll_decl(${DLLTargetName} "${EXPORT_IF_SHARED_LIB}" "${IMPORT_IF_SHARED_LIB}")
  else()
    D2MOO_set_dll_decl(${DLLTargetName} "" "${IMPORT_IF_SHARED_LIB}")
  endif()
endfunction()

function(D2MOO_prepare_targets LibraryName)
  # The real target you should be linking/installing
  add_library(${LibraryName})
  
  if(BUILD_SHARED_LIBS)
    set(${LibraryName}ImplName ${LibraryName}Objects)
    # We expose this library mainly for unit tests since we wouldn't be able to call non exported functions otherwise.
    add_library(${${LibraryName}ImplName} OBJECT)
    target_link_libraries(${LibraryName} PUBLIC ${${LibraryName}ImplName})
  
    # Add src/DllMain.cpp since we build a .dll
    target_sources(${LibraryName} PRIVATE src/DllMain.cpp)
  else()
    set(${LibraryName}ImplName ${LibraryName})
  endif()
   # Expose the lib name to the parent scope
  set(${LibraryName}ImplName ${${LibraryName}ImplName} PARENT_SCOPE)

  # Always add the include directory by default
  target_include_directories(${${LibraryName}ImplName} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

  # Add tests if there are any
  if(D2MOO_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_subdirectory(tests)
  endif()
  
  # Add the definition file that matches functions with their ordinals, and setup ${LIBRARYNAME}_DLL_DECL
  # Needs to be done after the creation of the test target so that we can set ${LIBRARYNAME}_DLL_DECL for it too.
  D2MOO_configure_exports(${LibraryName} ${ARGN})
  
  # We need to provide at least one file for those targets.
  # We have this issue if we do not provide a .def file for example.
  #get_target_property(TargetSources ${LibraryName} SOURCES)
  #if(NOT TargetSources)
  #  target_sources(${LibraryName} PRIVATE ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dummy.cpp)
  #endif()
endfunction()

function(D2MOO_target_source_group LibraryName)
  get_target_property(TargetSources ${${LibraryName}ImplName} SOURCES)
  list(REMOVE_ITEM TargetSources "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dummy.cpp")
  source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${TargetSources})
endfunction()
