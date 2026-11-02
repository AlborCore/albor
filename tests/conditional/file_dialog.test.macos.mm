// the macos file dialog configuration, which the platform suite asserts without showing a panel (§5.3, AD-16)

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <string>

#include <gtest/gtest.h>

#include "albor/application/file_dialog.h"
#include "albor/platform/macos/file_dialog.macos.h"

using namespace albor;

TEST(MacosFileDialog, AnOpenPanelCarriesTheRequestedOptions) {
    // arrange a native open panel and the request a caller writes (§5.3)
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    OpenFileOptions options;
    options.title = "Open netlist";
    options.filters = {FileFilter{.name = "Netlists", .extensions = {"kicad_sch", "kicad_pcb"}}};
    options.initial_directory = "/tmp";
    // act
    macos::configure_open_panel(panel, options);
    // assert the title, the types and the directory reached the panel (§5.3)
    ASSERT_NE(panel.title, nil);
    EXPECT_STREQ(panel.title.UTF8String, "Open netlist");
    ASSERT_EQ(panel.allowedContentTypes.count, 2U);
    EXPECT_TRUE([panel.allowedContentTypes containsObject:[UTType typeWithFilenameExtension:@"kicad_sch"]]);
    EXPECT_TRUE([panel.allowedContentTypes containsObject:[UTType typeWithFilenameExtension:@"kicad_pcb"]]);
    ASSERT_NE(panel.directoryURL, nil);
    EXPECT_STREQ(panel.directoryURL.fileSystemRepresentation, "/tmp");
    // assert the panel chooses one existing file, which is what the caller needs (§5.3)
    EXPECT_TRUE(panel.canChooseFiles);
    EXPECT_FALSE(panel.canChooseDirectories);
    EXPECT_FALSE(panel.allowsMultipleSelection);
}

TEST(MacosFileDialog, ASavePanelCarriesTheProposedName) {
    // arrange a native save panel and the request a caller writes (§5.3)
    NSSavePanel* panel = [NSSavePanel savePanel];
    SaveFileOptions options;
    options.title = "Save netlist";
    options.filters = {FileFilter{.name = "Netlists", .extensions = {"kicad_sch"}}};
    options.default_name = "netlist.kicad_sch";
    // act
    macos::configure_save_panel(panel, options);
    // assert the proposed name reached the panel, which is what the user edits (§5.3)
    ASSERT_NE(panel.nameFieldStringValue, nil);
    EXPECT_STREQ(panel.nameFieldStringValue.UTF8String, "netlist.kicad_sch");
    // assert a save panel lets the user create the destination directory (§5.3)
    EXPECT_TRUE(panel.canCreateDirectories);
}

TEST(MacosFileDialog, AnEmptyFilterListLeavesThePanelOpenToEveryFile) {
    // arrange a panel and a request that names no filter (§5.3)
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    OpenFileOptions options;
    options.title = "Any file";
    // act
    macos::configure_open_panel(panel, options);
    // assert no types were set, which is what an unrestricted panel means (§5.3)
    EXPECT_EQ(panel.allowedContentTypes.count, 0U);
}

TEST(MacosFileDialog, DuplicateExtensionsBecomeOneType) {
    // arrange two filters naming the same extension (§5.3)
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    OpenFileOptions options;
    options.filters = {FileFilter{.name = "Netlists", .extensions = {"kicad_sch"}}, FileFilter{.name = "Schematics", .extensions = {"kicad_sch"}}};
    // act
    macos::configure_open_panel(panel, options);
    // assert the panel receives one type, because the union is a set (§5.3)
    EXPECT_EQ(panel.allowedContentTypes.count, 1U);
}

TEST(MacosFileDialog, ANonAsciiDirectoryReachesThePanel) {
    // arrange a directory holding non ascii bytes, which the panel opens at (§5.3)
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    OpenFileOptions options;
    options.initial_directory = "/tmp/日本語";
    // act
    macos::configure_open_panel(panel, options);
    // assert the url carries the same bytes, which is what a utf8 path requires (§5.3)
    ASSERT_NE(panel.directoryURL, nil);
    EXPECT_STREQ(panel.directoryURL.fileSystemRepresentation, "/tmp/日本語");
}
