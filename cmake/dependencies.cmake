include(FetchContent)

FetchContent_Declare(
	fmt
	GIT_REPOSITORY https://github.com/fmtlib/fmt.git
	GIT_TAG 11.2.0
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
	GIT_TAG 2025-08-05
)
FetchContent_MakeAvailable(re2)

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

FetchContent_Declare(
	Catch2
	GIT_REPOSITORY https://github.com/catchorg/Catch2.git
	GIT_TAG v3.9.1
)
FetchContent_MakeAvailable(Catch2)
