#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace albor::headless
{
    // install the scripted backend, which the headless platform calls at load (§5.3, AD-16)
    void install_headless_file_dialog();

    // queue the answer the next panel returns, where nullopt cancels (§5.3)
    void push_file_dialog_answer(std::optional<std::filesystem::path> answer);

    // forget every queued answer and recorded call, which a test does before it starts (§5.3)
    void reset_file_dialogs();

    // read how many panels were shown, which a test asserts on (§5.3)
    [[nodiscard]] std::size_t file_dialog_count();

    // read the title of the last panel, which a test asserts on (§5.3)
    [[nodiscard]] const std::string& last_file_dialog_title();

    // read the initial directory of the last panel, as utf8 (§5.3)
    [[nodiscard]] const std::string& last_file_dialog_directory();

    // read the default name of the last save panel, as utf8 (§5.3)
    [[nodiscard]] const std::string& last_file_dialog_default_name();

    // read the filters of the last panel, each spelled as "name:ext1,ext2" (§5.3)
    [[nodiscard]] const std::vector<std::string>& last_file_dialog_filters();

    // report whether the last panel was a save panel, which a test asserts on (§5.3)
    [[nodiscard]] bool last_file_dialog_was_save();

    // report whether the last panel named a parent, which a test asserts on (§5.1)
    [[nodiscard]] bool last_file_dialog_had_parent();
} // namespace albor::headless
