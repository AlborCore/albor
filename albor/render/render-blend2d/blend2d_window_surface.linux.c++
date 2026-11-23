#include "albor/render/render-blend2d/blend2d_window_surface.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <wayland-client.h>

#include "albor/platform/linux/wayland/native_surface.linux.h"
#include "albor/render/render-blend2d/blend2d_render_surface.h"

namespace albor::blend2d
{
    namespace
    {
        // one frame the compositor may still read, which a later present frees (§6.3)
        struct PresentedBuffer
        {
            // the pool the buffer was carved from
            wl_shm_pool* pool = nullptr;

            // the buffer proxy the compositor presents
            wl_buffer* buffer = nullptr;

            // the mapping the compositor reads pixels from
            void* data = nullptr;

            // the descriptor the mapping came from
            int fd = -1;

            // the byte extent the mapping spans
            int size = 0;

            // whether the compositor released the buffer, which the next frame frees (§6.3)
            std::atomic<bool> released{false};
        };

        // free the pool, mapping and descriptor of one presented frame (§6.3)
        void free_presented(PresentedBuffer& presented) {
            // the buffer proxy is destroyed first, because the pool it came from must outlive it (§6.3)
            if (presented.buffer != nullptr)
                wl_buffer_destroy(presented.buffer);
            // the pool is destroyed, which the compositor no longer reads (§6.3)
            if (presented.pool != nullptr)
                wl_shm_pool_destroy(presented.pool);
            // the mapping is unmapped and its descriptor closed, which the pool came from (§6.3)
            if (presented.data != nullptr)
                munmap(presented.data, static_cast<std::size_t>(presented.size));
            // a descriptor that still names a file is closed, which every failure path owns (§6.3)
            if (presented.fd >= 0)
                close(presented.fd);
            // the record clears, which a second free reports (§6.3)
            presented.pool = nullptr;
            presented.buffer = nullptr;
            presented.data = nullptr;
            presented.fd = -1;
            presented.size = 0;
        }

        // the release the compositor sent, which marks the frame free for the next present (§6.3)
        void on_buffer_release(void* data, wl_buffer*) {
            // only the flag is written, because the freeing waits for the render thread (§6.3)
            static_cast<PresentedBuffer*>(data)->released.store(true, std::memory_order_release);
        }

        // the listener every presented buffer carries, which marks its frame released (§6.3)
        constexpr wl_buffer_listener BUFFER_LISTENER = {&on_buffer_release};

        // the cpu raster surface, which draws into a blend2d image and presents it through wl_shm (§6.3, AD-26)
        class WaylandWindowSurface final : public RenderSurface
        {
        public:
            // build the surface over the state the window shares and the raster it presents
            WaylandWindowSurface(std::shared_ptr<wayland::WaylandSurfaceState> state, std::unique_ptr<Blend2DRenderSurface> raster) :
                m_state(std::move(state)), m_raster(std::move(raster)) {}

            // release every frame the compositor has not released, which the display no longer reads (§6.3)
            ~WaylandWindowSurface() override {
                // the state is locked, because a queued release may still arrive through the window (§10.5)
                const std::lock_guard<std::mutex> lock(m_state->mutex);
                // every retained frame is freed, which no present can read after the surface is gone (§6.3)
                for (const std::unique_ptr<PresentedBuffer>& presented : m_presented)
                    free_presented(*presented);
                // the records clear, which the surface no longer owns (§6.3)
                m_presented.clear();
            }

            // override: report whether the raster and the shared state are usable (§6.3)
            [[nodiscard]] bool valid() const override {
                // the state is locked, because teardown clears it on the ui thread (§10.5)
                const std::lock_guard<std::mutex> lock(m_state->mutex);
                // every native object and the raster is what a present needs (§6.3)
                return m_raster != nullptr && m_raster->valid() && m_state->display != nullptr && m_state->surface != nullptr && m_state->shm != nullptr;
            }

            // override: hand out the metrics the raster is sized at (§6.2)
            [[nodiscard]] const WindowMetrics& metrics() const override {
                // the raster carries the authoritative metrics (§6.2)
                return m_raster->metrics();
            }

            // override: resize the raster, which the next present carries at the new extent (§6.2)
            void resize(const WindowMetrics& metrics) override {
                // a surface that cannot draw keeps its previous raster (§6.2)
                if (!valid() || !metrics.valid())
                    return;
                // the raster is rebuilt at the new extent, which the next present copies (§6.2, AD-27)
                m_raster->resize(metrics);
            }

            // override: begin a frame and hand out the context that draws it (§6.1)
            RenderContext& begin_frame() override {
                // the frame is begun, which the end balances (§6.1)
                m_began = true;
                // the raster draws into its own cpu image, which the present afterwards copies (§6.3)
                return m_raster->begin_frame();
            }

            // override: end the frame by flushing the raster and presenting it through wl_shm (§6.1, §6.3)
            void end_frame() override {
                // an unbalanced end would present a frame that never began, so it is ignored (§6.1)
                if (!m_began)
                    return;
                // the frame is marked finished before the present, so a failed copy skips it (§6.1)
                m_began = false;
                // the raster is flushed first, because the present copies complete pixels (§6.3)
                m_raster->end_frame();
                // the finished raster is copied into a fresh shm buffer and committed (§6.3)
                present();
            }

            // override: read the last finished frame back, which the raster carries (§6.3)
            [[nodiscard]] std::vector<std::uint8_t> read_pixels() const override {
                // a surface without a raster reads nothing (§6.3)
                if (m_raster == nullptr)
                    return {};
                // the raster is the same picture the present copied (§6.3)
                return m_raster->read_pixels();
            }

            // override: report how many frames reached the compositor, which a diagnostic or test reads (§6.3)
            [[nodiscard]] std::size_t presented_frames() const override {
                // the counter is read across threads, which is why it is atomic (§6.3)
                return m_presented_frames.load();
            }

        private:
            // copy the finished raster into a wl_shm buffer and commit it (§6.3, AD-26)
            void present() {
                // the physical extent is what the raster and the surface share (§6.3, AD-27)
                const int width = static_cast<int>(m_raster->metrics().pixel_size.width);
                const int height = static_cast<int>(m_raster->metrics().pixel_size.height);
                // an extent with no area presents nothing (§6.2)
                if (width <= 0 || height <= 0)
                    return;
                // the display and the pool are read under the lock, because teardown clears them (§10.5)
                wl_shm* shm = nullptr;
                {
                    const std::lock_guard<std::mutex> lock(m_state->mutex);
                    // a hidden, dismissed or torn down window presents nothing (§5.3, §10.5)
                    if (!m_state->mapped || !m_state->configured || m_state->display == nullptr || m_state->surface == nullptr || m_state->shm == nullptr)
                        return;
                    // every frame the compositor released is freed, which bounds the retained memory (§6.3)
                    std::erase_if(m_presented, [](const std::unique_ptr<PresentedBuffer>& presented) {
                        if (!presented->released.load(std::memory_order_acquire))
                            return false;
                        free_presented(*presented);
                        return true;
                    });
                    // the pool is borrowed for the buffer build, which runs outside the lock (§6.3)
                    shm = m_state->shm;
                }
                // the stride is one row of premultiplied bgra, which the native copy writes (§6.3)
                const int stride = width * 4;
                // the whole buffer is the rows of the extent (§6.3)
                const int size = stride * height;
                // the pool is one anonymous file the compositor reads pixels from (§6.3)
                const int fd = memfd_create("albor-blend2d", MFD_CLOEXEC);
                // a pool that could not be created presents nothing, which the next frame retries (§6.3)
                if (fd < 0)
                    return;
                // the file is sized to the buffer, which the compositor maps its side of (§6.3)
                if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
                    // the descriptor is closed, because the pool never came up (§6.3)
                    close(fd);
                    return;
                }
                // the mapping is shared, because the compositor reads its side directly (§6.3)
                void* data = mmap(nullptr, static_cast<std::size_t>(size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                // a mapping that failed leaves the descriptor closed, which the next frame retries (§6.3)
                if (data == MAP_FAILED) {
                    close(fd);
                    return;
                }
                // the frame owns its pool, mapping and descriptor until the compositor releases it (§6.3)
                std::unique_ptr<PresentedBuffer> presented = std::make_unique<PresentedBuffer>();
                presented->data = data;
                presented->fd = fd;
                presented->size = size;
                // the finished raster copies its native premultiplied bgra rows into the mapping (§6.3)
                if (!m_raster->copy_native_pixels(data, static_cast<std::size_t>(stride))) {
                    // a copy that refused leaves the mapping and descriptor freed (§6.3)
                    free_presented(*presented);
                    return;
                }
                // the pool is created on the compositor, which reads the descriptor's bytes (§6.3)
                presented->pool = wl_shm_create_pool(shm, fd, size);
                // a pool the compositor refused leaves the mapping freed (§6.3)
                if (presented->pool == nullptr) {
                    free_presented(*presented);
                    return;
                }
                // the buffer is carved from the pool, which the compositor scales and composes (§6.3, AD-27)
                presented->buffer = wl_shm_pool_create_buffer(presented->pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
                // a buffer that could not be created leaves the pool destroyed (§6.3)
                if (presented->buffer == nullptr) {
                    free_presented(*presented);
                    return;
                }
                // the release event marks the frame, which the next present frees (§6.3)
                wl_buffer_add_listener(presented->buffer, &BUFFER_LISTENER, presented.get());
                // the display is remembered for the flush, which runs outside the lock (§10.5)
                wl_display* display = nullptr;
                {
                    // the state is locked again, because a hide or a teardown may have landed during the copy (§10.5)
                    const std::lock_guard<std::mutex> lock(m_state->mutex);
                    // a window that went off screen during the copy drops the frame it built (§5.3)
                    if (!m_state->mapped || !m_state->configured || m_state->surface == nullptr) {
                        free_presented(*presented);
                        return;
                    }
                    // the buffer is attached, which the commit below presents (§6.3)
                    wl_surface_attach(m_state->surface, presented->buffer, 0, 0);
                    // the whole surface is damaged, which the compositor redraws (§6.3)
                    wl_surface_damage(m_state->surface, 0, 0, INT32_MAX, INT32_MAX);
                    // the commit presents the buffer, which the compositor copies (§6.3)
                    wl_surface_commit(m_state->surface);
                    // the frame is retained until its release, which the next frame frees (§6.3)
                    m_presented.push_back(std::move(presented));
                    // the frame reached the compositor, which the presentation counter records (§6.3)
                    m_presented_frames++;
                    // the display is read before the lock drops, because the state may be cleared afterwards (§10.5)
                    display = m_state->display;
                }
                // the requests are sent outside the lock, so the render thread never blocks a ui thread write (§10.5)
                wl_display_flush(display);
            }

            // the surface state the window shares, which teardown clears before the proxies die (§10.5)
            std::shared_ptr<wayland::WaylandSurfaceState> m_state;

            // the raster the frame draws into, which the present copies (§6.3)
            std::unique_ptr<Blend2DRenderSurface> m_raster;

            // the frames the compositor may still read, which a later present frees (§6.3)
            std::vector<std::unique_ptr<PresentedBuffer>> m_presented;

            // the frames the compositor received, which a diagnostic reads across threads (§6.3)
            std::atomic<std::size_t> m_presented_frames{0};

            // whether a frame was begun and not ended, which the end balances (§6.1)
            bool m_began = false;
        };

        // the installer that registers the wayland canvas at load, so linking the cpu backend is the selection (§5.3)
        struct WindowSurfaceInstall
        {
            WindowSurfaceInstall() {
                // the factory builds the canvas for a valid wayland handle, which a host without one refuses (§5.3)
                set_window_surface_factory([](NativeWindowHandle handle, const WindowMetrics& metrics) -> std::unique_ptr<RenderSurface> {
                    // a handle that names no wayland surface cannot present (§3.5)
                    const auto* native = static_cast<const wayland::WaylandNativeSurface*>(handle.object());
                    // a handle without the shared state is refused, which a closed window reports (§6.3)
                    if (native == nullptr || native->state == nullptr)
                        return nullptr;
                    // the shared state is locked, because the window may clear it on teardown (§10.5)
                    const std::lock_guard<std::mutex> lock(native->state->mutex);
                    // a window that is not mapped has no surface to present through (§5.3)
                    if (native->state->display == nullptr || native->state->surface == nullptr || native->state->shm == nullptr)
                        return nullptr;
                    // the raster is built at the metrics the window reports (§6.2, AD-27)
                    std::unique_ptr<Blend2DRenderSurface> raster = Blend2DRenderSurface::create(metrics);
                    // a raster that could not allocate leaves the window without a surface (§6.3)
                    if (raster == nullptr)
                        return nullptr;
                    // the canvas is returned as the hal, so no wayland type reaches the framework (§3.5, AD-05)
                    return std::unique_ptr<RenderSurface>(new WaylandWindowSurface(native->state, std::move(raster)));
                });
            }
        };

        // the installer is never referenced, because the load runs it before the factory is read (§5.3)
        const WindowSurfaceInstall g_window_surface_install;
    } // namespace
} // namespace albor::blend2d
