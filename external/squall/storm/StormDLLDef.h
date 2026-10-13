#pragma once

#if !defined(D2FUNC_DLL)
#define D2FUNC_DLL(DLL, NAME, RETURN, CONV, ARGS, OFFSET) \
	DLL##_DLL_DECL RETURN CONV DLL##_##NAME ARGS;
#endif // !defined(D2FUNC_DLL)

#if !defined(STORM_DLL_DECL)

#ifdef STORM_IMPL
#define STORM_DLL_DECL // We use .def files, not dllexport
#else
#define STORM_DLL_DECL __declspec( dllimport )
#endif

#endif // !defined(STORM_DLL_DECL)