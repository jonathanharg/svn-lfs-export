cmake_minimum_required(VERSION 3.31)

project(
	svn-lfs-export
	VERSION 0.1.0
	LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules")

find_package(APR REQUIRED)
find_package(Subversion REQUIRED)

find_package(argparse CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS process)
find_package(date CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(libgit2 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(re2 CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

include(cmake/warnings.cmake)

set(CODEGEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/codegen)
file(READ example_config.toml EXAMPLE_CONFIG)
configure_file(src/example_config.hpp.in ${CODEGEN_DIR}/example_config.hpp @ONLY)
set_property(
	DIRECTORY
	APPEND
	PROPERTY CMAKE_CONFIGURE_DEPENDS src/example_config.hpp.in example_config.toml
)

# Executable
add_executable(
	svn-lfs-export
	src/config.cpp
	src/git.cpp
	src/main.cpp
	src/svn.cpp
	src/writer.cpp
)

target_compile_features(svn-lfs-export PUBLIC cxx_std_23)
target_include_directories(svn-lfs-export PUBLIC "${PROJECT_SOURCE_DIR}/src" ${CODEGEN_DIR})

target_link_libraries(
	svn-lfs-export
	PUBLIC APR::APR
		   argparse::argparse
		   Boost::process
		   date::date
		   date::date-tz
		   fmt::fmt
		   libgit2::libgit2package
		   OpenSSL::Crypto
		   re2::re2
		   Subversion::fs
		   Subversion::repos
		   Subversion::subr
		   tomlplusplus::tomlplusplus
		   svn-lfs-export_warnings
)
target_compile_definitions(
	svn-lfs-export PUBLIC TOML_ENABLE_FORMATTERS=0 TOML_EXCEPTIONS=0 PROJECT_VERSION="${PROJECT_VERSION}"
)
