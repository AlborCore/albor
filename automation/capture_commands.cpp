#include "automation/capture_commands.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "automation/png_writer.h"

namespace albor::automation
{
    namespace
    {
        // the eight magic bytes a raw frame opens with, which a client checks before it reads the extent (§6.3)
        constexpr std::string_view RAW_MAGIC = "ALBRSHOT";

        // the pixel format a raw frame names, which is premultiplied rgba8 top down and never a host layout (AD-26)
        constexpr std::uint32_t RAW_PIXEL_FORMAT = 1;

        // the words that follow the magic, which are width, height, stride and pixel format in that order (§6.3)
        constexpr std::size_t RAW_WORDS = 4;

        // the raw header length, which the magic and those words fix (§6.3)
        constexpr std::size_t RAW_HEADER_BYTES = RAW_MAGIC.size() + RAW_WORDS * sizeof(std::uint32_t);

        // the header the encoder writes is the one every client reads, so a drift here would break each of them (§6.3)
        static_assert(RAW_HEADER_BYTES == 24);

        // append one little endian word, which the raw header spells so no client guesses the host byte order (§6.3)
        void append_word(std::string& out, std::uint32_t word) {
            // the four bytes go least significant first, which is the order the header documents (§6.3)
            for (int shift = 0; shift < 32; shift += 8)
                out.push_back(static_cast<char>((word >> shift) & 0xFFu));
        }

        // the frame a client decodes byte by byte, which a pixel comparison reads exactly as the renderer wrote it (§6.3)
        std::string encode_raw(const CapturedFrame& frame) {
            // the stride is the tight row length, because every backend reads packed rows (AD-26)
            const std::uint32_t stride = static_cast<std::uint32_t>(frame.width) * 4u;
            // the bytes are appended after the header, which keeps the header parseable before the payload is read (§6.3)
            std::string out;
            out.reserve(RAW_HEADER_BYTES + frame.pixels.size());
            out.append(RAW_MAGIC);
            append_word(out, static_cast<std::uint32_t>(frame.width));
            append_word(out, static_cast<std::uint32_t>(frame.height));
            append_word(out, stride);
            append_word(out, RAW_PIXEL_FORMAT);
            // the bytes are appended only when there are some, because a null data pointer is no range at all (§6.3)
            if (!frame.pixels.empty())
                out.append(reinterpret_cast<const char*>(frame.pixels.data()), frame.pixels.size());
            return out;
        }
    } // namespace

    std::vector<std::string> capture_command_names() {
        // the names are fixed rather than discovered, because a host allow-lists before any registry exists (§10.3, AD-10)
        return {std::string(SCREENSHOT_COMMAND)};
    }

    void register_capture_commands(AutomationRegistry& registry, CaptureSnapshot snapshot) {
        // the screenshot reads the most recently bound window per request, which is the rule every command shares (§10.3)
        registry.register_command(std::string(SCREENSHOT_COMMAND), [snapshot = std::move(snapshot)](const AutomationRequest& request) {
            // a host with no window source has nothing to read, which is refused rather than answered empty (§10.3)
            if (!snapshot)
                return AutomationResult::refused("the host supplied no capture source");
            // the format is optional, because a client that names none takes the picture this layer defaults to (§6.3)
            const std::string_view format = request.arguments.empty() ? PNG_FORMAT : std::string_view(request.arguments.front());
            // a format this build cannot produce is refused, because a client must not decode other bytes (§6.3)
            if (format != PNG_FORMAT && format != RAW_FORMAT)
                return AutomationResult::refused("the capture format is not available: " + std::string(format));
            // a frame is read and never rebuilt, so a mutation from this same pass waits for the next frame (§6.1)
            const CapturedFrame frame = snapshot();
            // a window that could not be read states why, which keeps a black frame from passing as a capture (§6.3)
            if (!frame.error.empty())
                return AutomationResult::refused(frame.error);
            // a frame whose extent and bytes disagree is refused, because its header would describe another picture (§6.3)
            if (frame.width <= 0 || frame.height <= 0 || frame.pixels.size() != static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4u)
                return AutomationResult::refused("the window has not presented a frame at its current size");
            // the exact bytes answer a client that compares pixels, which no image decoder reproduces (§6.3)
            if (format == RAW_FORMAT)
                return AutomationResult::answered(ValueKind::Binary, encode_raw(frame));
            // the picture answers a client that shows the frame, which is what the default names (§6.3)
            std::string png = encode_png(frame.width, frame.height, frame.pixels);
            // a frame the encoder could not compress is refused, because an empty picture reads as a black one (§6.3)
            if (png.empty())
                return AutomationResult::refused("the frame could not be encoded as a png");
            return AutomationResult::answered(ValueKind::Image, std::move(png));
        });
    }
} // namespace albor::automation
