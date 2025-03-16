cmake_minimum_required(VERSION 3.28)
project(svn-lfs-export)

include(FetchContent)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

FetchContent_Declare(
	fmt
	GIT_REPOSITORY https://github.com/fmtlib/fmt.git
	GIT_TAG 11.1.4
)
FetchContent_MakeAvailable(fmt)

FetchContent_Declare(
	argparse
	GIT_REPOSITORY https://github.com/p-ranav/argparse.git
	GIT_TAG v3.2
)
FetchContent_MakeAvailable(argparse)

FetchContent_Declare(
	tomlplusplus
	GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
	GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

FetchContent_Declare(
	re2
	GIT_REPOSITORY https://github.com/google/re2.git
	GIT_TAG 2024-07-02
)
FetchContent_MakeAvailable(re2)

find_package(APR REQUIRED)

find_path(
	SVN_INCLUDE_DIR
	NAMES svn_client.h
	PATH_SUFFIXES include include/subversion-1 include/subversion-1.0
)

find_library(SVN_FS_LIBRARY NAMES svn_fs-1 PATH_SUFFIXES lib)
find_library(SVN_REPOS_LIBRARY NAMES svn_repos-1 PATH_SUFFIXES lib)
find_library(SVN_SUBR_LIBRARY NAMES svn_subr-1 PATH_SUFFIXES lib)

add_executable(${PROJECT_NAME} src/main.cpp)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)

target_include_directories(${PROJECT_NAME} PRIVATE ${SVN_INCLUDE_DIR})

target_link_libraries(
	${PROJECT_NAME}
	fmt::fmt
	argparse::argparse
	tomlplusplus::tomlplusplus
	re2::re2
	apr::apr
	${SVN_FS_LIBRARY}
	${SVN_REPOS_LIBRARY}
	${SVN_SUBR_LIBRARY}
)

target_compile_definitions(
	${PROJECT_NAME}
	PRIVATE TOML_ENABLE_FORMATTERS=0 TOML_EXCEPTIONS=0
)
