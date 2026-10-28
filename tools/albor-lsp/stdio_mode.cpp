#include "tools/albor-lsp/stdio_mode.h"

namespace albor::tools
{
    bool set_binary_stdio() {
        // the streams are already byte streams on this host, so the json-rpc framing needs no translation (§12.2)
        return true;
    }
} // namespace albor::tools
