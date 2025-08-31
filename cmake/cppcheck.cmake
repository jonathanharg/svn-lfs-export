if(ENABLE_CPPCHECK)
	find_program(CPPCHECK cppcheck)
	if(CPPCHECK)
		set(CPPCHECK_TEMPLATE "gcc")

		if("${CPPCHECK_OPTIONS}" STREQUAL "")
			# Enable all warnings that are actionable by the user of this toolset
			# style should enable the other 3, but we'll be explicit just in case
			set(SUPPRESS_DIR "*:${CMAKE_CURRENT_BINARY_DIR}/_deps/*")
			message(STATUS "CPPCHECK_OPTIONS suppress: ${SUPPRESS_DIR}")
			set(CMAKE_CXX_CPPCHECK
				${CPPCHECK}
				--template=${CPPCHECK_TEMPLATE}
				--enable=style,performance,warning,portability
				--inline-suppr
				--check-level=exhaustive
				# We cannot act on a bug/missing feature of cppcheck
				--suppress=cppcheckError
				--suppress=internalAstError
				# if a file does not have an internalAstError, we get an unmatchedSuppression error
				--suppress=unmatchedSuppression
				# noisy and incorrect sometimes
				--suppress=passedByValue
				# ignores code that cppcheck thinks is invalid C++
				--suppress=syntaxError
				--suppress=preprocessorErrorDirective
				# ignores static_assert type failures
				--suppress=knownConditionTrueFalse
				--inconclusive
				--suppress=${SUPPRESS_DIR}
			)
		else()
			# if the user provides a CPPCHECK_OPTIONS with a template specified, it will override this template
			set(CMAKE_CXX_CPPCHECK ${CPPCHECK} --template=${CPPCHECK_TEMPLATE} ${CPPCHECK_OPTIONS})
		endif()

		set(CMAKE_CXX_CPPCHECK ${CMAKE_CXX_CPPCHECK} --std=c++${CMAKE_CXX_STANDARD})

		if(${ENABLE_WARNINGS_AS_ERRORS})
			list(APPEND CMAKE_CXX_CPPCHECK --error-exitcode=2)
		endif()
	else()
		message(WARNING "cppcheck requested but executable not found")
	endif()
endif()
