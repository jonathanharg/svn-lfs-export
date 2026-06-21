cmake_minimum_required(VERSION 3.31)

project(
	svn-lfs-export
	VERSION 1.0.0
	LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules")

find_package(APR REQUIRED)
find_package(Subversion REQUIRED)

add_subdirectory(extern/argparse SYSTEM)
add_subdirectory(extern/fmt SYSTEM)
add_subdirectory(extern/tomlplusplus SYSTEM)
add_subdirectory(extern/picosha2 SYSTEM)

set(BUILD_TZ_LIB ON)
set(USE_SYSTEM_TZ_DB ON)
add_subdirectory(extern/date SYSTEM)

set(USE_HTTPS
	OFF
	CACHE STRING "" FORCE
)
set(USE_SSH
	OFF
	CACHE STRING "" FORCE
)
set(USE_BUNDLED_ZLIB
	ON
	CACHE STRING "" FORCE
)
set(BUILD_TESTS
	OFF
	CACHE BOOL "" FORCE
)
set(BUILD_CLI
	OFF
	CACHE BOOL "" FORCE
)
set(BUILD_SHARED_LIBS
	OFF
	CACHE BOOL "" FORCE
)
add_subdirectory(extern/libgit2 SYSTEM)

set(ABSL_PROPAGATE_CXX_STD
	ON
	CACHE BOOL "" FORCE
)
set(ABSL_ENABLE_INSTALL
	ON
	CACHE BOOL "" FORCE
)
set(BUILD_TESTING
	OFF
	CACHE BOOL "" FORCE
)
add_subdirectory(extern/abseil SYSTEM)
add_subdirectory(extern/re2 SYSTEM)

include(cmake/warnings.cmake)

set(CODEGEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/codegen)
file(READ example_config.toml EXAMPLE_CONFIG)
configure_file(src/ExampleConfig.hpp.in ${CODEGEN_DIR}/ExampleConfig.hpp @ONLY)
set_property(
	DIRECTORY
	APPEND
	PROPERTY CMAKE_CONFIGURE_DEPENDS src/ExampleConfig.hpp.in example_config.toml
)

add_executable(
	svn-lfs-export
	src/Config.cpp
	src/Git.cpp
	src/Main.cpp
	src/Svn.cpp
	src/Writer.cpp
)

target_compile_features(svn-lfs-export PUBLIC cxx_std_23)
target_include_directories(svn-lfs-export PUBLIC "${PROJECT_SOURCE_DIR}/src" ${CODEGEN_DIR})

target_include_directories(svn-lfs-export SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/subprocess.h)

target_link_libraries(
	svn-lfs-export
	PUBLIC APR::APR
		   argparse::argparse
		   date::date
		   date::date-tz
		   fmt::fmt
		   libgit2
		   libgit2package
		   picosha2
		   re2::re2
		   Subversion::fs
		   Subversion::repos
		   Subversion::subr
		   tomlplusplus::tomlplusplus
		   project_warnings
)
target_compile_definitions(svn-lfs-export PUBLIC TOML_ENABLE_FORMATTERS=0 TOML_EXCEPTIONS=0 PROJECT_VERSION="${PROJECT_VERSION}")
