#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace albor::a1
{

    // the opaque retained resource set: its definition lives in the implementation that talks to the gpu, so no
    // skia type reaches this header (AD-05, AD-19)
    class ResourceSet;

    // what a retained line batch is built from, which is the primitive of §11.2
    struct LineBatchSpec
    {
        std::size_t segment_count = 0; // how many cosmetic segments the batch holds
        float extent = 0.0F; // the side of the square the segments are generated in
        float stroke_width = 1.0F; // the device width of a cosmetic line
        std::uint64_t seed = 0; // the generator seed, so a run is reproducible
    };

    // what a retained symbol set is built from, which is the cad workload of §11.3
    struct SymbolSetSpec
    {
        std::size_t symbol_count = 0; // how many symbols the canvas holds
        std::size_t segments_per_symbol = 0; // how many segments outline one symbol
        std::size_t label_count = 0; // how many text labels accompany the symbols
        float extent = 0.0F; // the side of the square the symbols are placed in
        float label_size = 0.0F; // the em size of a label
        std::uint64_t seed = 0; // the generator seed, so a run is reproducible
    };

    // one retained resource set, as the application sees it: opaque, immutable and shareable across threads (§10.5)
    using ResourceSetPtr = std::shared_ptr<const ResourceSet>;

    // what a retained set actually holds, exposed so a workload can report it without a skia type (§11.2)
    struct ResourceSetInfo
    {
        // a retained line batch always holds two points per segment, which is what a cosmetic line needs
        static constexpr std::size_t points_per_segment = 2;
        std::size_t points = 0; // how many points the retained geometry holds, two per segment
        std::size_t symbols = 0; // how many symbols the hit index holds
        std::size_t labels = 0; // how many text labels the set holds
        std::size_t bytes = 0; // how many bytes the retained resources occupy
        std::size_t draw_calls = 0; // how many draw calls one frame spends replaying the geometry
        float extent = 0.0F; // the side of the square the resources were generated in

        // the segment count, which is what a workload reports against its workload size
        [[nodiscard]] std::size_t segments() const { return points / points_per_segment; }
    };

    // report what a retained set holds, without exposing its implementation (AD-05, AD-19)
    [[nodiscard]] ResourceSetInfo describe(const ResourceSet& set);

    // read one symbol centre out of the hit index, which is what a hover workload aims its queries at
    [[nodiscard]] bool symbol_centre(const ResourceSet& set, std::size_t index, float& x, float& y);

    // the factory that owns the gpu resources of one window and hands out immutable sets (§11.2)
    class ResourceFactory
    {
    public:
        ResourceFactory();
        ~ResourceFactory();

        ResourceFactory(const ResourceFactory&) = delete;
        ResourceFactory& operator=(const ResourceFactory&) = delete;
        ResourceFactory(ResourceFactory&&) = delete;
        ResourceFactory& operator=(ResourceFactory&&) = delete;

        // build a retained batch of cosmetic line segments, which is what workload w1 measures (§11.2)
        // this is called on a background worker: retained geometry is cpu side and needs no gpu context
        [[nodiscard]] ResourceSetPtr create_line_batch(const LineBatchSpec& spec) const;

        // build a retained symbol set with its hit index and its labels, which is what workload w2 measures (§11.3)
        [[nodiscard]] ResourceSetPtr create_symbol_set(const SymbolSetSpec& spec) const;

        // the bytes the retained sets occupy, so an o(1) claim can be checked against something
        [[nodiscard]] std::size_t retained_bytes() const;

    private:
        struct Impl;
        Impl* m_impl;
    };

} // namespace albor::a1
