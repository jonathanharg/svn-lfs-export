if(Subversion_FIND_ALREADY_INCLUDED)
	return()
endif()
set(Subversion_FIND_ALREADY_INCLUDED TRUE)

set(_svn_root_hint
	${Subversion_ROOT}
	CACHE PATH "Root directory of Subversion installation"
)

find_path(
	Subversion_INCLUDE_DIR
	NAMES svn_version.h
	HINTS ${_svn_root_hint} ENV SubversionPATH
	PATH_SUFFIXES include include/subversion-1
	DOC "Subversion include directory"
)

find_library(
	Subversion_LIB_fs
	NAMES svn_fs-1
	HINTS ${_svn_root_hint} ENV SubversionPATH
	PATH_SUFFIXES lib
	DOC "Subversion filesystem library"
)

find_library(
	Subversion_LIB_repos
	NAMES svn_repos-1
	HINTS ${_svn_root_hint} ENV SubversionPATH
	PATH_SUFFIXES lib
	DOC "Subversion repository library"
)

find_library(
	Subversion_LIB_subr
	NAMES svn_subr-1
	HINTS ${_svn_root_hint} ENV SubversionPATH
	PATH_SUFFIXES lib
	DOC "Subversion subroutine library"
)

if(Subversion_INCLUDE_DIR
   AND Subversion_LIB_fs
   AND Subversion_LIB_repos
   AND Subversion_LIB_subr
)
	set(Subversion_FOUND TRUE)
else()
	set(Subversion_FOUND FALSE)
endif()

if(Subversion_FOUND)
	set(Subversion_INCLUDE_DIRS ${Subversion_INCLUDE_DIR})
	set(Subversion_LIBRARIES ${Subversion_LIB_fs} ${Subversion_LIB_repos} ${Subversion_LIB_subr})

	if(Subversion_LIB_fs AND NOT TARGET Subversion::fs)
		add_library(Subversion::fs UNKNOWN IMPORTED)
		set_target_properties(
			Subversion::fs PROPERTIES IMPORTED_LOCATION ${Subversion_LIB_fs} INTERFACE_INCLUDE_DIRECTORIES
																			 ${Subversion_INCLUDE_DIR}
		)
	endif()

	if(Subversion_LIB_repos AND NOT TARGET Subversion::repos)
		add_library(Subversion::repos UNKNOWN IMPORTED)
		set_target_properties(
			Subversion::repos PROPERTIES IMPORTED_LOCATION ${Subversion_LIB_repos} INTERFACE_INCLUDE_DIRECTORIES
																				   ${Subversion_INCLUDE_DIR}
		)
	endif()

	if(Subversion_LIB_subr AND NOT TARGET Subversion::subr)
		add_library(Subversion::subr UNKNOWN IMPORTED)
		set_target_properties(
			Subversion::subr PROPERTIES IMPORTED_LOCATION ${Subversion_LIB_subr} INTERFACE_INCLUDE_DIRECTORIES
																				 ${Subversion_INCLUDE_DIR}
		)
	endif()

endif()

if(Subversion_FOUND)
	message(STATUS "Found Subversion: ${Subversion_LIBRARIES} with includes ${Subversion_INCLUDE_DIR}")
else()
	message(STATUS "Subversion not found")
endif()
