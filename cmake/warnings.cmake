add_library(svn-lfs-export_warnings INTERFACE)

set(CLANG_WARNINGS
	-Wall
	-Wextra
	-Wshadow
	-Wnon-virtual-dtor
	-Wold-style-cast
	-Wcast-align
	-Wunused
	-Woverloaded-virtual
	-Wpedantic
	-Wconversion
	-Wsign-conversion
	-Wnull-dereference
	-Wdouble-promotion
	-Wformat=2
	-Wimplicit-fallthrough
)

set(GCC_WARNINGS
	${CLANG_WARNINGS}
	-Wmisleading-indentation
	-Wduplicated-cond
	-Wduplicated-branches
	-Wlogical-op
	-Wuseless-cast
	-Wsuggest-override
)

if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
	set(PROJECT_WARNINGS ${CLANG_WARNINGS})
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	set(PROJECT_WARNINGS ${GCC_WARNINGS})
else()
	message(AUTHOR_WARNING "No compiler warnings set for CXX compiler: '${CMAKE_CXX_COMPILER_ID}'")
endif()

target_compile_options(svn-lfs-export_warnings INTERFACE ${PROJECT_WARNINGS})
