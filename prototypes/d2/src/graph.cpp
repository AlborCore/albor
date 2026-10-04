#include "prototypes/d2/src/graph.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace albor::prototype_d2
{

    namespace
    {

        // bound how many pending properties one settle will queue, so a runaway graph cannot grow it without bound (§28.2)
        constexpr std::size_t PENDING_RESERVE = 256;

    } // namespace

    std::string_view model_name(EvalModel model) {
        // name the model so a measurement row is unambiguous (§28.2)
        switch (model) {
        case EvalModel::EagerPush:
            return "eager_push";
        case EvalModel::PullTopo:
            return "pull_topo";
        case EvalModel::HybridPushPull:
            return "hybrid_push_pull";
        }

        // fall back to a name that cannot be mistaken for a known model
        return "unknown";
    }

    template <class Store>
    Graph<Store>::Graph(RegionTable& regions, Store& store, EvalModel model, bool mutable_index) :
        m_regions(regions), m_store(store), m_model(model), m_mutable_index(mutable_index) {
        // hold the index the caller asked for, because the two designs answer different questions (§4)
        if (mutable_index) {
            m_index = SegmentedIndex{};
        }
        else {
            m_index = SealedCsrIndex{};
        }

        // reserve room for the first bindings (§13.3)
        m_pending.reserve(PENDING_RESERVE);

        // reserve room for the writer table (§13.3)
        m_writer.resize(m_regions.property_id_count(), INVALID_BINDING);
    }

    template <class Store>
    Graph<Store>::~Graph() = default;

    template <class Store>
    BindingId Graph<Store>::add_binding(PropertyId target, std::span<const PropertyId> dependencies, BindingFn<Store> fn) {
        // prepare the record of the new binding (§13.3)
        BindingRecord record;

        // store the plain function pointer instead of a std::function (§13.3)
        record.fn = fn;

        // store the property this binding writes (§13.3)
        record.target = target;

        // copy the declared dependency list so a later mutation can edit it in place (§4)
        record.dependencies.assign(dependencies.begin(), dependencies.end());

        // append the record and hand back the dense binding id (§13.3)
        m_bindings.push_back(std::move(record));
        const BindingId binding = static_cast<BindingId>(m_bindings.size() - 1);

        // attach the binding to its property so an assignment breaks it (§13.4)
        m_store.attach_binding(target);

        // remember the binding that writes the property for the pull models (§13.3)
        if (target >= m_writer.size()) {
            m_writer.resize(static_cast<std::size_t>(target) + 1, INVALID_BINDING);
        }

        m_writer.at(target) = binding;

        // declare the dependencies with the index (§13.3)
        std::visit([binding, &dependencies](auto& index) { index.declare(binding, dependencies); }, m_index);

        // grow the per-binding bookkeeping the settle uses (§28.2)
        m_dirty_stamp.resize(m_bindings.size(), 0);
        m_memo_stamp.resize(m_bindings.size(), 0);
        m_in_progress.resize(m_bindings.size(), 0);
        m_plan_stamp.resize(m_bindings.size(), 0);
        m_run_counts.resize(m_bindings.size(), 0);

        // report the new binding id
        return binding;
    }

    template <class Store>
    MutationCost Graph<Store>::add_dependency(BindingId binding, PropertyId dependency) {
        // resolve the binding that gains a dependency (§4)
        BindingRecord& record = m_bindings.at(binding);

        // append the dependency to its declared list (§4)
        record.dependencies.push_back(dependency);

        // append the index entry. The segmented index appends into one segment (§4 T4.1); the sealed index can only
        // answer this by rebuilding every bucket, which it does internally so both designs share this path (§4 T4.4)
        m_last_mutation = std::visit([binding, dependency](auto& index) { return index.add_entry(binding, dependency); }, m_index);

        // the order no longer matches the graph, so the pull models have to rebuild it (§4)
        m_order.clear();

        // report the measured cost of the mutation
        return m_last_mutation;
    }

    template <class Store>
    MutationCost Graph<Store>::remove_dependency(BindingId binding, PropertyId dependency) {
        // resolve the binding that loses a dependency (§4)
        BindingRecord& record = m_bindings.at(binding);

        // find the declared dependency to remove (§4)
        const auto found = std::ranges::find(record.dependencies, dependency);

        // report nothing when the binding never declared it (§4)
        if (found == record.dependencies.end()) {
            return m_last_mutation = MutationCost{};
        }

        // erase the declared dependency (§4)
        record.dependencies.erase(found);

        // remove the index entry. The segmented index shifts one segment (§4 T4.2); the sealed index rebuilds (§4 T4.4)
        m_last_mutation = std::visit([binding, dependency](auto& index) { return index.remove_entry(binding, dependency); }, m_index);

        // the order no longer matches the graph, so the pull models have to rebuild it (§4)
        m_order.clear();

        // report the measured cost of the mutation
        return m_last_mutation;
    }

    template <class Store>
    bool Graph<Store>::remove_binding(BindingId binding) {
        // resolve the binding that goes away (§4)
        BindingRecord& record = m_bindings.at(binding);

        // report that a live binding is required (§4 T4.7)
        if (!record.alive) {
            return false;
        }

        // drop every index entry the binding declared, which is one removal per dependency (§4)
        for (const PropertyId dependency : record.dependencies) {
            std::visit([binding, dependency](auto& index) { static_cast<void>(index.remove_entry(binding, dependency)); }, m_index);
        }

        // mark the binding dead so a settle never plans it again (§4 T4.7)
        record.alive = false;
        record.fn = nullptr;

        // release the property the binding wrote, so a pull never reaches a dead binding (§4)
        if (record.target < m_writer.size() && m_writer.at(record.target) == binding) {
            m_writer.at(record.target) = INVALID_BINDING;
        }

        // the order no longer matches the graph, so the pull models have to rebuild it (§4)
        m_order.clear();

        // report that the binding is gone
        return true;
    }

    template <class Store>
    BindingId Graph<Store>::replace_binding(PropertyId target, std::span<const PropertyId> dependencies, BindingFn<Store> fn) {
        // remember whether the property still followed its binding, because §13.4 makes that decide the outcome
        const bool was_bound = m_store.has_binding(target);

        // drop the binding that wrote the property, if one did (§18.1)
        if (target < m_writer.size() && m_writer.at(target) != INVALID_BINDING) {
            remove_binding(m_writer.at(target));
        }

        // install the new binding function and dependency set (§13.4)
        const BindingId binding = add_binding(target, dependencies, fn);

        if (was_bound) {
            // a bound property recomputes from its new dependencies straight away (§13.4)
            invalidate_and_replan(target);
        }
        else {
            // an imperatively overridden property keeps its override, so put the imperative owner back (§13.4)
            m_store.set_source(target, WriteSource::Imperative);
        }

        // report the new binding id
        return binding;
    }

    template <class Store>
    void Graph<Store>::seal() {
        // build the index buckets now that every binding and property is known (§13.4)
        std::visit([this](auto& index) { index.finish(m_regions.property_id_count()); }, m_index);

        // build the Kahn order the pull model evaluates in (§13.3)
        build_topological_order();

        // allocate the pending property stamps (§13.3)
        m_pending_stamp.assign(m_regions.property_id_count(), 0);

        // ask for one full evaluation. Dirtying the binding targets is not enough, because the index maps a property
        // to the bindings that read it, so a target with no readers schedules nothing and a freshly sealed graph would
        // hold whatever the store was seeded with until something changed (§13.4)
        m_needs_full_evaluation = true;

        // record that the graph is built (§13.4)
        m_sealed = true;
    }

    template <class Store>
    void Graph<Store>::build_topological_order() {
        // start from an empty order (§13.3)
        m_order.clear();

        // the order only means something once every binding is declared (§13.4)
        if (m_bindings.empty()) {
            return;
        }

        // count the incoming edges of every binding (§13.3)
        std::vector<std::size_t> indegree(m_bindings.size(), 0);

        // build the forward edges: the binding that writes a property runs before every binding that reads it (§13.3)
        std::vector<std::vector<BindingId>> edges(m_bindings.size());

        // walk every live binding and resolve the producer of each declared dependency (§13.3)
        for (const BindingId consumer : std::views::iota(BindingId{0}, static_cast<BindingId>(m_bindings.size()))) {
            // skip a binding that was removed (§4)
            if (!m_bindings.at(consumer).alive) {
                continue;
            }

            for (const PropertyId dependency : m_bindings.at(consumer).dependencies) {
                // a property outside the writer table has no producer (§4)
                if (dependency >= m_writer.size()) {
                    continue;
                }

                // resolve the binding that writes the dependency (§13.3)
                const BindingId producer = m_writer.at(dependency);

                // nothing orders a read of a directly written property (§13.3)
                if (producer == INVALID_BINDING || producer == consumer || !m_bindings.at(producer).alive) {
                    continue;
                }

                // record the edge and count the incoming degree (§13.3)
                edges.at(producer).push_back(consumer);
                indegree.at(consumer)++;
            }
        }

        // collect the bindings with no incoming edge to start the Kahn walk (§13.3)
        std::vector<BindingId> ready;

        for (const BindingId binding : std::views::iota(BindingId{0}, static_cast<BindingId>(m_bindings.size()))) {
            if (m_bindings.at(binding).alive && indegree.at(binding) == 0) {
                ready.push_back(binding);
            }
        }

        // walk the graph in Kahn order (§13.3)
        while (!ready.empty()) {
            // take the next binding with no unmet dependency (§13.3)
            const BindingId binding = ready.back();
            ready.pop_back();

            // append it to the order (§13.3)
            m_order.push_back(binding);

            // release the edges it satisfied (§13.3)
            for (const BindingId consumer : edges.at(binding)) {
                // count down the unmet dependencies (§13.3)
                if (--indegree.at(consumer) == 0) {
                    // the consumer may now run (§13.3)
                    ready.push_back(consumer);
                }
            }
        }
    }

    template <class Store>
    void Graph<Store>::remove_property(PropertyId property) {
        // forget the binding that wrote it, so no pull reaches a property that is gone (§4)
        if (property < m_writer.size()) {
            m_writer.at(property) = INVALID_BINDING;
        }

        // release the payload the property held and hand it back to generated initialization (§13.1)
        m_store.write(property, Value(), WriteSource::GeneratedInit);
    }

    template <class Store>
    void Graph<Store>::invalidate(PropertyId property) {
        // count the invalidation for the throughput measurement (§28.2)
        m_invalidated_total++;

        // a property outside the id space cannot be scheduled (§4)
        if (property >= m_pending_stamp.size()) {
            return;
        }

        // remember each changed property once per pass, so one property is not scanned twice in a pass (§13.3)
        if (m_pending_stamp.at(property) == m_pass_epoch) {
            return;
        }

        // queue the changed property for the pass that is being built (§13.3)
        m_pending_stamp.at(property) = m_pass_epoch;
        m_pending.push_back(property);
    }

    template <class Store>
    void Graph<Store>::mark_dirty(BindingId binding) {
        // skip a binding that no longer exists (§4)
        if (binding >= m_bindings.size() || !m_bindings.at(binding).alive) {
            return;
        }

        // schedule the binding for the pass that is being built (§13.4)
        m_direct_dirty.push_back(binding);
    }

    template <class Store>
    void Graph<Store>::invalidate_and_replan(PropertyId property) {
        // schedule the binding that writes the property, because the index only maps a property to its readers (§13.4)
        if (property < m_writer.size()) {
            mark_dirty(m_writer.at(property));
        }

        // invalidate the property so its readers are scheduled as usual (§13.4)
        invalidate(property);
    }

    template <class Store>
    void Graph<Store>::observe_write(void* user, PropertyId property, bool changed) {
        // skip a write that left the value equal (§13.3)
        if (!changed) {
            return;
        }

        // invalidate the readers of the target so a chain settles (§13.3)
        static_cast<Graph<Store>*>(user)->invalidate(property);
    }

    template <class Store>
    void Graph<Store>::run_binding(BindingId binding) {
        // resolve the record that describes the binding (§13.3)
        const BindingRecord& record = m_bindings.at(binding);

        // skip a binding whose property an explicit write owns (§13.4)
        if (!m_store.has_binding(record.target)) {
            return;
        }

        // build the plain context the binding function receives (§13.3)
        BindingContext<Store> context(m_store, record.target, binding, std::span<const PropertyId>(record.dependencies), &Graph<Store>::observe_write, this);

        // run the binding, which writes its target through the context (§13.3)
        record.fn(context);

        // count the evaluation so redundancy can be measured (§28.2)
        if (binding < m_run_counts.size()) {
            m_run_counts.at(binding)++;
        }
    }

    template <class Store>
    void Graph<Store>::collect_dirty(std::vector<BindingId>& candidates) {
        // walk every property that changed since the last pass (§13.3)
        for (const PropertyId property : m_pending) {
            // visit every binding that reads the changed property (§13.3)
            const std::span<const BindingId> readers = std::visit([property](const auto& index) { return index.dependents(property); }, m_index);

            for (const BindingId binding : readers) {
                // count every candidate, including one another changed property already reached (§28.2)
                candidates.push_back(binding);

                // deduplicate a binding reached by several changed properties (§13.3)
                if (binding >= m_dirty_stamp.size() || m_dirty_stamp.at(binding) == m_epoch) {
                    continue;
                }

                // remember the binding as dirty for this settle (§13.3)
                m_dirty_stamp.at(binding) = m_epoch;

                // record it in the settle-level dirty set the pull models walk (§13.3)
                m_dirty.push_back(binding);
            }
        }

        // add the bindings that were scheduled directly, which no changed property reaches (§13.4)
        for (const BindingId binding : m_direct_dirty) {
            candidates.push_back(binding);

            // deduplicate against a binding another changed property already reached (§13.3)
            if (binding >= m_dirty_stamp.size() || m_dirty_stamp.at(binding) == m_epoch) {
                continue;
            }

            m_dirty_stamp.at(binding) = m_epoch;
            m_dirty.push_back(binding);
        }

        // forget the scheduled bindings for the next pass (§13.4)
        m_direct_dirty.clear();

        // forget the changed properties, and start a new pass epoch so the next pass queues them again (§13.3)
        m_pending.clear();
        m_pass_epoch++;
    }

    template <class Store>
    void Graph<Store>::ensure_computed(BindingId binding, std::size_t round, SettleStats& stats) {
        // a binding already computed in this round needs no second evaluation (§13.3)
        if (m_memo_stamp.at(binding) == round) {
            return;
        }

        // re-entering a binding means the graph holds a cycle (§7)
        if (m_in_progress.at(binding) != 0) {
            stats.cycle_detected = true;
            return;
        }

        // mark the binding as being computed so a cycle can be seen (§7)
        m_in_progress.at(binding) = 1;

        // pull the dependencies first so this binding sees their newest values (§13.3)
        for (const PropertyId dependency : m_bindings.at(binding).dependencies) {
            // a property outside the writer table cannot be bound (§4)
            if (dependency >= m_writer.size()) {
                continue;
            }

            // resolve the binding that writes this dependency (§13.3)
            const BindingId producer = m_writer.at(dependency);

            // nothing to pull when the dependency is written directly or its binding is gone (§13.3)
            if (producer == INVALID_BINDING || producer >= m_memo_stamp.size() || !m_bindings.at(producer).alive) {
                continue;
            }

            // only a binding that is dirty this settle has to be recomputed (§13.3)
            if (m_dirty_stamp.at(producer) != m_epoch) {
                continue;
            }

            // compute the dependency before running the dependent (§13.3)
            ensure_computed(producer, round, stats);
        }

        // clear the in-progress marker before running (§7)
        m_in_progress.at(binding) = 0;

        // count the evaluation (§28.2)
        stats.bindings_evaluated++;

        // mark the binding computed so no second evaluation happens this settle (§13.3)
        m_memo_stamp.at(binding) = round;

        // run the binding (§13.3)
        run_binding(binding);
    }

    template <class Store>
    void Graph<Store>::settle_pull_topo(SettleStats& stats) {
        // count one pass, because a Kahn order needs no iteration to a fixpoint (§13.3)
        stats.passes = 1;

        // walk the bindings in the order computed at seal (§13.3)
        for (const BindingId binding : m_order) {
            // skip a binding no changed property reached (§13.3)
            if (m_dirty_stamp.at(binding) != m_epoch) {
                continue;
            }

            // skip a binding that no longer exists (§4)
            if (!m_bindings.at(binding).alive) {
                continue;
            }

            // count the evaluation (§28.2)
            stats.bindings_evaluated++;

            // run the binding in dependency order (§13.3)
            run_binding(binding);

            // absorb what this binding wrote, so a binding later in the order sees itself as dirty and still runs in
            // this same pass. Without this the topological order is useless for a chain, because nothing marks the
            // downstream bindings dirty before the pass reaches them (§13.3)
            std::vector<BindingId> produced;
            collect_dirty(produced);
            stats.candidates_considered += produced.size();
        }
    }

    template <class Store>
    SettleStats Graph<Store>::settle() {
        // accumulate what the settle did for the measurements (§28.2)
        SettleStats stats;

        // remember the invalidation count before the worker writes are applied (§28.2)
        const std::size_t invalidated_before = m_invalidated_total;

        // advance the settle epoch so the stamps of the previous settle stop matching (§13.3)
        m_epoch++;

        // forget which bindings ran in the previous settle (§28.2)
        std::ranges::fill(m_run_counts, 0);

        // apply the worker writes the inbox holds, on the UI thread (§13.2)
        for (const auto& [property, value] : m_inbox) {
            // an explicit write wins over an active animation, so the animation is dropped (§13.4, §13.5)
            m_shadowed.erase(property);
            const auto record = std::find(m_animated_records.begin(), m_animated_records.end(), property);
            if (record != m_animated_records.end()) {
                m_animated_records.erase(record);
            }

            // treat a queued worker write as imperative and break the binding (§13.2, §13.4)
            if (m_store.write(property, value, WriteSource::Imperative)) {
                invalidate(property);
            }
        }

        // forget the drained queue (§13.2)
        m_inbox.clear();

        // a sealed graph has to evaluate every binding once, because nothing has computed a value yet (§13.4)
        if (m_needs_full_evaluation) {
            for (BindingId binding = 0; binding < m_bindings.size(); binding++) {
                if (!m_bindings.at(binding).alive) {
                    continue;
                }

                // mark every binding dirty for this settle (§13.4)
                m_dirty_stamp.at(binding) = m_epoch;
                m_dirty.push_back(binding);
            }

            // report to the eager planner that this settle plans from the dirty set rather than candidates (§13.4)
            m_full_evaluation = true;

            // only the first settle after a seal needs a full evaluation (§13.4)
            m_needs_full_evaluation = false;
        }

        // dedup the dirty set once for the whole settle, whichever model runs (§13.3)
        //
        // the labelled variant reuses member buffers across settles, so a settle allocates nothing per pass
        // (plan.md R3); the default keeps D2's per-settle locals, which is what D2 recorded (§28.4)
        std::vector<BindingId> local_candidates;
        std::vector<BindingId> local_planned;
        std::vector<BindingId>& candidates = m_reuse_settle_buffers ? m_settle_candidates : local_candidates;
        std::vector<BindingId>& planned = m_reuse_settle_buffers ? m_settle_planned : local_planned;
        collect_dirty(candidates);

        // count the candidates the index produced, including the duplicates (§28.2)
        stats.candidates_considered = candidates.size();

        // run the model the graph was configured with (§13.3)
        switch (m_model) {
        case EvalModel::EagerPush: {
            // the full evaluation seeds from the dirty set, because no candidate reaches a binding nothing reads (§13.4)
            bool seeding_from_dirty_set = m_full_evaluation;

            // build the work list for one pass (§13.3)
            const auto plan_pass = [&](const std::vector<BindingId>& seeds) {
                planned.clear();

                for (const BindingId binding : seeds) {
                    // skip a binding that no longer exists (§4)
                    if (binding >= m_bindings.size() || !m_bindings.at(binding).alive) {
                        continue;
                    }

                    // deduplicate a binding reached by several changed properties in this pass (§13.3)
                    if (m_plan_stamp.at(binding) == m_pass_epoch) {
                        continue;
                    }

                    // plan the binding for this pass (§13.3)
                    m_plan_stamp.at(binding) = m_pass_epoch;
                    planned.push_back(binding);
                }
            };

            // seed the first pass (§13.4)
            plan_pass(seeding_from_dirty_set ? m_dirty : candidates);

            // evaluate pass after pass until no binding is left to run (§13.3)
            while (!planned.empty()) {
                // stop a cycle that never settles instead of hanging the UI thread (§28.2)
                if (stats.passes >= MAX_SETTLE_PASSES) {
                    stats.cycle_budget_exhausted = true;
                    break;
                }

                // count the pass (§28.2)
                stats.passes++;

                // run every binding of this pass in the order it was planned (§13.3)
                for (const BindingId binding : planned) {
                    // count the evaluation (§28.2)
                    stats.bindings_evaluated++;

                    // run the binding (§13.3)
                    run_binding(binding);
                }

                // collect the bindings the writes of this pass made dirty (§13.3)
                candidates.clear();
                collect_dirty(candidates);

                // count the candidates of this pass (§28.2)
                stats.candidates_considered += candidates.size();

                // every later pass plans from the candidates this pass produced (§13.3)
                seeding_from_dirty_set = false;
                plan_pass(candidates);
            }

            break;
        }

        case EvalModel::PullTopo:
            settle_pull_topo(stats);
            break;

        case EvalModel::HybridPushPull: {
            // the pull needs one pass and one memo epoch (§13.3)
            stats.passes = 1;

            // walk the dirty set with a watermark, because a binding that runs can dirty a binding the walk has not
            // reached yet. Each dirty binding is still evaluated exactly once, because the memo epoch stops a second
            // evaluation, so this is bookkeeping rather than another pass over a fixpoint (§13.3)
            std::size_t processed = 0;

            while (true) {
                // evaluate every dirty binding the walk has not reached yet, pulling its dependencies first (§13.3)
                while (processed < m_dirty.size()) {
                    const BindingId binding = m_dirty.at(processed++);

                    // skip a binding that no longer exists (§4)
                    if (binding >= m_bindings.size() || !m_bindings.at(binding).alive) {
                        continue;
                    }

                    ensure_computed(binding, m_epoch, stats);
                }

                // absorb what the evaluations wrote, so the bindings they dirtied are evaluated in this same settle (§13.3)
                std::vector<BindingId> produced;
                collect_dirty(produced);
                stats.candidates_considered += produced.size();

                // stop when nothing new became dirty (§13.3)
                if (m_dirty.size() == processed) {
                    break;
                }
            }

            break;
        }
        }

        // count a second evaluation of a binding as redundancy, which is what a reconvergent graph produces (§28.2)
        for (const std::size_t runs : m_run_counts) {
            if (runs > 1) {
                stats.redundant_evaluations += runs - 1;
            }
        }

        // forget the dirty set for the next settle (§13.3)
        m_dirty.clear();

        // a later settle plans from candidates again (§13.4)
        m_full_evaluation = false;

        // report how many properties this settle invalidated (§28.2)
        stats.invalidations = m_invalidated_total - invalidated_before;

        // hand back the measurement of this settle (§28.2)
        return stats;
    }

    template <class Store>
    void Graph<Store>::assign(PropertyId property, Value value) {
        // an explicit write wins over an active animation, so the animation is dropped (§13.4, §13.5)
        m_shadowed.erase(property);
        const auto record = std::find(m_animated_records.begin(), m_animated_records.end(), property);
        if (record != m_animated_records.end()) {
            m_animated_records.erase(record);
        }

        // take ownership of the property and break its binding (§13.4)
        if (m_store.write(property, std::move(value), WriteSource::Imperative)) {
            invalidate(property);
        }
    }

    template <class Store>
    void Graph<Store>::post_set(PropertyId property, Value value) {
        // queue the write so the UI thread applies it in settle (§13.2)
        m_inbox.emplace_back(property, std::move(value));
    }

    template <class Store>
    bool Graph<Store>::set_animation(PropertyId property, Value value) {
        // remember the binding value once, so the animation can be undone (§13.5)
        //
        // the active-set mode records only that an animation owns the target (plan.md R7); the none mode
        // records nothing, which is the naive elimination the audit disqualifies or keeps (§28.4)
        if (m_store.source_of(property) != WriteSource::Animation) {
            if (m_animation_shadowing == AnimationShadowing::Full) {
                m_shadowed.insert_or_assign(property, m_store.read(property));
            }
            else if (m_animation_shadowing == AnimationShadowing::ActiveSet) {
                if (std::find(m_animated_records.begin(), m_animated_records.end(), property) == m_animated_records.end()) {
                    m_animated_records.push_back(property);
                }
            }
        }

        // publish the animated value without breaking the binding (§13.5)
        const bool changed = m_store.write(property, std::move(value), WriteSource::Animation);

        // schedule the readers only when the animated value changed (§13.5)
        if (changed) {
            invalidate(property);
        }

        // report the change the frame scheduler redraws for (§13.5)
        return changed;
    }

    template <class Store>
    void Graph<Store>::end_animation(PropertyId property, AnimationEndMode mode) {
        // the naive elimination has no record to consult, so it hands the target back and lets the binding
        // re-evaluate from its current dependencies, whatever mode was requested (plan.md R7)
        if (m_animation_shadowing == AnimationShadowing::None) {
            m_store.rebind(property);
            invalidate_and_replan(property);
            return;
        }

        // the active-set mode's record is presence alone, so there is no value to put back (plan.md R7)
        if (m_animation_shadowing == AnimationShadowing::ActiveSet) {
            const auto record = std::find(m_animated_records.begin(), m_animated_records.end(), property);

            // an animation that never ran owns nothing to undo (§13.5)
            if (record == m_animated_records.end()) {
                return;
            }

            // forget the record, because after this point the binding owns the value again (§13.5)
            m_animated_records.erase(record);

            // hand the property back to its binding owner, so a binding write is no longer dropped (§13.5)
            m_store.rebind(property);

            // schedule the binding so it recomputes from its current dependencies (§13.5)
            invalidate_and_replan(property);
            return;
        }

        // do nothing when no animation owned the property (§13.5)
        const auto shadow = m_shadowed.find(property);

        // an animation that never ran owns nothing to undo (§13.5)
        if (shadow == m_shadowed.end()) {
            return;
        }

        // keep the shadowed value so the restoring modes can put it back (§13.5)
        Value shadowed = shadow->second;

        // forget the shadowed value, because after this point the binding owns the value again (§13.5)
        m_shadowed.erase(shadow);

        // hand the property back to its binding owner, so a binding write is no longer dropped (§13.5)
        m_store.rebind(property);

        if (mode == AnimationEndMode::RestoreShadow || mode == AnimationEndMode::RestoreNoInvalidate) {
            // put the value the animation shadowed back, which is what Prototype D's store does (§13.5)
            m_store.write(property, shadowed, WriteSource::Binding);
        }

        if (mode == AnimationEndMode::RestoreNoInvalidate) {
            // stop here on purpose, so the measurement can show what the missing invalidate costs (§13.5)
            return;
        }

        // schedule the binding so it recomputes from its current dependencies (§13.5)
        invalidate_and_replan(property);
    }

    template <class Store>
    void Graph<Store>::rebind(PropertyId property) {
        // an explicit rebind also ends the animation (§13.5)
        m_shadowed.erase(property);
        const auto record = std::find(m_animated_records.begin(), m_animated_records.end(), property);
        if (record != m_animated_records.end()) {
            m_animated_records.erase(record);
        }

        // hand the property back to its declarative binding (§13.4)
        m_store.rebind(property);

        // schedule the binding that owns the property and its readers (§13.4)
        invalidate_and_replan(property);
    }

    template <class Store>
    std::size_t Graph<Store>::binding_bytes() const {
        // charge the binding records, including the per-binding dependency vector header (§28.2)
        std::size_t total = m_bindings.size() * (sizeof(BindingFn<Store>) + sizeof(PropertyId) + sizeof(std::vector<PropertyId>) + sizeof(bool));

        // charge the declared dependency payloads (§4)
        for (const BindingRecord& record : m_bindings) {
            total += record.dependencies.capacity() * sizeof(PropertyId);
        }

        // charge the writer table the pull models need (§13.3)
        total += m_writer.capacity() * sizeof(BindingId);

        // report the binding table bytes
        return total;
    }

    template <class Store>
    std::size_t Graph<Store>::index_bytes() const {
        return std::visit([](const auto& index) { return index.index_bytes(); }, m_index);
    }

    template <class Store>
    std::size_t Graph<Store>::index_slack_bytes() const {
        return std::visit([](const auto& index) { return index.slack_entries() * sizeof(BindingId); }, m_index);
    }

    template <class Store>
    std::size_t Graph<Store>::animation_side_bytes() const {
        // charge the side table entry of every animated property (§28.2)
        std::size_t total = m_shadowed.size() * (sizeof(std::pair<const PropertyId, Value>) + sizeof(Value*) * 2);

        // charge the presence record the active-set mode keeps (plan.md R7)
        total += m_animated_records.capacity() * sizeof(PropertyId);

        // add the payload bytes of every shadowed value (§13.5)
        for (const auto& [property, value] : m_shadowed) {
            // the property id is only read to keep the loop explicit
            static_cast<void>(property);

            total += value.known_heap_bytes();
        }

        // report the side table bytes
        return total;
    }

    template <class Store>
    std::string_view Graph<Store>::index_name() const {
        return std::visit([](const auto& index) { return index.name(); }, m_index);
    }

    // instantiate the graph over both storage candidates, because the storage comparison needs the engine too (§1)
    template class Graph<ErasedStore>;
    template class Graph<TypedStore>;

} // namespace albor::prototype_d2
