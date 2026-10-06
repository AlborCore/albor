// Prototype D3 R8 — binding replacement and removal semantics, as variants (F2.8)
//
// plan.md was committed before this program existed (§28.4). Measurement code; not a runtime module.
//
// §13.4 and §18.1 fix what ReplaceBinding means; nothing fixes the mechanism. A live reload replaces bindings on
// every keystroke and a virtualized list removes and re-adds them per recycled row, so the mechanism decides
// whether the binding table and the index grow without bound, and at what price per edit

#include "prototypes/d2/src/graph.h"
#include "prototypes/d2/src/storage.h"
#include "prototypes/d2/src/workloads.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace albor::prototype_d3
{
    namespace
    {

        // T3.8.1 a reload that retains a record per keystroke is an unbounded leak wearing a mechanism's clothes
        constexpr double T3_8_1_TABLE_GROWTH = 0.25;

        // T3.8.2 a reload edits a handful of bindings, so each must stay far inside a frame at full scale
        constexpr double T3_8_2_REPLACE_US = 2.0;

        // T3.8.3 the sealed index must not accumulate across its rebuilds
        constexpr double T3_8_3_INDEX_GROWTH = 0.10;

        // the committed reload cycle count for one property (§28.4)
        constexpr std::size_t REPLACE_CYCLES = 1000;

        // the committed add/remove cycle count and its width (§28.4)
        constexpr std::size_t REMOVE_CYCLES = 1000;
        constexpr std::size_t REMOVE_WIDTH = 100;

        // the committed scale for the replacement p99, the same one R2 and R4 account at (§28.4)
        constexpr std::size_t SCALE_BINDINGS = 11000;

        // the committed number of timed replacements at scale (§28.4)
        constexpr std::size_t TIMED_REPLACEMENTS = 200;

        // the count of allocations the harness made, which only exists while the counter is armed (§28.2)
        std::size_t g_allocations = 0;
        bool g_count_allocations = false;

        // count one allocation while the counter is armed (§28.2)
        void count_allocation() {
            if (g_count_allocations) {
                g_allocations++;
            }
        }

        struct Verdict
        {
            std::size_t passed = 0;
            std::size_t failed = 0;
        };

        void check(Verdict& verdict, std::string_view id, const std::string& description, const bool passed, const std::string& detail) {
            std::printf("  %-8s %-46s %-4s %s\n", std::string(id).c_str(), description.c_str(), passed ? "PASS" : "FAIL", detail.c_str());

            if (passed) {
                verdict.passed++;
            }
            else {
                verdict.failed++;
            }
        }

        void record(std::string_view id, const std::string& description, const std::string& detail) { std::printf("  %-8s %-46s %-4s %s\n", std::string(id).c_str(), description.c_str(), "REC", detail.c_str()); }

        std::string format(const double value, const int precision = 2) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
            return std::string(buffer);
        }

        double percentile(std::vector<double> samples, const double fraction) {
            // report nothing when the variant measured no sample (§28.2)
            if (samples.empty()) {
                return 0.0;
            }

            // sort the samples so the fraction becomes an index (§28.2)
            std::ranges::sort(samples);

            // clamp the index so a fraction of one stays inside the samples (§28.2)
            const std::size_t index = std::min(samples.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(samples.size())));

            // hand back the sample at that position (§28.2)
            return samples.at(index);
        }

        using Clock = std::chrono::steady_clock;

        double to_us(Clock::duration elapsed) { return std::chrono::duration<double, std::micro>(elapsed).count(); }

        // the three replacement mechanisms the plan commits (§28.4)
        enum class ReplaceVariant : std::uint8_t
        {
            RemoveThenAdd, // D2's recorded mechanism: a tombstone plus a fresh id
            InPlace, // the id and the record survive, the function and dependencies are swapped
            InPlaceIdReuse // in place, and a freed id is reused by the next addition
        };

        // the three removal mechanisms the plan commits (§28.4)
        enum class RemoveVariant : std::uint8_t
        {
            Tombstone, // D2's recorded mechanism: the dead record is retained
            TombstoneIdReuse, // as above, plus a free list
            TombstoneIdReuseRelease // as above, and the store releases the removed property's payload
        };

        std::string replace_name(const ReplaceVariant variant) {
            // name the mechanism so a row is unambiguous (§28.2)
            switch (variant) {
            case ReplaceVariant::RemoveThenAdd:
                return "remove_then_add";
            case ReplaceVariant::InPlace:
                return "in_place";
            case ReplaceVariant::InPlaceIdReuse:
                return "in_place_id_reuse";
            }

            return "unknown";
        }

        std::string remove_name(const RemoveVariant variant) {
            // name the mechanism so a row is unambiguous (§28.2)
            switch (variant) {
            case RemoveVariant::Tombstone:
                return "tombstone";
            case RemoveVariant::TombstoneIdReuse:
                return "tombstone_id_reuse";
            case RemoveVariant::TombstoneIdReuseRelease:
                return "tombstone_id_reuse_release";
            }

            return "unknown";
        }

        // the binding a replacement installs first, and the one it swaps to (§18.1)
        template <class Store>
        void first_binding(albor::prototype_d2::BindingContext<Store>& context) {
            context.write_int(context.read_int(context.declared().front()) + 1);
        }

        template <class Store>
        void second_binding(albor::prototype_d2::BindingContext<Store>& context) {
            context.write_int(context.read_int(context.declared().front()) * 2);
        }

        // hold a graph over a plain scalar schema, which is what a reload edits (§13.1)
        template <class Store>
        struct Harness
        {
            albor::prototype_d2::Scene<Store> scene;
            albor::prototype_d2::SchemaId schema = 0;
            std::vector<albor::prototype_d2::PropertyId> properties;
            std::vector<albor::prototype_d2::PropertyId> sources;

            Harness(const albor::prototype_d2::EvalModel model, const bool mutable_index) :
                scene(model, mutable_index) {
                // one two-property component: a source nobody writes and a target a binding writes (§13.3)
                schema = scene.regions().add_schema("Pair", albor::prototype_d2::InstanceKind::Component, std::vector<albor::prototype_d2::ValueType>{albor::prototype_d2::ValueType::Int, albor::prototype_d2::ValueType::Int});
            }

            // create one pair and bind its target to its source (§13.3)
            albor::prototype_d2::PropertyId add_pair(albor::prototype_d2::BindingFn<Store> fn) {
                const albor::prototype_d2::InstanceId instance = scene.store().create(schema);
                const albor::prototype_d2::PropertyId source = scene.at(instance, 0);
                const albor::prototype_d2::PropertyId target = scene.at(instance, 1);

                static_cast<void>(scene.store().write(source, albor::prototype_d2::Value(static_cast<std::int64_t>(1)), albor::prototype_d2::WriteSource::GeneratedInit));
                static_cast<void>(scene.store().write(target, albor::prototype_d2::Value(static_cast<std::int64_t>(0)), albor::prototype_d2::WriteSource::GeneratedInit));
                static_cast<void>(scene.graph().add_binding(target, std::vector<albor::prototype_d2::PropertyId>{source}, fn));
                properties.push_back(target);
                sources.push_back(source);
                return target;
            }
        };

        // apply the replacement mechanism a variant names, which is the whole difference between the three (§18.1)
        template <class Store>
        albor::prototype_d2::BindingId replace(Harness<Store>& harness, const ReplaceVariant variant, const albor::prototype_d2::PropertyId target, const albor::prototype_d2::PropertyId source, albor::prototype_d2::BindingFn<Store> fn) {
            // the declared dependency set every variant installs (§13.3)
            const std::vector<albor::prototype_d2::PropertyId> dependencies{source};

            // D2's recorded mechanism drops the record and issues a fresh id (§18.1)
            if (variant == ReplaceVariant::RemoveThenAdd) {
                return harness.scene.graph().replace_binding(target, dependencies, fn);
            }

            // the other two keep the id and swap the body in place (§28.4)
            return harness.scene.graph().replace_binding_in_place(target, dependencies, fn);
        }

        // arm the id-reuse option the variants that need it (§28.4)
        template <class Store>
        void configure(Harness<Store>& harness, const ReplaceVariant replace_variant, const RemoveVariant remove_variant) {
            // the sealed index needs a rebuild after every post-seal change, or its buckets cannot see the new edges
            harness.scene.graph().set_rebuild_sealed_after_mutation(true);

            // id reuse is what separates the two reusing variants from D2's recorded behaviour (§28.4)
            const bool reuse = replace_variant == ReplaceVariant::InPlaceIdReuse || remove_variant == RemoveVariant::TombstoneIdReuse || remove_variant == RemoveVariant::TombstoneIdReuseRelease;
            harness.scene.graph().set_binding_reuse(reuse);
        }

        // hold one variant's cost over the committed reload cycles (§28.2)
        struct ReplaceCost
        {
            std::string name;
            std::string index;
            std::size_t records_before = 0;
            std::size_t records_after = 0;
            std::size_t binding_bytes_before = 0;
            std::size_t binding_bytes_after = 0;
            std::size_t index_bytes_before = 0;
            std::size_t index_bytes_after = 0;
            std::size_t allocations = 0;
            double p99_us = 0.0;
        };

        // run the committed replacement cycles for one variant on one index, and report what it cost (§28.4)
        template <class Store>
        ReplaceCost measure_replace(const ReplaceVariant variant, const bool mutable_index) {
            ReplaceCost cost;
            cost.name = replace_name(variant);
            cost.index = mutable_index ? "segmented" : "sealed_csr";

            Harness<Store> harness(albor::prototype_d2::EvalModel::EagerPush, mutable_index);
            configure(harness, variant, RemoveVariant::Tombstone);

            // one bound property is what a reload edits: the component whose declared binding changed (§18.3)
            const albor::prototype_d2::PropertyId target = harness.add_pair(&first_binding<Store>);
            const albor::prototype_d2::PropertyId source = harness.sources.front();

            harness.scene.graph().seal();
            static_cast<void>(harness.scene.graph().settle());

            cost.records_before = harness.scene.graph().binding_record_count();
            cost.binding_bytes_before = harness.scene.graph().binding_bytes();
            cost.index_bytes_before = harness.scene.graph().index_bytes();

            // count the allocations the cycles make, which is the price per edit in objects rather than bytes (§28.2)
            g_allocations = 0;
            g_count_allocations = true;

            std::vector<double> samples;
            samples.reserve(REPLACE_CYCLES);

            for (std::size_t cycle = 0; cycle < REPLACE_CYCLES; cycle++) {
                // alternate between the two declared bodies, which is what a reload of a changed expression does (§18.1)
                const albor::prototype_d2::BindingFn<Store> fn = cycle % 2 == 0 ? &second_binding<Store> : &first_binding<Store>;

                const Clock::time_point start = Clock::now();
                static_cast<void>(replace(harness, variant, target, source, fn));
                samples.push_back(to_us(Clock::now() - start));
            }

            g_count_allocations = false;
            cost.allocations = g_allocations;
            cost.p99_us = percentile(samples, 0.99);

            cost.records_after = harness.scene.graph().binding_record_count();
            cost.binding_bytes_after = harness.scene.graph().binding_bytes();
            cost.index_bytes_after = harness.scene.graph().index_bytes();

            // hand back what this mechanism cost (§28.2)
            return cost;
        }

        // run the committed add and remove cycles for one variant on one index (§28.4)
        //
        // the batch is a pool of instances created before seal and recycled every cycle, because a sealed index
        // cannot bucket a property that does not exist when finish() runs, and recycling a row reuses its instance
        // rather than creating one (§20.2)
        template <class Store>
        ReplaceCost measure_remove(const RemoveVariant variant, const bool mutable_index) {
            ReplaceCost cost;
            cost.name = remove_name(variant);
            cost.index = mutable_index ? "segmented" : "sealed_csr";

            Harness<Store> harness(albor::prototype_d2::EvalModel::EagerPush, mutable_index);
            harness.scene.graph().set_rebuild_sealed_after_mutation(true);
            harness.scene.graph().set_binding_reuse(variant != RemoveVariant::Tombstone);

            // build the pool before seal, so every property the index will ever see already exists (§4)
            std::vector<albor::prototype_d2::PropertyId> targets;
            std::vector<albor::prototype_d2::PropertyId> sources;

            for (std::size_t index = 0; index < REMOVE_WIDTH; index++) {
                targets.push_back(harness.add_pair(&first_binding<Store>));
                sources.push_back(harness.sources.back());
            }

            harness.scene.graph().seal();
            static_cast<void>(harness.scene.graph().settle());

            cost.records_before = harness.scene.graph().binding_record_count();
            cost.binding_bytes_before = harness.scene.graph().binding_bytes();
            cost.index_bytes_before = harness.scene.graph().index_bytes();

            g_allocations = 0;
            g_count_allocations = true;

            std::vector<double> samples;
            samples.reserve(REMOVE_CYCLES);

            for (std::size_t cycle = 0; cycle < REMOVE_CYCLES; cycle++) {
                // remove the whole batch, which is what recycling a page of rows does (§20.2)
                const Clock::time_point start = Clock::now();

                for (std::size_t index = 0; index < REMOVE_WIDTH; index++) {
                    static_cast<void>(harness.scene.graph().remove_binding(harness.scene.graph().writer_of(targets.at(index))));

                    // the releasing variant also hands the property's payload back to the store (§13.1)
                    if (variant == RemoveVariant::TombstoneIdReuseRelease) {
                        harness.scene.graph().remove_property(targets.at(index));
                    }
                }

                samples.push_back(to_us(Clock::now() - start));

                // re-bind the same properties, so the next cycle removes what this one added (§20.2)
                for (std::size_t index = 0; index < REMOVE_WIDTH; index++) {
                    static_cast<void>(harness.scene.graph().add_binding(targets.at(index), std::vector<albor::prototype_d2::PropertyId>{sources.at(index)}, &first_binding<Store>));
                }
            }

            g_count_allocations = false;
            cost.allocations = g_allocations;
            cost.p99_us = percentile(samples, 0.99);

            cost.records_after = harness.scene.graph().binding_record_count();
            cost.binding_bytes_after = harness.scene.graph().binding_bytes();
            cost.index_bytes_after = harness.scene.graph().index_bytes();

            // hand back what this mechanism cost (§28.2)
            return cost;
        }

        // measure the replacement p99 at the committed scale, which is where a reload has to stay inside a frame (§28.4)
        template <class Store>
        double measure_replace_p99_at_scale(const ReplaceVariant variant, const bool mutable_index) {
            Harness<Store> harness(albor::prototype_d2::EvalModel::EagerPush, mutable_index);
            configure(harness, variant, RemoveVariant::Tombstone);

            // build the committed binding count, so the measurement is taken at application scale (§28.4)
            for (std::size_t index = 0; index < SCALE_BINDINGS; index++) {
                static_cast<void>(harness.add_pair(&first_binding<Store>));
            }

            harness.scene.graph().seal();
            static_cast<void>(harness.scene.graph().settle());

            const albor::prototype_d2::PropertyId target = harness.properties.front();
            const albor::prototype_d2::PropertyId source = harness.sources.front();

            std::vector<double> samples;
            samples.reserve(TIMED_REPLACEMENTS);

            for (std::size_t index = 0; index < TIMED_REPLACEMENTS; index++) {
                const albor::prototype_d2::BindingFn<Store> fn = index % 2 == 0 ? &second_binding<Store> : &first_binding<Store>;

                const Clock::time_point start = Clock::now();
                static_cast<void>(replace(harness, variant, target, source, fn));
                samples.push_back(to_us(Clock::now() - start));
            }

            // hand back the p99 of one replacement at scale (§28.2)
            return percentile(samples, 0.99);
        }

        // the six correctness observables, one function each (§28.4)
        struct Observables
        {
            bool old_binding_evaluates_nothing = false;
            bool new_dependencies_picked_up = false;
            bool removed_target_keeps_value = false;
            bool removed_property_readers_re_evaluate = false;
            bool override_survives_replacement = false;
            bool animation_survives_replacement = false;

            [[nodiscard]] std::size_t passed_count() const {
                // count what this variant passes (§28.2)
                std::size_t passed = 0;
                passed += old_binding_evaluates_nothing ? 1u : 0u;
                passed += new_dependencies_picked_up ? 1u : 0u;
                passed += removed_target_keeps_value ? 1u : 0u;
                passed += removed_property_readers_re_evaluate ? 1u : 0u;
                passed += override_survives_replacement ? 1u : 0u;
                passed += animation_survives_replacement ? 1u : 0u;
                return passed;
            }
        };

        // observable 1: after a replacement, changing the old binding's source evaluates nothing (§18.1)
        template <class Store>
        bool check_old_binding_evaluates_nothing(const ReplaceVariant variant) {
            Harness<Store> harness(albor::prototype_d2::EvalModel::EagerPush, true);
            configure(harness, variant, RemoveVariant::Tombstone);

            // two sources, so the replacement can move the binding onto the second one (§18.1)
            const albor::prototype_d2::InstanceId pair = harness.scene.store().create(harness.schema);
            const albor::prototype_d2::PropertyId old_source = harness.scene.at(pair, 0);
            const albor::prototype_d2::PropertyId new_source = harness.scene.at(pair, 1);
            const albor::prototype_d2::InstanceId target_instance = harness.scene.store().create(harness.schema);
            const albor::prototype_d2::PropertyId target = harness.scene.at(target_instance, 0);

            static_cast<void>(harness.scene.store().write(old_source, albor::prototype_d2::Value(static_cast<std::int64_t>(1)), albor::prototype_d2::WriteSource::GeneratedInit));
            static_cast<void>(harness.scene.store().write(new_source, albor::prototype_d2::Value(static_cast<std::int64_t>(10)), albor::prototype_d2::WriteSource::GeneratedInit));
            static_cast<void>(harness.scene.store().write(target, albor::prototype_d2::Value(static_cast<std::int64_t>(0)), albor::prototype_d2::WriteSource::GeneratedInit));
            static_cast<void>(harness.scene.graph().add_binding(target, std::vector<albor::prototype_d2::PropertyId>{old_source}, &first_binding<Store>));
            harness.scene.graph().seal();
            static_cast<void>(harness.scene.graph().settle());

            // move the binding onto the new source, which is the reload's whole effect (§18.1)
            static_cast<void>(replace(harness, variant, target, new_source, &first_binding<Store>));
            static_cast<void>(harness.scene.graph().settle());

            // write the old source and count what the settle evaluated (§28.2)
            harness.scene.graph().assign(old_source, albor::prototype_d2::Value(static_cast<std::int64_t>(99)));
            const albor::prototype_d2::SettleStats stats = harness.scene.graph().settle();

            // report whether the dead binding was reached at all (§28.2)
            return stats.bindings_evaluated == 0 && stats.redundant_evaluations == 0;
        }

        // observable 2: the replacement's new dependency set is what the binding reads (§18.1)
        template <class Store>
        bool check_new_dependencies_picked_up(const ReplaceVariant variant) {
            Harness<Store> harness(albor::prototype_d2::EvalModel::EagerPush, true);
            configure(harness, variant, RemoveVariant::Tombstone);

            // two sources, so the second one can be the dependency the replacement declares (§18.1)
            const albor::prototype_d2::InstanceId pair = harness.scene.store().create(harness.schema);
            const albor::prototype_d2::PropertyId first_source = harness.scene.at(pair, 0);
            const albor::prototype_d2::PropertyId second_source = harness.scene.at(pair, 1);
            const albor::prototype_d2::InstanceId target_instance = harness.scene.store().create(harness.schema);
            const albor::prototype_d2::PropertyId target = harness.scene.at(target_instance, 0);

            static_cast<void>(harness.scene.store().write(first_source, albor::prototype_d2::Value(static_cast<std::int64_t>(1)), albor::prototype_d2::WriteSource::GeneratedInit));
            static_cast<void>(harness.scene.store().write(second_source, albor::prototype_d2::Value(static_cast<std::int64_t>(10)), albor::prototype_d2::WriteSource::GeneratedInit));
            static_cast<void>(harness.scene.store().write(target, albor::prototype_d2::Value(static_cast<std::int64_t>(0)), albor::prototype_d2::WriteSource::GeneratedInit));
            static_cast<void>(harness.scene.graph().add_binding(target, std::vector<albor::prototype_d2::PropertyId>{first_source}, &first_binding<Store>));
            harness.scene.graph().seal();
            static_cast<void>(harness.scene.graph().settle());

            static_cast<void>(replace(harness, variant, target, second_source, &first_binding<Store>));
            static_cast<void>(harness.scene.graph().settle());

            // write the new source and check the target moved (§13.3)
            harness.scene.graph().assign(second_source, albor::prototype_d2::Value(static_cast<std::int64_t>(20)));
            static_cast<void>(harness.scene.graph().settle());

            // report whether the target followed its new dependency (§28.2)
            return harness.scene.store().read(target).as_int() == 21;
        }

        // observable 3: a removed binding leaves the target's last value alone (§4)
        //
        // the removal mechanism is the variant's own, so the check covers both axes: which record a replacement
        // mechanism leaves behind, and whether a removal variant's free list keeps the target's value intact (§28.4)
        template <class Store>
        bool check_removed_target_after_removal(const ReplaceVariant replace_variant, const RemoveVariant variant = RemoveVariant::Tombstone) {
            Harness<Store> harness(albor::prototype_d2::EvalModel::EagerPush, true);
            configure(harness, replace_variant, variant);

            const albor::prototype_d2::PropertyId target = harness.add_pair(&first_binding<Store>);
            const albor::prototype_d2::PropertyId source = harness.sources.front();

            harness.scene.graph().seal();
            static_cast<void>(harness.scene.graph().settle());

            const std::int64_t before = harness.scene.store().read(target).as_int();

            // remove the binding, then move its old source and check nothing wrote the target again (§4)
            static_cast<void>(harness.scene.graph().remove_binding(harness.scene.graph().writer_of(target)));
            harness.scene.graph().assign(source, albor::prototype_d2::Value(static_cast<std::int64_t>(500)));
            const albor::prototype_d2::SettleStats stats = harness.scene.graph().settle();

            // report whether the value survived untouched (§28.2)
            return harness.scene.store().read(target).as_int() == before && stats.bindings_evaluated == 0;
        }

        // observable 4: a removed property's readers re-evaluate onto generated initialization (§4)
        template <class Store>
        bool check_removed_property_readers_re_evaluate(const ReplaceVariant replace_variant, const RemoveVariant variant = RemoveVariant::Tombstone) {
            Harness<Store> harness(albor::prototype_d2::EvalModel::EagerPush, true);
            configure(harness, replace_variant, variant);

            const albor::prototype_d2::PropertyId target = harness.add_pair(&first_binding<Store>);
            const albor::prototype_d2::PropertyId source = harness.sources.front();

            harness.scene.graph().seal();
            static_cast<void>(harness.scene.graph().settle());

            // remove the binding and the property, then re-add a reader of the same id (§4)
            static_cast<void>(harness.scene.graph().remove_binding(harness.scene.graph().writer_of(target)));
            harness.scene.graph().remove_property(source);
            static_cast<void>(harness.scene.graph().settle());

            // re-declare a binding on the released property, which is what a re-created row does (§20.2)
            static_cast<void>(harness.scene.graph().add_binding(target, std::vector<albor::prototype_d2::PropertyId>{source}, &first_binding<Store>));
            const albor::prototype_d2::SettleStats stats = harness.scene.graph().settle();

            // report whether the reader ran against the released value rather than stale payload (§28.2)
            return stats.bindings_evaluated > 0 && harness.scene.store().read(target).as_int() == 1;
        }

        // observable 5: an imperatively overridden property keeps its override across a replacement (§13.4)
        template <class Store>
        bool check_override_survives_replacement(const ReplaceVariant variant) {
            Harness<Store> harness(albor::prototype_d2::EvalModel::EagerPush, true);
            configure(harness, variant, RemoveVariant::Tombstone);

            const albor::prototype_d2::PropertyId target = harness.add_pair(&first_binding<Store>);
            const albor::prototype_d2::PropertyId source = harness.sources.front();

            harness.scene.graph().seal();
            static_cast<void>(harness.scene.graph().settle());

            // the user assigns the property, which breaks the binding (§13.4)
            harness.scene.graph().assign(target, albor::prototype_d2::Value(static_cast<std::int64_t>(1234)));
            static_cast<void>(harness.scene.graph().settle());

            // a reload replaces the declared binding, which must not take the override back (§13.4, §18.1)
            static_cast<void>(replace(harness, variant, target, source, &second_binding<Store>));
            static_cast<void>(harness.scene.graph().settle());

            // report whether the override survived (§28.2)
            return harness.scene.store().read(target).as_int() == 1234;
        }

        // observable 6: an animated property keeps its animation across a replacement (§13.5)
        template <class Store>
        bool check_animation_survives_replacement(const ReplaceVariant variant) {
            Harness<Store> harness(albor::prototype_d2::EvalModel::EagerPush, true);
            configure(harness, variant, RemoveVariant::Tombstone);

            const albor::prototype_d2::PropertyId target = harness.add_pair(&first_binding<Store>);
            const albor::prototype_d2::PropertyId source = harness.sources.front();

            harness.scene.graph().seal();
            static_cast<void>(harness.scene.graph().settle());

            // an animation owns the target, which drops every binding write until it ends (§13.5)
            static_cast<void>(harness.scene.graph().set_animation(target, albor::prototype_d2::Value(static_cast<std::int64_t>(7777))));
            static_cast<void>(replace(harness, variant, target, source, &first_binding<Store>));
            static_cast<void>(harness.scene.graph().settle());

            // report whether the animation still owns the property after the replacement (§28.2)
            return harness.scene.store().source_of(target) == albor::prototype_d2::WriteSource::Animation;
        }

    } // namespace

    void run_mutate(Verdict& verdict) {
        using albor::prototype_d2::TypedStore;

        const std::vector<ReplaceVariant> replace_variants{ReplaceVariant::RemoveThenAdd, ReplaceVariant::InPlace, ReplaceVariant::InPlaceIdReuse};
        const std::vector<RemoveVariant> remove_variants{RemoveVariant::Tombstone, RemoveVariant::TombstoneIdReuse, RemoveVariant::TombstoneIdReuseRelease};

        // the replacement cost over the committed cycles, on both indexes (§28.4)
        std::vector<ReplaceCost> replace_costs;

        for (const ReplaceVariant variant : replace_variants) {
            replace_costs.push_back(measure_replace<TypedStore>(variant, false));
            replace_costs.push_back(measure_replace<TypedStore>(variant, true));
        }

        std::printf("--- %zu replacement cycles on one bound property ---\n", REPLACE_CYCLES);
        std::printf("  %-20s %-12s %10s %10s %14s %14s %10s %10s\n", "mechanism", "index", "records", "growth", "binding B", "index B", "allocs", "p99 us");

        for (const ReplaceCost& cost : replace_costs) {
            const double growth = static_cast<double>(cost.records_after) / static_cast<double>(std::max<std::size_t>(1, cost.records_before)) - 1.0;
            std::printf("  %-20s %-12s %10zu %9s%% %14zu %14zu %10zu %10s\n", cost.name.c_str(), cost.index.c_str(), cost.records_after, format(growth * 100.0, 1).c_str(), cost.binding_bytes_after, cost.index_bytes_after, cost.allocations, format(cost.p99_us, 2).c_str());
        }

        // T3.8.1: no mechanism may grow the table by more than a quarter over the committed cycles (§28.4)
        {
            double worst = 0.0;
            std::string worst_name;

            for (const ReplaceCost& cost : replace_costs) {
                const double growth = static_cast<double>(cost.records_after) / static_cast<double>(std::max<std::size_t>(1, cost.records_before)) - 1.0;

                if (growth > worst) {
                    worst = growth;
                    worst_name = cost.name + "/" + cost.index;
                }
            }

            check(verdict, "T3.8.1", "binding table grows <= 25% over 1000 cycles", worst <= T3_8_1_TABLE_GROWTH, "worst " + worst_name + " " + format(worst * 100.0, 1) + "% <= " + format(T3_8_1_TABLE_GROWTH * 100.0, 0) + "%");
        }

        // T3.8.2: the in-place mechanism's replacement p99 at the committed scale (§28.4)
        {
            const double sealed_p99 = measure_replace_p99_at_scale<TypedStore>(ReplaceVariant::InPlace, false);
            const double segmented_p99 = measure_replace_p99_at_scale<TypedStore>(ReplaceVariant::InPlace, true);
            const double worst = std::max(sealed_p99, segmented_p99);
            check(verdict, "T3.8.2", "in-place replacement p99 <= 2 us at 11000 bindings", worst <= T3_8_2_REPLACE_US, "sealed " + format(sealed_p99, 2) + " us, segmented " + format(segmented_p99, 2) + " us, budget " + format(T3_8_2_REPLACE_US, 1) + " us");
        }

        // the removal cost over the committed cycles, on both indexes (§28.4)
        std::vector<ReplaceCost> remove_costs;

        for (const RemoveVariant variant : remove_variants) {
            remove_costs.push_back(measure_remove<TypedStore>(variant, false));
            remove_costs.push_back(measure_remove<TypedStore>(variant, true));
        }

        std::printf("--- %zu add and remove cycles of %zu bindings ---\n", REMOVE_CYCLES, REMOVE_WIDTH);
        std::printf("  %-26s %-12s %10s %10s %14s %14s %10s %10s\n", "mechanism", "index", "records", "growth", "binding B", "index B", "allocs", "p99 us");

        for (const ReplaceCost& cost : remove_costs) {
            const double growth = static_cast<double>(cost.records_after) / static_cast<double>(std::max<std::size_t>(1, cost.records_before)) - 1.0;
            std::printf("  %-26s %-12s %10zu %9s%% %14zu %14zu %10zu %10s\n", cost.name.c_str(), cost.index.c_str(), cost.records_after, format(growth * 100.0, 1).c_str(), cost.binding_bytes_after, cost.index_bytes_after, cost.allocations, format(cost.p99_us, 2).c_str());
        }

        // T3.8.3: the sealed index must not accumulate across its rebuilds, and the segmented slack is recorded (§28.4)
        {
            double worst_csr = 0.0;

            for (const ReplaceCost& cost : remove_costs) {
                if (cost.index != "sealed_csr") {
                    continue;
                }

                worst_csr = std::max(worst_csr, static_cast<double>(cost.index_bytes_after) / static_cast<double>(std::max<std::size_t>(1, cost.index_bytes_before)) - 1.0);
            }

            check(verdict, "T3.8.3", "sealed index bytes <= initial + 10% after cycles", worst_csr <= T3_8_3_INDEX_GROWTH, "worst sealed growth " + format(worst_csr * 100.0, 2) + "% <= " + format(T3_8_3_INDEX_GROWTH * 100.0, 0) + "%");
        }

        // T3.8.4: all six observables in every replacement variant, which is the committed 18 checks (§28.4)
        //
        // the removal variants are measured by the same two removal observables and reported beside the verdict,
        // because the committed count of 18 is six observables across three replacement mechanisms
        {
            std::size_t passed = 0;
            std::size_t total = 0;

            for (const ReplaceVariant variant : replace_variants) {
                const bool old_binding_quiet = check_old_binding_evaluates_nothing<TypedStore>(variant);
                const bool new_dependencies = check_new_dependencies_picked_up<TypedStore>(variant);
                const bool removed_value = check_removed_target_after_removal<TypedStore>(variant);
                const bool removed_readers = check_removed_property_readers_re_evaluate<TypedStore>(variant, RemoveVariant::Tombstone);
                const bool override_survives = check_override_survives_replacement<TypedStore>(variant);
                const bool animation_survives = check_animation_survives_replacement<TypedStore>(variant);

                passed += static_cast<std::size_t>(old_binding_quiet) + static_cast<std::size_t>(new_dependencies) + static_cast<std::size_t>(removed_value) + static_cast<std::size_t>(removed_readers) + static_cast<std::size_t>(override_survives) + static_cast<std::size_t>(animation_survives);
                total += 6;

                record("-", replace_name(variant) + ": observables", std::string("old-binding quiet=") + (old_binding_quiet ? "pass" : "FAIL") + ", new deps=" + (new_dependencies ? "pass" : "FAIL") + ", removed value=" + (removed_value ? "pass" : "FAIL") + ", removed readers=" + (removed_readers ? "pass" : "FAIL") + ", override=" + (override_survives ? "pass" : "FAIL") + ", animation=" + (animation_survives ? "pass" : "FAIL"));
            }

            // the removal variants run the two removal observables, which is what distinguishes them (§28.4)
            for (const RemoveVariant variant : remove_variants) {
                const bool removed_value = check_removed_target_after_removal<TypedStore>(ReplaceVariant::RemoveThenAdd, variant);
                const bool removed_readers = check_removed_property_readers_re_evaluate<TypedStore>(ReplaceVariant::RemoveThenAdd, variant);
                record("-", remove_name(variant) + ": removal observables", std::string("removed value=") + (removed_value ? "pass" : "FAIL") + ", removed readers=" + (removed_readers ? "pass" : "FAIL"));
            }

            check(verdict, "T3.8.4", "all six observables pass in every variant", passed == total, std::to_string(passed) + "/" + std::to_string(total) + " checks passed");
        }

        // record the two failures as the diagnoses they are, because both are engine behaviour rather than a
        // property of any mechanism, and both are uniform across the six mechanisms (§28.4)
        record("-", "diagnosis: removed readers", "add_binding after seal never schedules the new binding, so a recycled row's binding is not evaluated until one of its dependencies changes; no removal mechanism changes this");
        record("-", "diagnosis: animation", "has_binding is true only for WriteSource::Binding, so replace_binding reads an animated property as overridden and stamps Imperative over the animation; all three replacement mechanisms share the test");

        // T3.8.5: the per-cycle deltas, which are the decision input between the mechanisms (§28.4)
        for (const ReplaceCost& cost : replace_costs) {
            record("T3.8.5", "replacement cost " + cost.name + "/" + cost.index, std::to_string((cost.binding_bytes_after - cost.binding_bytes_before) / REPLACE_CYCLES) + " binding B/cycle, " + std::to_string(cost.allocations / REPLACE_CYCLES) + " allocs/cycle, " + format(cost.p99_us, 2) + " us p99");
        }

        for (const ReplaceCost& cost : remove_costs) {
            record("T3.8.5", "removal cost " + cost.name + "/" + cost.index, std::to_string((cost.binding_bytes_after - cost.binding_bytes_before) / REMOVE_CYCLES) + " binding B/cycle, " + std::to_string(cost.allocations / (REMOVE_CYCLES * REMOVE_WIDTH)) + " allocs/binding, " + format(cost.p99_us, 2) + " us p99 per cycle of " + std::to_string(REMOVE_WIDTH));
        }
    }

} // namespace albor::prototype_d3

// count every allocation the prototype makes while its counter is armed, which is the only way to report
// allocations per cycle without instrumenting Prototype D2's vectors (§28.2)
void* operator new(std::size_t size) {
    albor::prototype_d3::count_allocation();

    // a throwing allocation is what the standard requires, and the prototype never exhausts memory (§28.2)
    void* memory = std::malloc(size == 0 ? 1 : size);

    if (memory == nullptr) {
        throw std::bad_alloc();
    }

    // hand back the block (§28.2)
    return memory;
}

void* operator new[](std::size_t size) { return operator new(size); }

void operator delete(void* memory) noexcept { std::free(memory); }

void operator delete[](void* memory) noexcept { std::free(memory); }

void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main() {
    std::printf("Prototype D3 R8 — binding replacement and removal semantics, as variants (AD-04c, §18.1)\n");
    std::printf("hardware: i7-9750H macOS, recorded in plan.md; build: cmake --preset release (-O3, C++23)\n");
    std::printf("thresholds: prototypes/d3/plan.md R8, committed before this program existed\n");
    std::printf("remove_then_add and tombstone are D2's recorded mechanisms, so every D2 figure stays reproducible\n\n");

    albor::prototype_d3::Verdict verdict;
    albor::prototype_d3::run_mutate(verdict);

    std::printf("\n=== total: %zu passed, %zu failed ===\n", verdict.passed, verdict.failed);

    // return non-zero when any threshold failed, so a script can gate on it (§28.4)
    return verdict.failed == 0 ? 0 : 1;
}
