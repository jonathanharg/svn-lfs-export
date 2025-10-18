if(APR_FIND_ALREADY_INCLUDED)
	return()
endif()
set(APR_FIND_ALREADY_INCLUDED TRUE)

set(_apr_root_hint
	${APR_ROOT} "/opt/homebrew/opt/apr"
	CACHE PATH "Root directory of APR installation"
)

set(_apr_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
set(CMAKE_FIND_LIBRARY_SUFFIXES .a)

find_path(
	APR_INCLUDE_DIR
	NAMES apr.h
	HINTS ${_apr_root_hint} ENV APRPATH
	PATH_SUFFIXES include include/apr-1 include/apr-1.0
	DOC "APR include directory"
)

find_library(
	APR_LIBRARY
	NAMES apr-1 apr-1.0
	HINTS ${_apr_root_hint} ENV APRPATH
	PATH_SUFFIXES lib
	DOC "APR library"
)

if(APR_INCLUDE_DIR AND APR_LIBRARY)
	set(APR_FOUND TRUE)
else()
	set(APR_FOUND FALSE)
endif()

if(APR_FOUND)
	set(APR_INCLUDE_DIRS ${APR_INCLUDE_DIR})
	set(APR_LIBRARIES ${APR_LIBRARY})

	if(NOT TARGET APR::APR)
		add_library(APR::APR UNKNOWN IMPORTED)

		set_target_properties(
			APR::APR PROPERTIES IMPORTED_LOCATION ${APR_LIBRARY} INTERFACE_INCLUDE_DIRECTORIES ${APR_INCLUDE_DIR}
		)
	endif()
endif()

if(APR_FOUND)
	message(STATUS "Found APR: ${APR_LIBRARY}")
else()
	message(STATUS "APR not found")
endif()

set(CMAKE_FIND_LIBRARY_SUFFIXES ${_apr_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
