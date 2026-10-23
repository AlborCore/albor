#include <string_view>
#include <vector>

#include "manifest/font_embed.h"

int main(int argc, char** argv) {
    // the driver takes the flags after the program name, which is what the runner receives (§14.2)
    std::vector<std::string_view> arguments;
    // every argument after the program name joins the list, in the order the build wrote it (§14.2)
    for (int index = 1; index < argc; index++)
        arguments.emplace_back(argv[index]);
    // the manifest library runs the driver, so the entry point stays a thin wrapper (§14.2)
    return albor::manifest::run_font_embed(arguments);
}
