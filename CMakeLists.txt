cmake_minimum_required(VERSION 3.28)

project(
	svn-lfs-export
	VERSION 0.1.0
	LANGUAGES CXX
)

get_filename_component(SRCDIR "${CMAKE_SOURCE_DIR}" REALPATH)
get_filename_component(BINDIR "${CMAKE_BINARY_DIR}" REALPATH)
if(SRCDIR STREQUAL BINDIR)
	message(FATAL_ERROR "Run CMake from a build directory.")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	message(STATUS "Setting build type to 'RelWithDebInfo' as none was specified.")
	set(CMAKE_BUILD_TYPE
		RelWithDebInfo
		CACHE STRING "Choose the type of build." FORCE
	)
	# Set the possible values of build type for cmake-gui, ccmake
	set_property(
		CACHE CMAKE_BUILD_TYPE
		PROPERTY STRINGS
				 "Debug"
				 "Release"
				 "MinSizeRel"
				 "RelWithDebInfo"
	)
endif()

option(ENABLE_LTO "Enable LTO" OFF)
option(ENABLE_WARNINGS_AS_ERRORS "Treat Warnings As Errors" OFF)
option(ENABLE_ASAN "Enable address sanitizer" OFF)
option(ENABLE_LSAN "Enable leak sanitizer" OFF)
option(ENABLE_UBSAN "Enable undefined sanitizer" OFF)
option(ENABLE_TSAN "Enable thread sanitizer" OFF)
option(ENABLE_MSAN "Enable memory sanitizer" OFF)
option(ENABLE_CPPCHECK "Enable cpp-check analysis" OFF)

if(ENABLE_LTO)
	include(CheckIPOSupported)
	check_ipo_supported(RESULT lto_supported OUTPUT lto_error)
	if(lto_supported)
		set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
	else()
		message(SEND_ERROR "IPO is not supported: ${lto_error}")
	endif()
endif()

include(cmake/dependencies.cmake)
find_package(APR REQUIRED)
find_package(Subversion REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(libgit2 REQUIRED)

include(cmake/warnings.cmake)
include(cmake/cppcheck.cmake)
include(cmake/sanitizers.cmake)

add_executable(${PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
target_link_libraries(
	${PROJECT_NAME}
	PRIVATE applib
			argparse::argparse
			project_sanitizers
			project_warnings
)

file(
	GLOB_RECURSE
	lib_sources
	CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(REMOVE_ITEM lib_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
add_library(applib ${lib_sources})
target_link_libraries(
	applib
	PUBLIC APR::APR
		   OpenSSL::Crypto
		   fmt::fmt
		   libgit2::libgit2package
		   re2::re2
		   Subversion::fs
		   Subversion::repos
		   Subversion::subr
		   tomlplusplus::tomlplusplus
		   project_sanitizers
		   project_warnings
	PRIVATE date::date date-tz
)
target_compile_definitions(applib PUBLIC TOML_ENABLE_FORMATTERS=0 TOML_EXCEPTIONS=0)

if(BUILD_TESTING)
	file(
		GLOB_RECURSE
		test_sources
		CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp"
	)
	add_executable(tests ${test_sources})
	target_include_directories(tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
	target_link_libraries(
		tests
		PRIVATE applib
				Catch2::Catch2WithMain
				project_sanitizers
				project_warnings
	)

	list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
	include(CTest)
	include(Catch)
	catch_discover_tests(tests)
endif()
