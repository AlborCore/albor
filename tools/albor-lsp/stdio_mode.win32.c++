#include "tools/albor-lsp/stdio_mode.h"

#include <cstdio>
#include <fcntl.h>
#include <io.h>

namespace albor::tools
{
    bool set_binary_stdio() {
        // the input stream carries the frame header and body as bytes, so text translation must be disabled (§12.2)
        if (_setmode(_fileno(stdin), _O_BINARY) == -1)
            return false;
        // the output stream carries the same byte framing, so it is switched to binary as well (§12.2)
        if (_setmode(_fileno(stdout), _O_BINARY) == -1)
            return false;
        // both streams carry raw bytes, which is what the content-length framing needs (§12.2)
        return true;
    }
} // namespace albor::tools
