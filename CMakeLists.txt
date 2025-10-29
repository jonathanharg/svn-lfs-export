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
find_package(date CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(libgit2 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(re2 CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

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
	PRIVATE date::date date::date-tz svn-lfs-export_warnings
)

target_include_directories(svn-lfs-export_lib PUBLIC "\$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>")
target_compile_features(svn-lfs-export_lib PUBLIC cxx_std_23)
target_compile_definitions(svn-lfs-export_lib PUBLIC TOML_ENABLE_FORMATTERS=0 TOML_EXCEPTIONS=0)

# Executable
add_executable(svn-lfs-export_exe src/main.cpp)
set_property(TARGET svn-lfs-export_exe PROPERTY OUTPUT_NAME svn-lfs-export)
target_compile_features(svn-lfs-export_exe PRIVATE cxx_std_23)
target_link_libraries(svn-lfs-export_exe PRIVATE svn-lfs-export_lib argparse::argparse svn-lfs-export_warnings)

target_compile_definitions(svn-lfs-export_exe PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")

set(CODEGEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/codegen)
file(READ example_config.toml EXAMPLE_CONFIG)
configure_file(src/example_config.hpp.in ${CODEGEN_DIR}/example_config.hpp @ONLY)
set_property(
	DIRECTORY
	APPEND
	PROPERTY CMAKE_CONFIGURE_DEPENDS src/example_config.hpp.in example_config.toml
)
target_include_directories(svn-lfs-export_exe PRIVATE ${CODEGEN_DIR})

include(CTest)
if(BUILD_TESTING)
	add_subdirectory(test)
endif()
