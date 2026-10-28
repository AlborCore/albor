# the stdio language server an editor launches, which frames json-rpc over its process pipes (§12.2)

# the launcher is the transport around the server library, so it owns only the process entry point (§12.2)
set(ALBOR_LSP_SOURCES main.cpp)

# the windows console translates newlines by default, which would corrupt a content-length body (§12.2)
if(WIN32)
    list(APPEND ALBOR_LSP_SOURCES stdio_mode.win32.c++)
else()
    list(APPEND ALBOR_LSP_SOURCES stdio_mode.cpp)
endif()

# create the executable the editor names as its server command
add_executable(albor-lsp ${ALBOR_LSP_SOURCES})

# the launcher reaches the project root, which is where the module includes point (§3.2)
target_include_directories(albor-lsp PRIVATE ${ALBOR_SOURCE_DIR})
target_compile_features(albor-lsp PRIVATE cxx_std_23)

# the server library carries the compiler and core, because the editor sees the compiler's diagnostics (§12.2)
target_link_libraries(albor-lsp PRIVATE albor-language-server)
