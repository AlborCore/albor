#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "prototypes/d2/src/index.h"
#include "prototypes/d2/src/schema.h"
#include "prototypes/d2/src/storage.h"
#include "prototypes/d2/src/value.h"

namespace albor::prototype_d2
{

    // choose how a settle turns invalidations into evaluations (AD-04c)
    enum class EvalModel : std::uint8_t
    {
        EagerPush, // Prototype D's model: plan, evaluate, repeat until the plan is empty (§13.3)
        PullTopo, // one pass in a Kahn order computed once at seal (§13.3)
        HybridPushPull // push marks the dirty set, a memoised pull evaluates each dirty binding once (§13.3)
    };

    [[nodiscard]] std::string_view model_name(EvalModel model);

    // choose how ending an animation restores the value the binding owns (AD-04d)
    enum class AnimationEndMode : std::uint8_t
    {
        RestoreShadow, // Prototype D's path: put the shadow back, then invalidate the target (§13.5)
        ReEvaluate, // drop the shadow and invalidate, so the binding recomputes from current dependencies (§13.5)
        RestoreNoInvalidate // diagnostic: put the shadow back and invalidate nothing, to isolate what protects D (§13.5)
    };

    // choose how the animation layer records the binding value an animation shadows (plan.md R7)
    enum class AnimationShadowing : std::uint8_t
    {
        Full, // D2's recorded path: an entry per animated property carrying the binding value (§13.5)
        ActiveSet, // the audit's proposed minimal mechanism: a record that an animation owns the target, no value (§28.4)
        None // the naive elimination: no record at all, invalidation only (§28.4)
    };

    // report what one settle did, so a topology can be characterised rather than only timed (§28.2)
    struct SettleStats
    {
        std::size_t passes = 0;
        std::size_t bindings_evaluated = 0;
        std::size_t redundant_evaluations = 0;
        std::size_t candidates_considered = 0;
        std::size_t invalidations = 0;
        bool cycle_budget_exhausted = false;
        bool cycle_detected = false;
    };

    // bound the passes of a settle so a cycle cannot hang the UI thread, as in Prototype D (§28.2)
    constexpr std::size_t MAX_SETTLE_PASSES = 64;

    // notify the graph that a binding wrote its target, so a chain settles (§13.3)
    using WriteObserver = void (*)(void* user, PropertyId property, bool changed);

    // hand a binding a plain context with no heap allocation (§13.3)
    template <class Store>
    class BindingContext
    {
    public:
        BindingContext(Store& store, PropertyId target, BindingId self, std::span<const PropertyId> dependencies, WriteObserver observer, void* user) :
            m_store(store), m_target(target), m_self(self), m_dependencies(dependencies), m_observer(observer), m_user(user) {}

        [[nodiscard]] Value read(PropertyId id) const { return m_store.read(id); }
        [[nodiscard]] std::int64_t read_int(PropertyId id) const { return m_store.read(id).as_int(); }
        [[nodiscard]] double read_double(PropertyId id) const { return m_store.read(id).as_double(); }
        [[nodiscard]] bool read_bool(PropertyId id) const { return m_store.read(id).as_bool(); }
        [[nodiscard]] BindingId self() const { return m_self; }
        [[nodiscard]] std::span<const PropertyId> declared() const { return m_dependencies; }
        [[nodiscard]] PropertyId target() const { return m_target; }

        // write only the binding target, and never while an animation owns it (§13.4, §13.5)
        void write(Value value) const {
            // drop the write while the animation layer owns the target (§13.5)
            if (m_store.source_of(m_target) == WriteSource::Animation) {
                return;
            }

            // publish through the path that keeps the binding alive (§13.4)
            const bool changed = m_store.write(m_target, std::move(value), WriteSource::Binding);

            // tell the graph so that a chain settles (§13.3)
            if (m_observer != nullptr) {
                m_observer(m_user, m_target, changed);
            }
        }

        void write_int(std::int64_t value) const { write(Value(value)); }
        void write_double(double value) const { write(Value(value)); }
        void write_bool(bool value) const { write(Value(value)); }
        void write_string(std::shared_ptr<const std::string> value) const { write(Value(std::move(value))); }

    private:
        Store& m_store;
        PropertyId m_target;
        BindingId m_self;
        std::span<const PropertyId> m_dependencies;
        WriteObserver m_observer;
        void* m_user;
    };

    // compile a binding into a plain function plus declared property ids (§13.3)
    template <class Store>
    using BindingFn = void (*)(BindingContext<Store>&);

    // hold the binding table, the dependency index and the evaluation model (§13.3)
    template <class Store>
    class Graph
    {
    public:
        Graph(RegionTable& regions, Store& store, EvalModel model, bool mutable_index);
        ~Graph();

        Graph(const Graph&) = delete;
        Graph& operator=(const Graph&) = delete;
        Graph(Graph&&) = delete;
        Graph& operator=(Graph&&) = delete;

        // register a binding with its declared dependencies and the property it writes (§13.3)
        BindingId add_binding(PropertyId target, std::span<const PropertyId> dependencies, BindingFn<Store> fn);

        // add one dependency to a live binding without rebuilding the graph (§4 T4.1)
        MutationCost add_dependency(BindingId binding, PropertyId dependency);

        // remove one dependency from a live binding without rebuilding the graph (§4 T4.2)
        MutationCost remove_dependency(BindingId binding, PropertyId dependency);

        // remove a binding entirely (§4)
        bool remove_binding(BindingId binding);

        // replace the binding of a property with a new function and dependency set (§13.4, §18.1)
        BindingId replace_binding(PropertyId target, std::span<const PropertyId> dependencies, BindingFn<Store> fn);

        // replace a binding's function and dependency set while keeping its id (plan.md R8)
        //
        // D2's recorded mechanism is remove_binding followed by add_binding, which retains a tombstone and issues a
        // fresh id; this variant swaps the record in place, so the choice of mechanism is a measured option rather
        // than an assumption. The default is off, so every figure D2 published stays reproducible (§28.4)
        BindingId replace_binding_in_place(PropertyId target, std::span<const PropertyId> dependencies, BindingFn<Store> fn);

        // reuse the id of a removed binding for the next addition, from a free list (plan.md R8)
        //
        // default off, which keeps D2's recorded tombstone-per-removal behaviour (§28.4)
        void set_binding_reuse(bool reuse) { m_binding_reuse = reuse; }

        // rebuild the sealed half of the index after a change declared after seal (plan.md R8)
        //
        // the sealed index buckets the declared pairs once, so a dependency declared after seal is invisible to a
        // query until every bucket is rebuilt; the segmented index needs no such step. Default off, so D2's recorded
        // behaviour is unchanged and the staleness is measured rather than assumed (§28.4)
        void set_rebuild_sealed_after_mutation(bool rebuild) { m_rebuild_sealed = rebuild; }

        // report whether the index buckets still match the declared pairs (plan.md R8)
        [[nodiscard]] bool index_stale() const;

        // report how many binding records the table holds, including the dead ones (plan.md R8)
        [[nodiscard]] std::size_t binding_record_count() const { return m_bindings.size(); }

        // report how many binding records a removal cycle leaves behind (plan.md R8)
        [[nodiscard]] std::size_t live_binding_count() const;

        // report the bytes the engine's own per-binding and per-property machinery holds (plan.md R5)
        //
        // the graph allocates every model's machinery whatever model it runs, so the accessor reports the whole
        // allocation and the caller separates the model's own subset from the union (§28.2)
        [[nodiscard]] std::size_t engine_state_bytes() const;

        // report the bytes one model's own machinery holds at the current sizes, which is what carrying a second
        // engine would add (plan.md R5)
        [[nodiscard]] static std::size_t model_state_bytes(EvalModel model, std::size_t binding_count, std::size_t property_count);

        // build the topological order the pull model needs and replay deferred invalidations (§13.4)
        void seal();

        // apply queued writes and settle until the values are stable (§13.3)
        SettleStats settle();

        // take a property out of the graph so a mutation can release it (§4)
        void remove_property(PropertyId property);

        // write from an imperative assignment, which breaks the binding (§13.4)
        void assign(PropertyId property, Value value);

        // queue a write from any thread for the UI thread (§13.2)
        void post_set(PropertyId property, Value value);

        // write one animation frame value, which shadows the binding value (§13.5)
        bool set_animation(PropertyId property, Value value);

        // end the animation and either restore the shadow or re-evaluate the binding (§13.5)
        void end_animation(PropertyId property, AnimationEndMode mode);

        // give a property back to its declarative binding (§13.4)
        void rebind(PropertyId property);

        // report whether a binding may write the property right now, which an animation prevents (§13.5)
        [[nodiscard]] bool has_binding(PropertyId property) const { return m_store.has_binding(property); }

        // report whether a binding is still attached, which an animation layers over rather than breaks (§13.5)
        [[nodiscard]] bool binding_attached(PropertyId property) const { return m_store.binding_attached(property); }

        [[nodiscard]] std::size_t binding_count() const { return m_bindings.size(); }
        [[nodiscard]] bool sealed() const { return m_sealed; }
        [[nodiscard]] EvalModel model() const { return m_model; }
        [[nodiscard]] const Store& store() const { return m_store; }
        [[nodiscard]] PropertyId target_of(BindingId binding) const { return m_bindings.at(binding).target; }
        [[nodiscard]] std::span<const PropertyId> dependencies_of(BindingId binding) const { return m_bindings.at(binding).dependencies; }

        // report the binding that writes a property, which cycle detection resolves an edge through (§7)
        [[nodiscard]] BindingId writer_of(PropertyId property) const { return property < m_writer.size() ? m_writer.at(property) : INVALID_BINDING; }

        // report whether a binding still participates in evaluation (§4)
        [[nodiscard]] bool binding_alive(BindingId binding) const { return m_bindings.at(binding).alive; }

        // report whether the topological order could place every binding, so a cycle is detectable from it (§7)
        [[nodiscard]] std::size_t ordered_binding_count() const { return m_order.size(); }

        // report the memory the binding table and its dependency lists hold (§28.2)
        [[nodiscard]] std::size_t binding_bytes() const;
        [[nodiscard]] std::size_t index_bytes() const;
        [[nodiscard]] std::size_t index_slack_bytes() const;
        [[nodiscard]] std::size_t animation_side_bytes() const;
        [[nodiscard]] std::string_view index_name() const;

        // report the last mutation cost so a benchmark can attribute it (§4)
        [[nodiscard]] const MutationCost& last_mutation() const { return m_last_mutation; }

        // reset the invalidation counter before a measured window (§28.2)
        void reset_invalidated_total() { m_invalidated_total = 0; }
        [[nodiscard]] std::size_t invalidated_total() const { return m_invalidated_total; }

        // count how many times a binding ran during the last settle, to measure redundancy (§28.2)
        [[nodiscard]] std::vector<std::size_t> last_run_counts() const { return m_run_counts; }

        // reuse the settle's two plan buffers across settles, which is D2's named mitigation for the fixed
        // per-settle allocation cost (plan.md R3); the default keeps D2's recorded behaviour (§28.4)
        void set_reuse_settle_buffers(bool reuse) { m_reuse_settle_buffers = reuse; }

        // run the animation layer with a chosen shadow record (plan.md R7): Full is D2's recorded behaviour;
        // ActiveSet keeps only the record that an animation owns the target; None keeps nothing and relies
        // on invalidation alone (§28.4)
        void set_animation_shadowing(AnimationShadowing mode) { m_animation_shadowing = mode; }

        // report how many entries the shadow record holds, so a mode that claims to allocate none is
        // observable rather than asserted (§28.2)
        [[nodiscard]] std::size_t shadow_entry_count() const { return m_shadowed.size() + m_animated_records.size(); }

    private:
        struct BindingRecord
        {
            BindingFn<Store> fn = nullptr;
            PropertyId target = INVALID_PROPERTY;
            std::vector<PropertyId> dependencies;
            bool alive = true;
        };

        static void observe_write(void* user, PropertyId property, bool changed);
        void invalidate(PropertyId property);

        // schedule a binding directly, because invalidating a property only reaches the bindings that read it (§13.4)
        void mark_dirty(BindingId binding);

        // invalidate a property and schedule the binding that writes it, which is what replacement and rebind need (§13.4)
        void invalidate_and_replan(PropertyId property);

        // rebuild the sealed half of the index when the caller asked for it (plan.md R8)
        void rebuild_sealed_index();
        void run_binding(BindingId binding);

        // build the Kahn order the pull model evaluates in (§13.3)
        void build_topological_order();

        // collect the bindings a set of changed properties makes dirty (§13.3)
        void collect_dirty(std::vector<BindingId>& out);

        // pull the dependencies of a binding before running it, memoised per settle (§13.3)
        void ensure_computed(BindingId binding, std::size_t round, SettleStats& stats);

        // apply a Kahn order to the dirty bindings in one pass (§13.3)
        void settle_pull_topo(SettleStats& stats);

        RegionTable& m_regions;
        Store& m_store;
        EvalModel m_model;
        bool m_mutable_index;
        bool m_sealed = false;

        // hold the binding table, the dependency index and the evaluation model (§13.3)
        std::vector<BindingRecord> m_bindings;

        // reuse a removed binding's id for the next addition, which is off by default (plan.md R8)
        bool m_binding_reuse = false;
        std::vector<BindingId> m_free_bindings;

        // rebuild the sealed half of the index after a post-seal change, which is off by default (plan.md R8)
        bool m_rebuild_sealed = false;

        // hold either the sealed CSR or the mutable segmented index (§4)
        std::variant<SealedCsrIndex, SegmentedIndex> m_index;

        // remember the binding that writes each property, which the pull models need (§13.3)
        std::vector<BindingId> m_writer;

        // remember the Kahn order the pull model evaluates in (§13.3)
        std::vector<BindingId> m_order;
        std::vector<std::size_t> m_rank;

        // hold the properties that changed and are waiting for a settle (§13.3)
        std::vector<PropertyId> m_pending;

        // hold bindings that must run even though no property they read changed, which is what a replaced, rebound or
        // newly un-animated property needs (§13.4, §13.5)
        std::vector<BindingId> m_direct_dirty;

        // remember the properties already queued this settle, so a repeat is not scanned twice (§13.3)
        std::vector<std::uint64_t> m_pending_stamp;
        std::uint64_t m_epoch = 1;

        // remember which bindings are dirty, and which have already run this settle (§13.3)
        std::vector<std::uint64_t> m_dirty_stamp;
        std::vector<std::uint64_t> m_memo_stamp;
        std::vector<std::uint8_t> m_in_progress;
        std::vector<BindingId> m_dirty;
        std::vector<std::size_t> m_run_counts;

        // hold the binding values an active animation shadows (§13.5)
        std::unordered_map<PropertyId, Value> m_shadowed;

        // remember the per-pass dedup stamps, so a binding planned in one pass is not dropped in the next (§28.2)
        std::vector<std::uint64_t> m_plan_stamp;
        std::uint64_t m_pass_epoch = 1;

        // the settle's plan buffers, reused across settles only when the option is set (plan.md R3)
        bool m_reuse_settle_buffers = false;
        std::vector<BindingId> m_settle_candidates;
        std::vector<BindingId> m_settle_planned;

        // the animation shadow record, which defaults to D2's recorded full shadow (§28.4)
        AnimationShadowing m_animation_shadowing = AnimationShadowing::Full;

        // the active-set mode's record that an animation owns a target, kept as a sorted small vector so the
        // audit can charge its real bytes (plan.md R7)
        std::vector<PropertyId> m_animated_records;

        // hold the worker writes until the UI thread applies them (§13.2)
        std::vector<std::pair<PropertyId, Value>> m_inbox;

        // ask for one full evaluation, which sealing sets because a fresh graph holds no computed values (§13.4)
        bool m_needs_full_evaluation = false;

        // report that the settle in progress is the full evaluation, which the eager planner seeds differently (§13.4)
        bool m_full_evaluation = false;

        std::size_t m_invalidated_total = 0;
        MutationCost m_last_mutation;
    };

} // namespace albor::prototype_d2
