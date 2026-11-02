#include "albor/application/file_dialog.h"

#include "albor/platform/windows/file_dialog.win32.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <wrl/client.h>
// clang-format on

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace albor::windows
{
    namespace
    {
        // the apartment the shell dialogs need, which the ui thread owns for its life (§5.3)
        struct Apartment
        {
            // the result decides whether this thread may host a shell dialog (§5.3)
            HRESULT result = E_FAIL;

            Apartment() {
                // a single threaded apartment is what a ui thread needs (§5.3)
                result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            }

            ~Apartment() {
                // an apartment this thread initialized is released with it (§5.3)
                if (SUCCEEDED(result))
                    CoUninitialize();
            }
        };

        // report whether this thread can host a shell dialog, refusing a thread in another apartment (§5.3)
        bool ensure_apartment() {
            // the guard is per thread, because COM apartments are per thread (§5.3)
            static thread_local Apartment apartment;
            // a success or an already initialized apartment is usable (§5.3)
            return SUCCEEDED(apartment.result);
        }

        // widen the utf8 the framework carries into the utf16 the shell takes (§5.3)
        std::wstring widen(std::string_view text) {
            // an empty text widens to nothing, because there is nothing to convert (§5.3)
            if (text.empty())
                return {};
            // the length is measured in units, because the api answers the measure before the copy (§5.3)
            const int units = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            // a text that does not convert stays empty rather than dangling (§5.3)
            if (units <= 0)
                return {};
            // the buffer is sized before the conversion writes into it (§5.3)
            std::wstring result(static_cast<std::size_t>(units), L'\0');
            // the conversion fills the buffer the caller sized (§5.3)
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), units);
            return result;
        }

        // the configuration the last seam applied, which the platform suite reads back (§5.3)
        FileDialogConfiguration g_last_configuration;

        // the filter storage a dialog reads while it is configured and shown (§5.3)
        struct FilterStorage
        {
            // the labels, which the specs point into (§5.3)
            std::vector<std::wstring> names;

            // the patterns, which the specs point into (§5.3)
            std::vector<std::wstring> patterns;

            // the specs the dialog receives, which point at the two vectors above (§5.3)
            std::vector<COMDLG_FILTERSPEC> specs;
        };

        // build the filter specs, which the caller keeps alive until the dialog is shown (§5.3)
        FilterStorage build_filters(const std::vector<FileFilter>& filters) {
            // the storage grows before any spec points into it, because a reallocation would dangle the pointers (§5.3)
            FilterStorage storage;
            // every filter contributes one row, which is the shell's shape (§5.3)
            for (const FileFilter& filter : filters) {
                // the label is what the user reads, which falls back to the extensions when unnamed (§5.3)
                std::wstring label = widen(filter.name);
                // the pattern joins every extension with a semicolon, which the shell accepts (§5.3)
                std::wstring pattern;
                // every extension of the filter is one alternative (§5.3)
                for (const std::string& extension : filter.extensions) {
                    // a later extension follows the separator, which the shell spells with a semicolon (§5.3)
                    if (!pattern.empty())
                        pattern += L';';
                    // the wildcard pattern is what the shell matches a file name against (§5.3)
                    pattern += L"*.";
                    // the extension itself, which the language wrote without a dot (§5.3)
                    pattern += widen(extension);
                }
                // an empty pattern means any file, which a filter without extensions names (§5.3)
                if (pattern.empty())
                    pattern = L"*.*";
                // an unnamed filter reads as its pattern, which is still useful to the user (§5.3)
                if (label.empty())
                    label = pattern;
                // the label is stored before the spec points at it (§5.3)
                storage.names.push_back(std::move(label));
                // the pattern is stored before the spec points at it (§5.3)
                storage.patterns.push_back(std::move(pattern));
            }
            // the vectors are sized first, so a later push cannot move the storage the specs point into (§5.3)
            storage.specs.reserve(storage.names.size());
            // every stored filter becomes one spec (§5.3)
            for (std::size_t index = 0; index < storage.names.size(); index++)
                storage.specs.push_back(COMDLG_FILTERSPEC{storage.names[index].c_str(), storage.patterns[index].c_str()});
            return storage;
        }

        // apply the common options to a dialog, which is what both directions share (§5.3)
        void configure_into(IFileDialog* dialog, const std::string& title, const FilterStorage& storage, const std::string& directory) {
            // the seam records what it applies, because the shell getters are not available on every sdk (§5.3)
            g_last_configuration.title = title;
            g_last_configuration.directory = directory;
            g_last_configuration.filters.clear();
            // every filter is spelled as the shell pattern it becomes (§5.3)
            for (std::size_t index = 0; index < storage.names.size(); index++) {
                // the narrow spelling is what a test compares, which keeps the api utf8 (§5.3)
                const std::string name(storage.names[index].begin(), storage.names[index].end());
                const std::string pattern(storage.patterns[index].begin(), storage.patterns[index].end());
                g_last_configuration.filters.push_back(name + ":" + pattern);
            }
            // an empty title keeps the system default, which a dialog names itself (§5.3)
            if (!title.empty()) {
                // the utf8 title becomes the utf16 the shell shows (§5.3)
                const std::wstring wide = widen(title);
                // a title that does not convert keeps the default (§5.3)
                if (!wide.empty())
                    dialog->SetTitle(wide.c_str());
            }
            // an empty filter list leaves the dialog open to any file (§5.3)
            if (!storage.specs.empty())
                dialog->SetFileTypes(static_cast<UINT>(storage.specs.size()), storage.specs.data());
            // an empty directory keeps the system default, which is where the user last was (§5.3)
            if (!directory.empty()) {
                // the utf8 directory becomes the parsing name the shell reads (§5.3)
                const std::wstring wide = widen(directory);
                // the item is what the dialog accepts as its starting folder (§5.3)
                Microsoft::WRL::ComPtr<IShellItem> folder;
                // a directory that does not resolve leaves the default in place (§5.3)
                if (!wide.empty() && SUCCEEDED(SHCreateItemFromParsingName(wide.c_str(), nullptr, IID_PPV_ARGS(&folder))))
                    dialog->SetFolder(folder.Get());
            }
        }

        // apply the proposed name a save dialog starts from (§5.3)
        void set_default_name(IFileDialog* dialog, const std::string& name) {
            // the seam records the proposed name, which is what the platform suite asserts (§5.3)
            g_last_configuration.default_name = name;
            // an empty name keeps the system default, which the shell invents (§5.3)
            if (name.empty())
                return;
            // the utf8 name becomes the utf16 the shell shows (§5.3)
            const std::wstring wide = widen(name);
            // a name that does not convert keeps the default (§5.3)
            if (!wide.empty())
                dialog->SetFileName(wide.c_str());
        }

        // run one configured dialog and hand out the chosen path, where nullopt means a cancel (§5.3)
        std::optional<std::filesystem::path> run(IFileDialog* dialog, bool save, HWND owner) {
            // the current options are read first, because the dialog keeps defaults the caller must not lose (§5.3)
            FILEOPENDIALOGOPTIONS options = 0;
            // a dialog that reports no options still runs, so the failure is not fatal (§5.3)
            if (SUCCEEDED(dialog->GetOptions(&options)))
                options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
            // an open dialog chooses an existing file, which is what the caller asked for (§5.3)
            if (!save)
                options |= FOS_FILEMUSTEXIST;
            // a save dialog confirms an overwrite, which is the shell's own default (§5.3)
            if (save)
                options |= FOS_OVERWRITEPROMPT;
            // the options are applied before the dialog runs (§5.3)
            dialog->SetOptions(options);
            // the dialog runs and reports a cancel as its own failure code (§5.3)
            const HRESULT shown = dialog->Show(owner);
            // a user cancel is the quiet path, which the caller reads as a cancel (§5.3)
            if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED))
                return std::nullopt;
            // any other failure is reported, because it is not a user decision (§5.3)
            if (FAILED(shown)) {
                std::fprintf(stderr, "albor: the file dialog could not be shown (0x%08lx)\n", static_cast<unsigned long>(shown));
                return std::nullopt;
            }
            // the chosen item is what the dialog answered (§5.3)
            Microsoft::WRL::ComPtr<IShellItem> item;
            // a dialog that answers no item is a failure rather than a cancel (§5.3)
            if (FAILED(dialog->GetResult(&item)) || item == nullptr) {
                std::fprintf(stderr, "albor: the file dialog answered no item\n");
                return std::nullopt;
            }
            // the display name is the file system path the caller opens (§5.3)
            PWSTR chosen = nullptr;
            // a name that is not a file system path is a failure rather than a cancel (§5.3)
            if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &chosen)) || chosen == nullptr) {
                std::fprintf(stderr, "albor: the chosen file has no file system path\n");
                return std::nullopt;
            }
            // the path is copied before the shell buffer is released (§5.3)
            const std::filesystem::path result(chosen);
            // the shell owns the name, so it is freed here (§5.3)
            CoTaskMemFree(chosen);
            return result;
        }

        // the win32 backend, which runs a native shell dialog for both directions (§5.3)
        class Win32FileDialog final : public PlatformFileDialog
        {
        public:
            std::optional<std::filesystem::path> open(const OpenFileOptions& options, NativeWindowHandle parent) override {
                // a thread in another apartment cannot host a shell dialog, which is reported (§5.3)
                if (!ensure_apartment()) {
                    std::fprintf(stderr, "albor: the file dialog needs a single threaded apartment\n");
                    return std::nullopt;
                }
                // the open dialog is the modern shell chooser (§5.3)
                Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
                // a failed creation is reported, because no panel exists to run (§5.3)
                if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
                    std::fprintf(stderr, "albor: the open file dialog could not be created\n");
                    return std::nullopt;
                }
                // the filter storage lives until the dialog returns, because the specs point into it (§5.3)
                const FilterStorage storage = build_filters(options.filters);
                // the request is applied before the dialog runs, which the test seam also asserts on (§5.3)
                configure_into(dialog.Get(), options.title, storage, options.initial_directory);
                // the parent lends the window the dialog is owned by (§5.1)
                HWND owner = parent.valid() ? static_cast<HWND>(const_cast<void*>(parent.object())) : nullptr;
                // the dialog runs and reports the chosen path or a cancel (§5.3)
                return run(dialog.Get(), false, owner);
            }

            std::optional<std::filesystem::path> save(const SaveFileOptions& options, NativeWindowHandle parent) override {
                // a thread in another apartment cannot host a shell dialog, which is reported (§5.3)
                if (!ensure_apartment()) {
                    std::fprintf(stderr, "albor: the file dialog needs a single threaded apartment\n");
                    return std::nullopt;
                }
                // the save dialog is the modern shell chooser (§5.3)
                Microsoft::WRL::ComPtr<IFileSaveDialog> dialog;
                // a failed creation is reported, because no panel exists to run (§5.3)
                if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
                    std::fprintf(stderr, "albor: the save file dialog could not be created\n");
                    return std::nullopt;
                }
                // the filter storage lives until the dialog returns, because the specs point into it (§5.3)
                const FilterStorage storage = build_filters(options.filters);
                // the request is applied before the dialog runs, which the test seam also asserts on (§5.3)
                configure_into(dialog.Get(), options.title, storage, options.initial_directory);
                // the proposed name is what the user starts from, which a save dialog edits (§5.3)
                set_default_name(dialog.Get(), options.default_name);
                // the parent lends the window the dialog is owned by (§5.1)
                HWND owner = parent.valid() ? static_cast<HWND>(const_cast<void*>(parent.object())) : nullptr;
                // the dialog runs and reports the chosen path or a cancel (§5.3)
                return run(dialog.Get(), true, owner);
            }
        };
    } // namespace

    void configure_open_dialog(IFileDialog* dialog, const OpenFileOptions& options) {
        // the test seam keeps the storage for the call, because the shell copies the types it reads (§5.3)
        const FilterStorage storage = build_filters(options.filters);
        // the common options are applied before the dialog is shown (§5.3)
        configure_into(dialog, options.title, storage, options.initial_directory);
    }

    void configure_save_dialog(IFileDialog* dialog, const SaveFileOptions& options) {
        // the test seam keeps the storage for the call, because the shell copies the types it reads (§5.3)
        const FilterStorage storage = build_filters(options.filters);
        // the common options are applied before the dialog is shown (§5.3)
        configure_into(dialog, options.title, storage, options.initial_directory);
        // the proposed name is what the user starts from, which a save dialog edits (§5.3)
        set_default_name(dialog, options.default_name);
    }

    const FileDialogConfiguration& last_file_dialog_configuration() {
        // the configuration is the last seam call's, which a test compares (§5.3)
        return g_last_configuration;
    }

    void install_windows_file_dialog() {
        // the factory builds the win32 backend, which the application calls like any other (§5.3, AD-16)
        set_platform_file_dialog_factory([] { return std::unique_ptr<PlatformFileDialog>(new Win32FileDialog()); });
    }
} // namespace albor::windows
