# the source route: fetch a pinned commit from GitHub and consume the framework and its .ui helper
cmake_minimum_required(VERSION 3.28)

if(APPLE)
    if(NOT DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
        set(ENV{MACOSX_DEPLOYMENT_TARGET} "13.3")
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "" FORCE)
endif()

project(albor-fetch-consumer LANGUAGES CXX)

if(NOT DEFINED ALBOR_FETCH_REPOSITORY OR ALBOR_FETCH_REPOSITORY STREQUAL "")
    message(FATAL_ERROR "ALBOR_FETCH_REPOSITORY must name the repository to fetch")
endif()
if(NOT DEFINED ALBOR_FETCH_TAG OR ALBOR_FETCH_TAG STREQUAL "")
    message(FATAL_ERROR "ALBOR_FETCH_TAG must name a commit or a stable tag, never a moving branch")
endif()

# the fetched copy builds the framework alone; examples and tests belong to the framework checkout
set(ALBOR_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALBOR_TESTS OFF CACHE BOOL "" FORCE)
set(ALBOR_RENDERER_BLEND2D OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(albor
    GIT_REPOSITORY "${ALBOR_FETCH_REPOSITORY}"
    GIT_TAG "${ALBOR_FETCH_TAG}")
FetchContent_MakeAvailable(albor)

add_executable(albor-fetch-consumer main.cpp)
albor_build_ui_component(albor-fetch-consumer "${CMAKE_CURRENT_SOURCE_DIR}/fetch_panel.ui")
target_link_libraries(albor-fetch-consumer PRIVATE albor)
target_compile_features(albor-fetch-consumer PRIVATE cxx_std_20)
