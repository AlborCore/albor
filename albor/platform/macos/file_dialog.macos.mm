#include "albor/application/file_dialog.h"

#include "albor/platform/macos/file_dialog.macos.h"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace albor::macos
{
    namespace
    {
        // whether a panel is already running on this thread, which appkit allows once per run loop (§5.3)
        thread_local bool g_panel_running = false;

        // the guard that refuses a nested panel and clears the flag when the panel returns (§5.3)
        struct PanelGuard
        {
            // the flag owns the state, because appkit runs one panel at a time per thread (§5.3)
            explicit PanelGuard(bool& running) :
                m_running(running) {
                m_running = true;
            }

            ~PanelGuard() { m_running = false; }

            PanelGuard(const PanelGuard&) = delete;
            PanelGuard& operator=(const PanelGuard&) = delete;

        private:
            // the flag this guard set and clears (§5.3)
            bool& m_running;
        };

        // run a panel as a sheet when it has a visible owner, or as its own modal session otherwise (§4.3, §5.3)
        std::optional<std::filesystem::path> run_panel(NSSavePanel* panel, NSWindow* owner) {
            // the result is written by the completion handler, which appkit runs when the panel closes (§5.3)
            __block std::optional<std::filesystem::path> chosen;
            // the flag is what the nested loop waits on, because a sheet never blocks its caller by itself (§4.3)
            __block bool finished = NO;
            // the completion is the one place a panel answer is read, whichever session ran it (§5.3)
            void (^completion)(NSModalResponse) = ^(NSModalResponse response) {
              // only an accepted panel names a file, because a cancel names none (§5.3)
              if (response == NSModalResponseOK) {
                  // the url is the file the user chose, which the panel owns (§5.3)
                  NSURL* url = panel.URL;
                  // a null url is a cancel in disguise, which stays empty (§5.3)
                  if (url != nil)
                      chosen = std::filesystem::path(url.fileSystemRepresentation);
              }
              // a response that is neither accepted nor cancelled is a failure worth reporting (§5.3)
              else if (response != NSModalResponseCancel) {
                  // the reason is printed, because the caller reads nullopt as a cancel (§5.3)
                  std::fprintf(stderr, "albor: the file panel ended with an unexpected response %ld\n", static_cast<long>(response));
              }
              // the loop below stops once the panel answered (§4.3)
              finished = YES;
            };
            // an owned panel is a sheet, which keeps it above its window and modal to it (§5.1)
            if (owner != nil && owner.isVisible) {
                // the sheet starts and reports through the completion handler (§5.3)
                [panel beginSheetModalForWindow:owner completionHandler:completion];
                // a sheet appkit refused is never attached, so the wait below would never end (§5.3)
                if (owner.attachedSheet != panel) {
                    // the refusal is reported, because the caller reads nullopt as a cancel (§5.3)
                    std::fprintf(stderr, "albor: the file panel could not attach as a sheet\n");
                    return std::nullopt;
                }
                // the sheet needs the modal panel mode, which the application loop never services (§4.3)
                while (!finished) {
                    @autoreleasepool {
                        // one event is pulled in the modal panel mode, which is where a sheet lives (§4.3)
                        NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01] inMode:NSModalPanelRunLoopMode dequeue:YES];
                        // an event that arrived is handed to appkit, which is the native path (§4.3)
                        if (event != nil)
                            [NSApp sendEvent:event];
                    }
                }
            }
            // an unowned or hidden window runs a plain modal session, which appkit pumps for itself (§4.3)
            else {
                // the response is the same value the sheet reports through its handler (§5.3)
                const NSModalResponse response = [panel runModal];
                // the completion reads the answer, which keeps one decoding path (§5.3)
                completion(response);
            }
            return chosen;
        }

        // the content types a filter list names, which a panel accepts (§5.3)
        NSArray<UTType*>* types_for(const std::vector<FileFilter>& filters) {
            // the set is built once, because the panel takes one array for every filter (§5.3)
            NSMutableArray<UTType*>* types = [NSMutableArray array];
            // every extension of every filter contributes its type (§5.3)
            for (const FileFilter& filter : filters) {
                for (const std::string& extension : filter.extensions) {
                    // the extension is what the language wrote, without a dot (§5.3)
                    NSString* spelling = [NSString stringWithUTF8String:extension.c_str()];
                    // an empty extension names no type, because there is nothing to resolve (§5.3)
                    UTType* type = spelling.length > 0 ? [UTType typeWithFilenameExtension:spelling] : nil;
                    // a duplicate type is added once, because the panel reads a set (§5.3)
                    if (type != nil && ![types containsObject:type])
                        [types addObject:type];
                }
            }
            return types;
        }

        // apply the common options to a panel, which is what both directions share (§5.3)
        void configure(NSSavePanel* panel, const std::string& title, const std::vector<FileFilter>& filters, const std::string& directory) {
            // an empty title keeps the system default, which a panel names itself (§5.3)
            if (!title.empty())
                panel.title = [NSString stringWithUTF8String:title.c_str()];
            // a written filter restricts the panel to the types it names (§5.3)
            NSArray<UTType*>* types = types_for(filters);
            // a panel with no filter offers any file, which is what an empty list means (§5.3)
            if (types.count > 0)
                panel.allowedContentTypes = types;
            // an empty directory keeps the system default, which is where the user last was (§5.3)
            if (!directory.empty()) {
                // the utf8 directory becomes the url the panel opens at (§5.3)
                NSString* path = [NSString stringWithUTF8String:directory.c_str()];
                // a path that does not resolve leaves the default in place (§5.3)
                if (path.length > 0)
                    panel.directoryURL = [NSURL fileURLWithPath:path isDirectory:YES];
            }
        }

        // the appkit backend, which runs a native panel for both directions (§5.3)
        class MacosFileDialog final : public PlatformFileDialog
        {
        public:
            std::optional<std::filesystem::path> open(const OpenFileOptions& options, NativeWindowHandle parent) override {
                // a nested panel is refused, because appkit allows one sheet per window (§5.3)
                if (g_panel_running) {
                    // the refusal is reported, because the caller reads nullopt as a cancel (§5.3)
                    std::fprintf(stderr, "albor: a file panel is already open\n");
                    return std::nullopt;
                }
                // the guard clears the flag when this call returns, whichever path it takes (§5.3)
                const PanelGuard guard(g_panel_running);
                // the open panel is the native chooser, which appkit owns while it runs (§5.3)
                NSOpenPanel* panel = [NSOpenPanel openPanel];
                // the request is applied before the panel runs, which the test seam also asserts on (§5.3)
                configure_open_panel(panel, options);
                // the parent lends the window the sheet attaches to, which is the ownership relation (§5.1)
                NSWindow* owner = parent.valid() ? (__bridge NSWindow*)parent.object() : nil;
                // the panel runs and reports the chosen path or a cancel (§5.3)
                return run_panel(panel, owner);
            }

            std::optional<std::filesystem::path> save(const SaveFileOptions& options, NativeWindowHandle parent) override {
                // a nested panel is refused, because appkit allows one sheet per window (§5.3)
                if (g_panel_running) {
                    // the refusal is reported, because the caller reads nullopt as a cancel (§5.3)
                    std::fprintf(stderr, "albor: a file panel is already open\n");
                    return std::nullopt;
                }
                // the guard clears the flag when this call returns, whichever path it takes (§5.3)
                const PanelGuard guard(g_panel_running);
                // the save panel is the native chooser, which appkit owns while it runs (§5.3)
                NSSavePanel* panel = [NSSavePanel savePanel];
                // the request is applied before the panel runs, which the test seam also asserts on (§5.3)
                configure_save_panel(panel, options);
                // the parent lends the window the sheet attaches to, which is the ownership relation (§5.1)
                NSWindow* owner = parent.valid() ? (__bridge NSWindow*)parent.object() : nil;
                // the panel runs and reports the chosen path or a cancel (§5.3)
                return run_panel(panel, owner);
            }
        };
    } // namespace

    void configure_open_panel(NSOpenPanel* panel, const OpenFileOptions& options) {
        // the common options are applied before the panel runs (§5.3)
        configure(panel, options.title, options.filters, options.initial_directory);
        // an open panel chooses one existing file, which is what the caller needs (§5.3)
        panel.canChooseFiles = YES;
        // a directory is not a file the caller can open here, so it is not choosable (§5.3)
        panel.canChooseDirectories = NO;
        // the caller opens one netlist or executable at a time, which is the single selection contract (§5.3)
        panel.allowsMultipleSelection = NO;
    }

    void configure_save_panel(NSSavePanel* panel, const SaveFileOptions& options) {
        // the common options are applied before the panel runs (§5.3)
        configure(panel, options.title, options.filters, options.initial_directory);
        // the proposed name is what the user starts from, which a save panel edits (§5.3)
        if (!options.default_name.empty())
            panel.nameFieldStringValue = [NSString stringWithUTF8String:options.default_name.c_str()];
        // a save panel lets the user create a directory, which is what a file destination means (§5.3)
        panel.canCreateDirectories = YES;
    }

    void install_macos_file_dialog() {
        // the factory builds the appkit backend, which the application calls like any other (§5.3, AD-16)
        set_platform_file_dialog_factory([] { return std::unique_ptr<PlatformFileDialog>(new MacosFileDialog()); });
    }
} // namespace albor::macos
