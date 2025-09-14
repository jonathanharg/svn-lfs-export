include(FetchContent)

if(NOT fmt_FOUND)
	message(STATUS "fmt not found, falling back to FetchContent")
	FetchContent_Declare(
		fmt
		GIT_REPOSITORY https://github.com/fmtlib/fmt.git
		GIT_TAG 11.2.0
	)
	FetchContent_MakeAvailable(fmt)
endif()

if(NOT argparse_FOUND)
	message(STATUS "argparse not found, falling back to FetchContent")
	FetchContent_Declare(
		argparse
		GIT_REPOSITORY https://github.com/p-ranav/argparse.git
		GIT_TAG v3.2
	)
	FetchContent_MakeAvailable(argparse)
endif()

if(NOT tomlplusplus_FOUND)
	message(STATUS "tomlplusplus not found, falling back to FetchContent")
	FetchContent_Declare(
		tomlplusplus
		GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
		GIT_TAG v3.4.0
	)
	FetchContent_MakeAvailable(tomlplusplus)
endif()

if(NOT re2_FOUND)
	message(STATUS "re2 not found, falling back to FetchContent")
	FetchContent_Declare(
		re2
		GIT_REPOSITORY https://github.com/google/re2.git
		GIT_TAG 2025-08-05
	)
	FetchContent_MakeAvailable(re2)
endif()

# TODO: Remove the dependency when Apple C++ 20 support doesn't suck
if(NOT date_FOUND)
	message(STATUS "date not found, falling back to FetchContent")
	FetchContent_Declare(
		date
		GIT_REPOSITORY https://github.com/HowardHinnant/date.git
		GIT_TAG v3.0.4
	)
	set(BUILD_TZ_LIB
		ON
		CACHE BOOL "Build date/tz library"
	)
	set(USE_SYSTEM_TZ_DB
		ON
		CACHE BOOL "Use system time zone database"
	)
	FetchContent_MakeAvailable(date)
endif()

if(NOT Catch2_FOUND)
	message(STATUS "Catch2 not found, falling back to FetchContent")
	FetchContent_Declare(
		Catch2
		GIT_REPOSITORY https://github.com/catchorg/Catch2.git
		GIT_TAG v3.9.1
	)
	FetchContent_MakeAvailable(Catch2)
endif()
