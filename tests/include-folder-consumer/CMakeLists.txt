# the include folder consumer, which locks the albor/ header resolution beside a consumer core root (#152, §3.5, AD-19)

# the umbrella exists only where a desktop adapter builds, which is where the fixture can run (§3.3)
if(NOT TARGET albor)
    message(STATUS "albor-include-folder-consumer: skipped, no desktop umbrella")
    return()
endif()

add_executable(albor-include-folder-consumer main.cpp)

# the consumer root joins the compile before the framework's, which is the include order the issue describes (#152)
target_include_directories(albor-include-folder-consumer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(albor-include-folder-consumer PRIVATE albor)
target_compile_features(albor-include-folder-consumer PRIVATE cxx_std_23)
