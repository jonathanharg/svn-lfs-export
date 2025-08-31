if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
	set(SANITIZERS "")

	if(${ENABLE_ASAN})
		list(APPEND SANITIZERS "address")
	endif()

	if(${ENABLE_LSAN})
		list(APPEND SANITIZERS "leak")
	endif()

	if(${ENABLE_UBSAN})
		list(APPEND SANITIZERS "undefined")
	endif()

	if(${ENABLE_TSAN})
		if("address" IN_LIST SANITIZERS OR "leak" IN_LIST SANITIZERS)
			message(WARNING "Thread sanitizer does not work with Address and Leak sanitizer enabled")
		else()
			list(APPEND SANITIZERS "thread")
		endif()
	endif()

	if(${ENABLE_MSAN} AND CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
		message(
			WARNING
				"Memory sanitizer requires all the code (including libc++) to be MSan-instrumented otherwise it reports false positives"
		)
		if("address" IN_LIST SANITIZERS
		   OR "thread" IN_LIST SANITIZERS
		   OR "leak" IN_LIST SANITIZERS
		)
			message(WARNING "Memory sanitizer does not work with Address, Thread or Leak sanitizer enabled")
		else()
			list(APPEND SANITIZERS "memory")
		endif()
	endif()
endif()

list(
	JOIN
	SANITIZERS
	","
	LIST_OF_SANITIZERS
)

if(LIST_OF_SANITIZERS)
	if(NOT
	   "${LIST_OF_SANITIZERS}"
	   STREQUAL
	   ""
	)
		target_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=${LIST_OF_SANITIZERS})
		target_link_options(${PROJECT_NAME} PRIVATE -fsanitize=${LIST_OF_SANITIZERS})
	endif()
endif()
