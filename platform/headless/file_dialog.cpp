#include "platform/headless/file_dialog.h"

#include <deque>
#include <utility>

#include "application/file_dialog.h"

namespace albor::headless
{
    namespace
    {
        // the state the scripted backend carries, which a test drives through the free functions (§5.3)
        struct FileDialogState
        {
            // the answers the next panels return, in call order (§5.3)
            std::deque<std::optional<std::filesystem::path>> answers;

            // how many panels were shown, which a test asserts on (§5.3)
            std::size_t shown = 0;

            // the title of the last panel (§5.3)
            std::string title;

            // the initial directory of the last panel, as utf8 (§5.3)
            std::string directory;

            // the default name of the last save panel, as utf8 (§5.3)
            std::string default_name;

            // the filters of the last panel, each spelled as "name:ext1,ext2" (§5.3)
            std::vector<std::string> filters;

            // whether the last panel was a save panel (§5.3)
            bool was_save = false;

            // whether the last panel named a parent window (§5.1)
            bool had_parent = false;
        };

        // the one state a headless process carries, because a panel is a process wide service (§5.3)
        FileDialogState& state() {
            // the first caller creates the state, and every later one reads the same object (§5.3)
            static FileDialogState shared;
            return shared;
        }

        // take the next scripted answer, where an empty queue is a cancel (§5.3)
        std::optional<std::filesystem::path> take_answer() {
            // an empty queue means the test did not script an answer, which cancels (§5.3)
            if (state().answers.empty())
                return std::nullopt;
            // the answer leaves the queue, because one panel consumes one answer (§5.3)
            std::optional<std::filesystem::path> answer = std::move(state().answers.front());
            state().answers.pop_front();
            return answer;
        }

        // the backend that answers from the scripted queue rather than from a screen (§5.3)
        class HeadlessFileDialog final : public PlatformFileDialog
        {
        public:
            std::optional<std::filesystem::path> open(const OpenFileOptions& options, NativeWindowHandle parent) override {
                // the call is recorded, which is what a test asserts on (§5.3)
                record(options.title, options.initial_directory, std::string{}, options.filters, false, parent);
                // the scripted answer is returned, which is a path or a cancel (§5.3)
                return take_answer();
            }

            std::optional<std::filesystem::path> save(const SaveFileOptions& options, NativeWindowHandle parent) override {
                // the call is recorded with the default name, which a save panel proposes (§5.3)
                record(options.title, options.initial_directory, options.default_name, options.filters, true, parent);
                // the scripted answer is returned, which is a path or a cancel (§5.3)
                return take_answer();
            }

        private:
            // record one panel call, which is what a test reads back (§5.3)
            static void record(std::string title, std::string directory, std::string default_name, const std::vector<FileFilter>& filters, bool save, NativeWindowHandle parent) {
                // the count is what proves the panel was shown (§5.3)
                state().shown++;
                // the title is copied, because the options die with the call (§5.3)
                state().title = std::move(title);
                // the directory is copied as the utf8 the caller wrote (§5.3)
                state().directory = std::move(directory);
                // the default name is copied, which only a save panel carries (§5.3)
                state().default_name = std::move(default_name);
                // the filters are spelled into one string each, which is what a test compares (§5.3)
                state().filters.clear();
                for (const FileFilter& filter : filters) {
                    // the label starts the spelling, because it is part of what the caller wrote (§5.3)
                    std::string spelled = filter.name + ":";
                    for (std::size_t index = 0; index < filter.extensions.size(); index++) {
                        // a later extension follows the comma, which keeps the spelling readable (§5.3)
                        if (index > 0)
                            spelled += ',';
                        // the extension is the bare spelling the caller wrote (§5.3)
                        spelled += filter.extensions[index];
                    }
                    // the spelled filter joins the record (§5.3)
                    state().filters.push_back(std::move(spelled));
                }
                // the direction is recorded, because a test distinguishes an open from a save (§5.3)
                state().was_save = save;
                // the parent flag records whether the panel was owned (§5.1)
                state().had_parent = parent.valid();
            }
        };
    } // namespace

    void install_headless_file_dialog() {
        // the factory builds the scripted backend, which the application calls like any other (§5.3)
        set_platform_file_dialog_factory([] { return std::unique_ptr<PlatformFileDialog>(new HeadlessFileDialog()); });
    }

    void push_file_dialog_answer(std::optional<std::filesystem::path> answer) {
        // the answer is queued, because a test may script more than one panel (§5.3)
        state().answers.push_back(std::move(answer));
    }

    void reset_file_dialogs() {
        // the counters and the recorded call are cleared, so a test reads only its own panel (§5.3)
        state() = FileDialogState{};
    }

    std::size_t file_dialog_count() {
        // the count is how many panels the process showed (§5.3)
        return state().shown;
    }

    const std::string& last_file_dialog_title() {
        // the title is the last panel's, which a test asserted on (§5.3)
        return state().title;
    }

    const std::string& last_file_dialog_directory() {
        // the directory is the last panel's, as utf8 (§5.3)
        return state().directory;
    }

    const std::string& last_file_dialog_default_name() {
        // the default name is the last save panel's, as utf8 (§5.3)
        return state().default_name;
    }

    const std::vector<std::string>& last_file_dialog_filters() {
        // the filters are the last panel's, as the caller wrote them (§5.3)
        return state().filters;
    }

    bool last_file_dialog_was_save() {
        // the direction is the last panel's, which a test asserts on (§5.3)
        return state().was_save;
    }

    bool last_file_dialog_had_parent() {
        // the flag records whether the last panel was owned by a window (§5.1)
        return state().had_parent;
    }
} // namespace albor::headless
