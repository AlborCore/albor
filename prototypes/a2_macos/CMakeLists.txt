# Prototype A2 macOS pacing triage (§30, AD-18b)
# measurement code, standalone: no runtime module links it

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

    add_executable(albor-prototype-a2-pacing pacing_triage.macos.mm)
    target_link_libraries(albor-prototype-a2-pacing PRIVATE
        "-framework AppKit"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework CoreVideo"
    )
endif()
