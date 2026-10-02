cmake_minimum_required(VERSION 3.28)

project(albor LANGUAGES CXX)

# c++ 23
set(CMAKE_CXX_STANDARD 23)
# enforce the c++23 standard
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# disable compiler specific things
set(CMAKE_CXX_EXTENSIONS OFF)

# generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# build configurations (§2.2): the flags below select which libraries exist in the link
option(ALBOR_DEV "Build the dev runtime extension (Dev configuration)" OFF)
option(ALBOR_TEST "Build the test configuration extras: automation core, headless backend, software renderer" OFF)
option(ALBOR_AUTOMATION "Build the automation core (Dev/Test configurations only, AD-10)" OFF)
option(ALBOR_PROTOTYPES "Build the Phase 0 prototypes of §30" ON)

# include should be relative to the root of the project
include_directories(".")

# a configuration is Release when neither the dev runtime extension nor the test extras are enabled (§2.2)
if(NOT ALBOR_DEV AND NOT ALBOR_TEST)
    set(ALBOR_RELEASE ON)
else()
    set(ALBOR_RELEASE OFF)
endif()

if(ALBOR_AUTOMATION AND NOT (ALBOR_DEV OR ALBOR_TEST))
    message(FATAL_ERROR "the automation core is excluded from Release at compile/link time (AD-10)")
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)

# Phase 0 measurement code (§30): built as standalone targets, never linked into a runtime module
if(ALBOR_PROTOTYPES)
    add_subdirectory(prototypes/d)
    add_subdirectory(prototypes/a2)
endif()

enable_testing()

add_subdirectory(tests)
