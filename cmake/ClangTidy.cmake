# Find clang-tidy with find_program
# Custom paths can be specified with CMAKE_PROGRAM_DIR
# Set CLANG_TIDY_COMMAND and CMAKE_CXX_CLANG_TIDY if found

function(clangtidy_setup)
    option(CLANG_TIDY_FIX "Perform fixes for Clang-Tidy" OFF)
    find_program(
        CLANG_TIDY_COMMAND
        NAMES "clang-tidy"
        DOC "Path to clang-tidy executable"
    )

    if(CLANG_TIDY_COMMAND)
        if(CLANG_TIDY_FIX)
            set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}" "-fix" PARENT_SCOPE)
        else()
            set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}" PARENT_SCOPE)
        endif()
    else()
        message(FATAL_ERROR "CMake_RUN_CLANG_TIDY is ON but clang-tidy is not found!")
    endif()
endfunction()

