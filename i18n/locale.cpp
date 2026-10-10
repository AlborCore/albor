#include "i18n/locale.h"

#include <algorithm>
#include <mutex>

namespace albor::i18n
{
    namespace
    {
        // the locale in use, which startup and a language selector both replace (§9.2)
        std::string g_locale = "en";

        // the listeners that must re-evaluate, which the binding engine registers (§9.2, §7.3)
        std::vector<LocaleListener> g_listeners;

        // the guard around both, because a locale change reaches a reader on another thread (§9.2, §10.5)
        std::mutex g_mutex;
    } // namespace

    std::string locale() {
        // the guard is taken, because a reader may be on another thread (§9.2, §10.5)
        const std::lock_guard<std::mutex> guard(g_mutex);
        // the locale is returned by value, because a caller must not hold a reference across a change (§9.2)
        return g_locale;
    }

    void set_locale(std::string next) {
        // the listeners are copied under the guard, because a listener may register another (§9.2)
        std::vector<LocaleListener> listeners;
        {
            // the guard is taken, because a change is visible process wide (§9.2, §10.5)
            const std::lock_guard<std::mutex> guard(g_mutex);
            // an unchanged locale changes nothing, because re-evaluating every binding would be wasted work (§7.3)
            if (g_locale == next)
                return;
            // the locale is stored, which is what every later lookup reads (§9.2)
            g_locale = std::move(next);
            // the listeners are copied, because calling one under the guard could deadlock (§9.2)
            listeners = g_listeners;
        }
        // each listener is called outside the guard, because one of them settles the binding engine (§9.2, §7.3)
        for (const LocaleListener listener : listeners) {
            // a listener that is unset is skipped, because a registration may have been cleared (§9.2)
            if (listener == nullptr)
                continue;
            // the listener re-evaluates whatever depends on the locale (§9.2, §7.3)
            listener();
        }
    }

    TextDirection direction() {
        // the current locale decides, because direction is a property of the language (§9.2)
        return right_to_left(locale()) ? TextDirection::RightToLeft : TextDirection::LeftToRight;
    }

    bool right_to_left(std::string_view locale) {
        // the language is the part before a region or a script, because a locale is written that way (§9.2)
        const std::size_t dash = locale.find_first_of("-_");
        // the language is what precedes the dash, or the whole tag when there is none (§9.2)
        const std::string_view language = dash == std::string_view::npos ? locale : locale.substr(0, dash);
        // the languages that read right to left are named here, because the set is small and fixed (§9.2)
        static constexpr std::string_view RIGHT_TO_LEFT[] = {"ar", "he", "fa", "ur", "yi", "dv", "ps", "sd", "ug", "ckb"};
        // any of those languages writes right to left (§9.2)
        return std::any_of(std::begin(RIGHT_TO_LEFT), std::end(RIGHT_TO_LEFT), [language](std::string_view candidate) { return language == candidate; });
    }

    void on_locale_changed(LocaleListener listener) {
        // the guard is taken, because registration happens at startup beside no other work (§9.2, §10.5)
        const std::lock_guard<std::mutex> guard(g_mutex);
        // an unset listener is refused, because calling it would fault (§9.2)
        if (listener == nullptr)
            return;
        // the listener joins the set, which a locale change will call (§9.2, §7.3)
        g_listeners.push_back(listener);
    }

    std::size_t listener_count() {
        // the guard is taken, because the count is read beside a possible registration (§9.2, §10.5)
        const std::lock_guard<std::mutex> guard(g_mutex);
        // the count is returned whole (§9.2)
        return g_listeners.size();
    }
} // namespace albor::i18n
