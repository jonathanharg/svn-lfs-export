cmake_minimum_required(VERSION 3.31)

project(
	svn-lfs-export
	VERSION 0.1.0
	LANGUAGES CXX
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
	message(FATAL_ERROR "Run CMake from a build directory.")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules")

find_package(APR REQUIRED)
find_package(libgit2 REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Subversion REQUIRED)

# All other packages will fallback to FetchContent if not installed already
find_package(argparse QUIET)
find_package(date QUIET)
find_package(fmt QUIET)
find_package(re2 QUIET)
find_package(tomlplusplus QUIET)
include(cmake/dependencies.cmake)

include(cmake/warnings.cmake)

# Library
add_library(svn-lfs-export_lib OBJECT src/config.cpp src/git.cpp src/svn.cpp)

target_link_libraries(
	svn-lfs-export_lib
	PUBLIC APR::APR
		   OpenSSL::Crypto
		   fmt::fmt
		   libgit2::libgit2package
		   re2::re2
		   Subversion::fs
		   Subversion::repos
		   Subversion::subr
		   tomlplusplus::tomlplusplus
	PRIVATE date::date date-tz svn-lfs-export_warnings
)

target_include_directories(svn-lfs-export_lib PUBLIC "\$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>")

target_compile_features(svn-lfs-export_lib PUBLIC cxx_std_23)

target_compile_definitions(svn-lfs-export_lib PUBLIC TOML_ENABLE_FORMATTERS=0 TOML_EXCEPTIONS=0)

# Executable
add_executable(svn-lfs-export_exe src/main.cpp)

set_property(TARGET svn-lfs-export_exe PROPERTY OUTPUT_NAME svn-lfs-export)

target_compile_features(svn-lfs-export_exe PRIVATE cxx_std_23)

target_link_libraries(svn-lfs-export_exe PRIVATE svn-lfs-export_lib argparse::argparse svn-lfs-export_warnings)

include(CTest)
if(BUILD_TESTING)
	add_subdirectory(test)
endif()
