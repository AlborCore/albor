// Prototype A1-Graphite-Dawn: why does this build resolve no typeface?
//
// The text workload is not optional - it is the whole reason skia stays in the tree - so the cause has to be found
// rather than recorded as unmeasurable. This probe asks the only font manager the build has, in the order that
// matters, and prints what each route returns:
//
//   A  SkFontMgr_New_CoreText(nullptr) as a bare command-line process
//   B  the same call after [NSApplication sharedApplication]
//   C  a collection built from an explicit font descriptor
//
// A is what a1's resource factory does today. If B works and A does not, the cause is that a CLI process has no
// application object, which is an integration requirement and not a skia defect.

#include <skia/core/SkFontMgr.h>
#include <skia/core/SkTypeface.h>
#include <skia/ports/SkFontMgr_mac_ct.h>

#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>

#include <cstdio>
#include <string>

namespace
{

    const char* ok(const sk_sp<SkTypeface>& face) { return face != nullptr ? "ok" : "null"; }

    std::string describe(const sk_sp<SkFontMgr>& manager) {
        if (manager == nullptr) {
            return "manager is null";
        }

        std::string text = std::to_string(manager->countFamilies()) + " families";
        if (manager->countFamilies() > 0) {
            SkString first;
            manager->getFamilyName(0, &first);
            text += std::string(", first family \"") + first.c_str() + "\"";
        }

        text += std::string(", matchFamilyStyle(null)=") + ok(manager->matchFamilyStyle(nullptr, SkFontStyle()));
        text += std::string(", (Helvetica)=") + ok(manager->matchFamilyStyle("Helvetica", SkFontStyle()));
        text += std::string(", (Arial)=") + ok(manager->matchFamilyStyle("Arial", SkFontStyle()));
        text += std::string(", legacyMakeTypeface(null)=") + ok(manager->legacyMakeTypeface(nullptr, SkFontStyle()));
        return text;
    }

    void probe(const char* label) {
        const sk_sp<SkFontMgr> manager = SkFontMgr_New_CoreText(nullptr);
        std::printf("  %-46s %s\n", label, describe(manager).c_str());
    }

} // namespace

int main() {
    std::printf("font probe: %s\n", "this build's only font manager is SkFontMgr_New_CoreText");

    // route A, which is exactly what prototypes/a1/resource_factory.macos.mm does
    std::printf("\nroute A: as a bare command-line process\n");
    probe("SkFontMgr_New_CoreText(nullptr)");

    // route B: give the process an application object first. a1's slices all created windows, so they had one and
    // this driver does not, which is the difference worth testing
    std::printf("\nroute B: after [NSApplication sharedApplication]\n");
    @autoreleasepool {
        [NSApplication sharedApplication];
        probe("SkFontMgr_New_CoreText(nullptr)");
    }

    // route C: build a collection from a descriptor instead of asking for the default one
    std::printf("\nroute C: an explicit collection from a named descriptor\n");
    @autoreleasepool {
        const void* keys[] = {kCTFontFamilyNameAttribute};
        const void* values[] = {CFSTR("Helvetica")};
        CFDictionaryRef attribute_dict = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

        CTFontDescriptorRef descriptor = CTFontDescriptorCreateWithAttributes(attribute_dict);
        const void* items[1] = {descriptor};
        CFArrayRef descriptors = CFArrayCreate(kCFAllocatorDefault, items, 1, &kCFTypeArrayCallBacks);
        CTFontCollectionRef collection = CTFontCollectionCreateWithFontDescriptors(descriptors, nullptr);
        const sk_sp<SkFontMgr> manager = SkFontMgr_New_CoreText(collection);
        std::printf("  %-46s %s\n", "SkFontMgr_New_CoreText(collection)", describe(manager).c_str());
        if (manager != nullptr) {
            const sk_sp<SkTypeface> face = manager->matchFamilyStyle(nullptr, SkFontStyle());
            SkString name;
            if (face != nullptr) {
                face->getFamilyName(&name);
            }
            std::printf("  %-46s %s\n", "  face family name", face != nullptr ? name.c_str() : "null");
        }
        CFRelease(collection);
        CFRelease(descriptors);
        CFRelease(descriptor);
        CFRelease(attribute_dict);
    }

    return 0;
}
