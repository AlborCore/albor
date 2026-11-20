#include "albor/core/env.h"

#include <cstdlib>

namespace albor::core
{
    std::optional<std::string> get_environment_variable(std::string_view name) {
        // the name is copied once because both entry points take a terminated string
        const std::string key(name);
#if defined(_WIN32)
        // the secure entry point is used because the crt reports getenv as unsafe on windows
        char* value = nullptr;
        // the size is not needed because the value is copied before the buffer is released
        if (_dupenv_s(&value, nullptr, key.c_str()) != 0) {
            // a failed call is reported as no value, releasing any buffer it still wrote
            std::free(value);
            // exit early because the call failed
            return std::nullopt;
        }
        // an absent variable writes a null buffer, which is reported as no value
        if (value == nullptr)
            return std::nullopt;
        // the value is copied before the crt buffer is released, because the caller owns the result
        std::string result(value);
        // the crt buffer is released because the secure entry point allocated it
        std::free(value);
        // exit
        return result;
#else
        // getenv is the portable entry point and carries no deprecation outside the windows crt
        const char* value = std::getenv(key.c_str());
        // an absent variable yields no pointer, which is reported as no value
        if (value == nullptr)
            return std::nullopt;
        // the value is copied because the caller must own it past any later environment change
        return std::string(value);
#endif
    }
} // namespace albor::core
