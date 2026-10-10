#pragma once

#include <string>
#include <vector>

#include "i18n/catalog.h"

namespace albor::i18n
{
    // the reader's locale, process wide so two components never disagree about the language (§9.2)
    [[nodiscard]] std::string locale();

    // replace the reader's locale, which is what a language selector calls (§9.2)
    void set_locale(std::string locale);

    // the reading direction the current locale writes in, which is what layout mirrors (§9.2)
    [[nodiscard]] TextDirection direction();

    // report whether a locale reads right to left, which is the only question layout asks (§9.2)
    [[nodiscard]] bool right_to_left(std::string_view locale);

    // a listener re-evaluates through the ordinary invalidation path rather than being told what changed (§7.3)
    using LocaleListener = void (*)();

    // register a listener, which the binding engine calls once at startup (§9.2, §7.3)
    void on_locale_changed(LocaleListener listener);

    // how many listeners are registered, which a test asserts on (§9.2)
    [[nodiscard]] std::size_t listener_count();
} // namespace albor::i18n
