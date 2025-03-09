cmake_minimum_required(VERSION 3.28)
project(svn-lfs-export)

include(FetchContent)

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
	GIT_TAG        v3.2
)
FetchContent_MakeAvailable(argparse)

FetchContent_Declare(
	tomlplusplus
	GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
	GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

add_executable(${PROJECT_NAME} src/main.cpp)

target_link_libraries(${PROJECT_NAME} fmt::fmt argparse tomlplusplus::tomlplusplus)

target_compile_definitions(${PROJECT_NAME} PRIVATE
	TOML_ENABLE_FORMATTERS=0
	TOML_EXCEPTIONS=0
)
