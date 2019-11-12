cmake_minimum_required(VERSION 3.5)
project(ecs VERSION 1.0.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ECS_SAMPLE_BUILD OFF CACHE BOOL "Build sample")

set(sources_to_format "")
set(targets_to_tidy "")

if (ECS_SAMPLE_BUILD)
    add_executable(sample sample.cpp)
    target_include_directories(sample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    target_compile_features(sample PUBLIC cxx_std_17)
    set_target_properties(sample PROPERTIES CXX_EXTENSIONS OFF)

    set(targets_to_tidy "${targets_to_tidy}" sample)
endif()


set(sources_to_format "${sources_to_format}" sample.cpp ECS.h)

##################################
# Utilities
##################################

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CUDA_COMPILER_LAUNCHER "${CCACHE_PROGRAM}") # CMake 3.9+
endif()

# A target named "clangformat", use make clangformat to format all specified files
include(ClangFormat)
clangformat_setup(${sources_to_format})

# If this finds clang-tidy, it will automatically set CMAKE_CXX_CLANG_TIDY,
include(ClangTidy)
clangtidy_setup()
if (CLANG_TIDY_COMMAND)
    message(STATUS "Your clang-tidy path is: ${CMAKE_CXX_CLANG_TIDY}")
    set_target_properties(${targets_to_tidy} PROPERTIES
        CXX_CLANG_TIDY "${CMAKE_CXX_CLANG_TIDY}"
    )
endif()
