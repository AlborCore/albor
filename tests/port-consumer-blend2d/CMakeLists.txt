cmake_minimum_required(VERSION 3.28)

# the deployment floor is the consumer's to declare before the first configure, which the overlay triplets also assert (§6.3)
if(NOT DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
    set(ENV{MACOSX_DEPLOYMENT_TARGET} "13.3")
endif()

# the blend2d feature's consumer, which exercises the content of the CPU renderer path (AD-26)
project(albor-port-consumer-blend2d LANGUAGES CXX)

# the umbrella is the only target the consumer names, which is the same contract an application uses (§3.3)
find_package(albor CONFIG REQUIRED)

add_executable(albor-port-consumer-blend2d "${CMAKE_CURRENT_SOURCE_DIR}/../port-consumer/main.cpp")

# the helper drives the installed codegen executable, which is what the tool prefix contract exists for (AD-19)
albor_build_ui_component(albor-port-consumer-blend2d "${CMAKE_CURRENT_SOURCE_DIR}/../port-consumer/consumer_panel.ui")

target_link_libraries(albor-port-consumer-blend2d PRIVATE albor)
