// the win32 file dialog configuration, which the platform suite asserts without showing a panel (§5.3, AD-16)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>
// clang-format on

#include <string>

#include <gtest/gtest.h>

#include "albor/application/file_dialog.h"
#include "albor/platform/windows/file_dialog.win32.h"

using namespace albor;

TEST(Win32FileDialog, AnOpenDialogCarriesTheRequestedOptions) {
    // arrange the apartment a shell dialog needs and a native open dialog (§5.3)
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ASSERT_TRUE(SUCCEEDED(initialized));
    Microsoft::WRL::ComPtr<IFileDialog> dialog;
    ASSERT_HRESULT_SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
    OpenFileOptions options;
    options.title = "Open netlist";
    options.filters = {FileFilter{.name = "Netlists", .extensions = {"kicad_sch", "kicad_pcb"}}};
    // act
    windows::configure_open_dialog(dialog.Get(), options);
    // assert the title and the joined filter reached the seam, which is the only path to the shell (§5.3)
    const windows::FileDialogConfiguration& applied = windows::last_file_dialog_configuration();
    EXPECT_EQ(applied.title, "Open netlist");
    ASSERT_EQ(applied.filters.size(), 1U);
    EXPECT_EQ(applied.filters.at(0), "Netlists:*.kicad_sch;*.kicad_pcb");
    // the dialog is released before the apartment it was created in (§5.3)
    dialog.Reset();
    CoUninitialize();
}

TEST(Win32FileDialog, ASaveDialogCarriesTheProposedName) {
    // arrange the apartment a shell dialog needs and a native save dialog (§5.3)
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ASSERT_TRUE(SUCCEEDED(initialized));
    Microsoft::WRL::ComPtr<IFileDialog> dialog;
    ASSERT_HRESULT_SUCCEEDED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
    SaveFileOptions options;
    options.title = "Save netlist";
    options.default_name = "netlist.kicad_sch";
    options.filters = {FileFilter{.name = "Netlists", .extensions = {"kicad_sch"}}};
    // act
    windows::configure_save_dialog(dialog.Get(), options);
    // assert the proposed name and the filter reached the seam (§5.3)
    const windows::FileDialogConfiguration& applied = windows::last_file_dialog_configuration();
    EXPECT_EQ(applied.title, "Save netlist");
    EXPECT_EQ(applied.default_name, "netlist.kicad_sch");
    ASSERT_EQ(applied.filters.size(), 1U);
    EXPECT_EQ(applied.filters.at(0), "Netlists:*.kicad_sch");
    // the dialog is released before the apartment it was created in (§5.3)
    dialog.Reset();
    CoUninitialize();
}

TEST(Win32FileDialog, TheInitialDirectoryReachesTheSeam) {
    // arrange the apartment a shell dialog needs and a request with a directory (§5.3)
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ASSERT_TRUE(SUCCEEDED(initialized));
    Microsoft::WRL::ComPtr<IFileDialog> dialog;
    ASSERT_HRESULT_SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
    OpenFileOptions options;
    options.title = "Open netlist";
    options.initial_directory = "C:\\Windows";
    // act
    windows::configure_open_dialog(dialog.Get(), options);
    // assert the directory is what the seam applied, which is the value the shell reads (§5.3)
    EXPECT_EQ(windows::last_file_dialog_configuration().directory, "C:\\Windows");
    // the dialog is released before the apartment it was created in (§5.3)
    dialog.Reset();
    CoUninitialize();
}

TEST(Win32FileDialog, AnEmptyFilterShowsEveryFile) {
    // arrange a filter with no extensions, which means any file (§5.3)
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ASSERT_TRUE(SUCCEEDED(initialized));
    Microsoft::WRL::ComPtr<IFileDialog> dialog;
    ASSERT_HRESULT_SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
    OpenFileOptions options;
    options.filters = {FileFilter{.name = "All files", .extensions = {}}};
    // act
    windows::configure_open_dialog(dialog.Get(), options);
    // assert the shell pattern accepts every name (§5.3)
    ASSERT_EQ(windows::last_file_dialog_configuration().filters.size(), 1U);
    EXPECT_EQ(windows::last_file_dialog_configuration().filters.at(0), "All files:*.*");
    // the dialog is released before the apartment it was created in (§5.3)
    dialog.Reset();
    CoUninitialize();
}

TEST(Win32FileDialog, AnUnnamedFilterFallsBackToItsPattern) {
    // arrange a filter with no label, which the shell would show empty (§5.3)
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ASSERT_TRUE(SUCCEEDED(initialized));
    Microsoft::WRL::ComPtr<IFileDialog> dialog;
    ASSERT_HRESULT_SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
    OpenFileOptions options;
    options.filters = {FileFilter{.name = "", .extensions = {"kicad_sch"}}};
    // act
    windows::configure_open_dialog(dialog.Get(), options);
    // assert the label reads as the pattern, which is still useful to the user (§5.3)
    ASSERT_EQ(windows::last_file_dialog_configuration().filters.size(), 1U);
    EXPECT_EQ(windows::last_file_dialog_configuration().filters.at(0), "*.kicad_sch:*.kicad_sch");
    // the dialog is released before the apartment it was created in (§5.3)
    dialog.Reset();
    CoUninitialize();
}
