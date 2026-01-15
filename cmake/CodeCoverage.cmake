# Code Coverage Module for Clang
# Usage:
#   pnkr_instrument_target(target_name)
#   pnkr_add_coverage_report(test_target_name)

function(pnkr_check_coverage_tools)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(WARNING "Code coverage requires Clang compiler. Coverage disabled.")
        return()
    endif()

    get_filename_component(COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    
    find_program(LLVM_PROFDATA_PATH llvm-profdata HINTS "${COMPILER_DIR}")
    find_program(LLVM_COV_PATH llvm-cov HINTS "${COMPILER_DIR}")

    if(NOT LLVM_PROFDATA_PATH OR NOT LLVM_COV_PATH)
        message(WARNING "llvm-profdata or llvm-cov not found. Coverage disabled.")
        set(PNKR_COVERAGE_TOOLS_FOUND OFF PARENT_SCOPE)
    else()
        message(STATUS "Found llvm-profdata: ${LLVM_PROFDATA_PATH}")
        message(STATUS "Found llvm-cov: ${LLVM_COV_PATH}")
        set(PNKR_COVERAGE_TOOLS_FOUND ON PARENT_SCOPE)
    endif()
endfunction()

function(pnkr_instrument_target target_name)
    if(NOT PNKR_ENABLE_COVERAGE)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        return()
    endif()

    target_compile_options(${target_name} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(${target_name} PRIVATE -fprofile-instr-generate)
endfunction()

function(pnkr_add_coverage_report test_target_name)
    if(NOT PNKR_ENABLE_COVERAGE)
        return()
    endif()

    if(NOT PNKR_COVERAGE_TOOLS_FOUND)
        message(WARNING "Coverage tools not found, skipping report target for ${test_target_name}")
        return()
    endif()

    set(COVERAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/coverage_report")
    set(PROFRAW_FILE "${CMAKE_CURRENT_BINARY_DIR}/default.profraw")
    set(PROFDATA_FILE "${CMAKE_CURRENT_BINARY_DIR}/coverage.profdata")

    # The command to run the test and generate raw profile data
    # Note: LLVM_PROFILE_FILE env var can set the output path, but default is default.profraw in working dir
    
    add_custom_target(${test_target_name}_coverage
        # 1. Run the tests
        COMMAND ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE=${PROFRAW_FILE} $<TARGET_FILE:${test_target_name}>
        
        # 2. Merge profile data
        COMMAND ${LLVM_PROFDATA_PATH} merge -sparse ${PROFRAW_FILE} -o ${PROFDATA_FILE}
        
        # 3. Generate HTML report
        # We explicitly verify the engine library object files are included if possible, 
        # but standard source-based coverage usually picks up linked code if instrumented.
        # We ignore system headers and third-party code.
        COMMAND ${LLVM_COV_PATH} show $<TARGET_FILE:${test_target_name}> 
                -instr-profile=${PROFDATA_FILE} 
                -format=html 
                -output-dir=${COVERAGE_DIR}
                -ignore-filename-regex="third-party|vcpkg|tests|external"
        
        # 4. Also print a summary to console
        COMMAND ${LLVM_COV_PATH} report $<TARGET_FILE:${test_target_name}>
                -instr-profile=${PROFDATA_FILE}
                -ignore-filename-regex="third-party|vcpkg|tests|external"

        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating code coverage report in ${COVERAGE_DIR}"
        DEPENDS ${test_target_name}
    )

    message(STATUS "Added coverage target: ${test_target_name}_coverage")
endfunction()
