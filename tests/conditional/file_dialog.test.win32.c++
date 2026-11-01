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

#include "application/file_dialog.h"
#include "platform/windows/file_dialog.win32.h"

using namespace albor;

TEST(Win32FileDialog, AnOpenDialogCarriesTheRequestedOptions) {
    // arrange the apartment a shell dialog needs and a native open dialog (§5.3)
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ASSERT_TRUE(SUCCEEDED(initialized) || initialized == S_FALSE);
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    ASSERT_HRESULT_SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
    OpenFileOptions options;
    options.title = "Open netlist";
    options.filters = {FileFilter{.name = "Netlists", .extensions = {"kicad_sch", "kicad_pcb"}}};
    options.initial_directory = "C:\\Temp";
    // act
    windows::configure_open_dialog(dialog.Get(), options);
    // assert the title reached the dialog (§5.3)
    LPWSTR title = nullptr;
    ASSERT_HRESULT_SUCCEEDED(dialog->GetTitle(&title));
    ASSERT_NE(title, nullptr);
    EXPECT_STREQ(title, L"Open netlist");
    CoTaskMemFree(title);
    // assert the filter joined its extensions into one shell pattern (§5.3)
    UINT count = 0;
    COMDLG_FILTERSPEC* specs = nullptr;
    ASSERT_HRESULT_SUCCEEDED(dialog->GetFileTypes(&count, &specs));
    ASSERT_EQ(count, 1U);
    ASSERT_NE(specs, nullptr);
    EXPECT_STREQ(specs[0].pszName, L"Netlists");
    EXPECT_STREQ(specs[0].pszSpec, L"*.kicad_sch;*.kicad_pcb");
    CoTaskMemFree(specs);
    CoUninitialize();
}

TEST(Win32FileDialog, ASaveDialogCarriesTheProposedName) {
    // arrange the apartment a shell dialog needs and a native save dialog (§5.3)
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ASSERT_TRUE(SUCCEEDED(initialized) || initialized == S_FALSE);
    Microsoft::WRL::ComPtr<IFileSaveDialog> dialog;
    ASSERT_HRESULT_SUCCEEDED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
    SaveFileOptions options;
    options.title = "Save netlist";
    options.default_name = "netlist.kicad_sch";
    options.filters = {FileFilter{.name = "Netlists", .extensions = {"kicad_sch"}}};
    // act
    windows::configure_save_dialog(dialog.Get(), options);
    // assert the proposed name reached the dialog, which is what the user edits (§5.3)
    LPWSTR name = nullptr;
    ASSERT_HRESULT_SUCCEEDED(dialog->GetFileName(&name));
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, L"netlist.kicad_sch");
    CoTaskMemFree(name);
    CoUninitialize();
}

TEST(Win32FileDialog, TheInitialDirectoryBecomesTheFolder) {
    // arrange the apartment a shell dialog needs and a request with an existing directory (§5.3)
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ASSERT_TRUE(SUCCEEDED(initialized) || initialized == S_FALSE);
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    ASSERT_HRESULT_SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
    OpenFileOptions options;
    options.title = "Open netlist";
    options.initial_directory = "C:\\Windows";
    // act
    windows::configure_open_dialog(dialog.Get(), options);
    // assert the starting folder is the one the caller named (§5.3)
    Microsoft::WRL::ComPtr<IShellItem> folder;
    ASSERT_HRESULT_SUCCEEDED(dialog->GetFolder(&folder));
    ASSERT_NE(folder.Get(), nullptr);
    PWSTR path = nullptr;
    ASSERT_HRESULT_SUCCEEDED(folder->GetDisplayName(SIGDN_FILESYSPATH, &path));
    ASSERT_NE(path, nullptr);
    EXPECT_NE(wcsstr(path, L"Windows"), nullptr);
    CoTaskMemFree(path);
    CoUninitialize();
}

TEST(Win32FileDialog, AnEmptyFilterShowsEveryFile) {
    // arrange a filter with no extensions, which means any file (§5.3)
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ASSERT_TRUE(SUCCEEDED(initialized) || initialized == S_FALSE);
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    ASSERT_HRESULT_SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
    OpenFileOptions options;
    options.filters = {FileFilter{.name = "All files", .extensions = {}}};
    // act
    windows::configure_open_dialog(dialog.Get(), options);
    // assert the shell pattern accepts every name (§5.3)
    UINT count = 0;
    COMDLG_FILTERSPEC* specs = nullptr;
    ASSERT_HRESULT_SUCCEEDED(dialog->GetFileTypes(&count, &specs));
    ASSERT_EQ(count, 1U);
    EXPECT_STREQ(specs[0].pszSpec, L"*.*");
    CoTaskMemFree(specs);
    CoUninitialize();
}

TEST(Win32FileDialog, AnUnnamedFilterFallsBackToItsPattern) {
    // arrange a filter with no label, which the shell would show empty (§5.3)
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ASSERT_TRUE(SUCCEEDED(initialized) || initialized == S_FALSE);
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    ASSERT_HRESULT_SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
    OpenFileOptions options;
    options.filters = {FileFilter{.name = "", .extensions = {"kicad_sch"}}};
    // act
    windows::configure_open_dialog(dialog.Get(), options);
    // assert the label reads as the pattern, which is still useful to the user (§5.3)
    UINT count = 0;
    COMDLG_FILTERSPEC* specs = nullptr;
    ASSERT_HRESULT_SUCCEEDED(dialog->GetFileTypes(&count, &specs));
    ASSERT_EQ(count, 1U);
    EXPECT_STREQ(specs[0].pszName, L"*.kicad_sch");
    CoTaskMemFree(specs);
    CoUninitialize();
}
