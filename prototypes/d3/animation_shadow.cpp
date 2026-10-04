// Prototype D3 R7 — animation shadow audit: does invalidation alone keep binding correctness? (F2.7)
//
// plan.md was committed before this program existed (§28.4). Measurement code; not a runtime module.

#include "prototypes/d2/src/graph.h"
#include "prototypes/d2/src/storage.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace albor::prototype_d3
{
    namespace
    {

        // T3.7.2 the no-shadow mode's animation bookkeeping, per animated property at 1024 properties
        constexpr double T3_7_2_ANIMATION_BYTES = 8.0;

        // the animated-property scale the audit reports at (plan.md R7)
        constexpr std::size_t ANIMATED_PROPERTIES = 1024;

        struct Verdict
        {
            std::size_t passed = 0;
            std::size_t failed = 0;
        };

        void check(Verdict& verdict, std::string_view id, std::string_view description, bool passed, std::string_view detail) {
            std::printf("  %-8s %-64s %-4s %s\n", std::string(id).c_str(), std::string(description).c_str(), passed ? "PASS" : "FAIL", std::string(detail).c_str());

            if (passed) {
                verdict.passed++;
            }
            else {
                verdict.failed++;
            }
        }

        void record(std::string_view id, std::string_view description, std::string_view detail) { std::printf("  %-8s %-64s %-4s %s\n", std::string(id).c_str(), std::string(description).c_str(), "REC", std::string(detail).c_str()); }

        std::string format(double value, int precision = 2) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
            return std::string(buffer);
        }

        // the binding every scenario uses: target = source + 1, a real re-evaluation (§28.2)
        template <class Store>
        void plus_one_binding(albor::prototype_d2::BindingContext<Store>& context) {
            context.write_int(context.read_int(context.declared().front()) + 1);
        }

        // what one scenario produced (§28.2)
        struct ScenarioResult
        {
            std::int64_t final_value = 0;
            bool binding_broken = false;
        };

        // scenario 1: an animation overwrites, ends, and the binding re-evaluates from the current dependency (§13.5)
        template <class Store>
        ScenarioResult scenario_animation_ends(albor::prototype_d2::Graph<Store>& graph, const albor::prototype_d2::PropertyId source, const albor::prototype_d2::PropertyId target, const std::int64_t dependency_value) {
            ScenarioResult result;

            // the dependency the binding will read after the animation ends (§13.5)
            graph.assign(source, albor::prototype_d2::Value(dependency_value));
            static_cast<void>(graph.settle());

            // the animation writes values the binding does not produce (§13.5)
            static_cast<void>(graph.set_animation(target, albor::prototype_d2::Value(static_cast<std::int64_t>(7777))));

            graph.end_animation(target, albor::prototype_d2::AnimationEndMode::RestoreShadow);
            static_cast<void>(graph.settle());

            result.final_value = graph.store().read(target).as_int();
            result.binding_broken = !graph.binding_attached(target);
            return result;
        }

        // scenario 2: a dependency moves mid-animation; the animation ends; the binding reads the new dependency (§13.5)
        template <class Store>
        ScenarioResult scenario_dependency_moves_mid_animation(albor::prototype_d2::Graph<Store>& graph, const albor::prototype_d2::PropertyId source, const albor::prototype_d2::PropertyId target) {
            ScenarioResult result;

            graph.assign(source, albor::prototype_d2::Value(static_cast<std::int64_t>(10)));
            static_cast<void>(graph.settle());
            static_cast<void>(graph.set_animation(target, albor::prototype_d2::Value(static_cast<std::int64_t>(5555))));

            // the dependency moves while the animation owns the target (§13.5)
            graph.assign(source, albor::prototype_d2::Value(static_cast<std::int64_t>(55)));
            static_cast<void>(graph.settle());

            graph.end_animation(target, albor::prototype_d2::AnimationEndMode::RestoreShadow);
            static_cast<void>(graph.settle());

            result.final_value = graph.store().read(target).as_int();
            result.binding_broken = !graph.binding_attached(target);
            return result;
        }

        // scenario 3: an imperative write during the animation wins and survives the animation's end (§13.5)
        template <class Store>
        ScenarioResult scenario_imperative_wins(albor::prototype_d2::Graph<Store>& graph, const albor::prototype_d2::PropertyId source, const albor::prototype_d2::PropertyId target) {
            ScenarioResult result;

            graph.assign(source, albor::prototype_d2::Value(static_cast<std::int64_t>(10)));
            static_cast<void>(graph.settle());
            static_cast<void>(graph.set_animation(target, albor::prototype_d2::Value(static_cast<std::int64_t>(5555))));

            // the imperative assignment breaks the binding, which neither mode may undo (§13.5)
            graph.assign(target, albor::prototype_d2::Value(static_cast<std::int64_t>(1234)));
            static_cast<void>(graph.settle());
            graph.end_animation(target, albor::prototype_d2::AnimationEndMode::RestoreShadow);
            static_cast<void>(graph.settle());

            result.final_value = graph.store().read(target).as_int();
            result.binding_broken = !graph.binding_attached(target);
            return result;
        }

        // scenario 4: a rebind after an animation re-evaluates from the declarative binding (§13.4, §13.5)
        template <class Store>
        ScenarioResult scenario_rebind_after_animation(albor::prototype_d2::Graph<Store>& graph, const albor::prototype_d2::PropertyId source, const albor::prototype_d2::PropertyId target) {
            ScenarioResult result;

            graph.assign(source, albor::prototype_d2::Value(static_cast<std::int64_t>(10)));
            static_cast<void>(graph.settle());
            static_cast<void>(graph.set_animation(target, albor::prototype_d2::Value(static_cast<std::int64_t>(5555))));

            graph.rebind(target);
            static_cast<void>(graph.settle());

            result.final_value = graph.store().read(target).as_int();
            result.binding_broken = !graph.binding_attached(target);
            return result;
        }

        // the animation bookkeeping both modes really hold, measured while every animation is active (§28.2)
        double animation_bytes_per_property(const albor::prototype_d2::AnimationShadowing shadowing) {
            // every instance and binding exists before the graph is constructed, because the graph stamps
            // by property id at construction (§28.4)
            albor::prototype_d2::RegionTable regions;
            const std::vector<albor::prototype_d2::ValueType> types{albor::prototype_d2::ValueType::Int, albor::prototype_d2::ValueType::Int};
            const albor::prototype_d2::SchemaId schema = regions.add_schema("Animated", albor::prototype_d2::InstanceKind::Component, types);
            albor::prototype_d2::TypedStore store(regions);
            std::vector<albor::prototype_d2::PropertyId> sources;
            std::vector<albor::prototype_d2::PropertyId> targets;
            for (std::size_t index = 0; index < ANIMATED_PROPERTIES; index++) {
                const albor::prototype_d2::InstanceId animated = store.create(schema);
                sources.push_back(regions.global(animated, static_cast<albor::prototype_d2::LocalPropertyId>(0)));
                targets.push_back(regions.global(animated, static_cast<albor::prototype_d2::LocalPropertyId>(1)));
            }

            albor::prototype_d2::Graph<albor::prototype_d2::TypedStore> graph(regions, store, albor::prototype_d2::EvalModel::EagerPush, false);
            graph.set_animation_shadowing(shadowing);
            for (std::size_t index = 0; index < ANIMATED_PROPERTIES; index++) {
                const std::vector<albor::prototype_d2::PropertyId> dependencies{sources.at(index)};
                static_cast<void>(graph.add_binding(targets.at(index), dependencies, &plus_one_binding<albor::prototype_d2::TypedStore>));
            }
            graph.seal();

            // every animation active at once, which is when the shadow record is at its fullest (§28.4)
            for (std::size_t index = 0; index < ANIMATED_PROPERTIES; index++) {
                static_cast<void>(graph.set_animation(targets.at(index), albor::prototype_d2::Value(static_cast<std::int64_t>(static_cast<std::int64_t>(index)))));
            }

            // charge the animation record over the animated property count (§28.2)
            return static_cast<double>(graph.animation_side_bytes()) / static_cast<double>(ANIMATED_PROPERTIES);
        }

        // one mode's pass over every scenario (§28.4)
        struct ModeResults
        {
            ScenarioResult fresh;
            ScenarioResult moved;
            ScenarioResult imperative;
            ScenarioResult rebound;
            std::size_t shadow_entries = 0;
        };

        ModeResults run_all_scenarios(const bool shadowing_enabled, const albor::prototype_d2::AnimationShadowing mode) {
            albor::prototype_d2::RegionTable regions;
            const std::vector<albor::prototype_d2::ValueType> types{albor::prototype_d2::ValueType::Int, albor::prototype_d2::ValueType::Int};
            const albor::prototype_d2::SchemaId schema = regions.add_schema("Animated", albor::prototype_d2::InstanceKind::Component, types);
            albor::prototype_d2::TypedStore store(regions);
            const albor::prototype_d2::InstanceId instance = store.create(schema);
            const albor::prototype_d2::PropertyId source = regions.global(instance, static_cast<albor::prototype_d2::LocalPropertyId>(0));
            const albor::prototype_d2::PropertyId target = regions.global(instance, static_cast<albor::prototype_d2::LocalPropertyId>(1));

            albor::prototype_d2::Graph<albor::prototype_d2::TypedStore> graph(regions, store, albor::prototype_d2::EvalModel::EagerPush, false);
            graph.set_animation_shadowing(mode);
            const std::vector<albor::prototype_d2::PropertyId> dependencies{source};
            static_cast<void>(graph.add_binding(target, dependencies, &plus_one_binding<albor::prototype_d2::TypedStore>));
            graph.seal();

            ModeResults results;
            results.fresh = scenario_animation_ends(graph, source, target, 15);
            results.moved = scenario_dependency_moves_mid_animation(graph, source, target);
            results.imperative = scenario_imperative_wins(graph, source, target);
            results.rebound = scenario_rebind_after_animation(graph, source, target);
            results.shadow_entries = graph.shadow_entry_count();

            // name the mode in the row (§28.2)
            std::printf("  mode %s:\n", shadowing_enabled ? "(records)" : "(raw)");
            static_cast<void>(shadowing_enabled);
            return results;
        }

    } // namespace

    void run_shadow_audit(Verdict& verdict) {
        std::printf("--- animation observable checks: full shadow, active-set, none ---\n");
        std::printf("  %-38s %-10s %-12s %-14s %-10s\n", "scenario", "full", "active-set", "none", "expected");

        const ModeResults full = run_all_scenarios(true, albor::prototype_d2::AnimationShadowing::Full);
        const ModeResults active = run_all_scenarios(false, albor::prototype_d2::AnimationShadowing::ActiveSet);
        const ModeResults none = run_all_scenarios(false, albor::prototype_d2::AnimationShadowing::None);

        std::printf("  %-38s f=%-5lld b=%-4d f=%-5lld b=%-4d f=%-5lld\n", "animation ends at the fresh value", static_cast<long long>(full.fresh.final_value), full.fresh.binding_broken ? 1 : 0, static_cast<long long>(active.fresh.final_value), active.fresh.binding_broken ? 1 : 0, static_cast<long long>(none.fresh.final_value));
        std::printf("  %-38s f=%-5lld b=%-4d f=%-5lld b=%-4d f=%-5lld\n", "dependency move mid-animation", static_cast<long long>(full.moved.final_value), full.moved.binding_broken ? 1 : 0, static_cast<long long>(active.moved.final_value), active.moved.binding_broken ? 1 : 0, static_cast<long long>(none.moved.final_value));
        std::printf("  %-38s f=%-5lld b=%-4d f=%-5lld b=%-4d f=%-5lld\n", "imperative write wins", static_cast<long long>(full.imperative.final_value), full.imperative.binding_broken ? 1 : 0, static_cast<long long>(active.imperative.final_value), active.imperative.binding_broken ? 1 : 0, static_cast<long long>(none.imperative.final_value));
        std::printf("  %-38s f=%-5lld b=%-4d f=%-5lld b=%-4d f=%-5lld\n", "rebind after animation", static_cast<long long>(full.rebound.final_value), full.rebound.binding_broken ? 1 : 0, static_cast<long long>(active.rebound.final_value), active.rebound.binding_broken ? 1 : 0, static_cast<long long>(none.rebound.final_value));

        // T3.7.1: judged on the active-set mode, which is the audit's proposed minimal mechanism, and on the
        // naive mode, so the audit's answer is measured in both (§28.4)
        check(verdict, "T3.7.1a", "active-set: animation ends at the fresh binding value", active.fresh.final_value == 16 && !active.fresh.binding_broken, "final=" + std::to_string(active.fresh.final_value) + ", expected=16");
        check(verdict, "T3.7.1b", "active-set: dependency move mid-animation is picked up", active.moved.final_value == 56 && !active.moved.binding_broken, "final=" + std::to_string(active.moved.final_value) + ", expected=56");
        check(verdict, "T3.7.1c", "active-set: imperative write during animation wins", active.imperative.final_value == 1234 && active.imperative.binding_broken, "final=" + std::to_string(active.imperative.final_value) + ", binding broken=" + (active.imperative.binding_broken ? "yes" : "no"));
        check(verdict, "T3.7.1d", "active-set: rebind after animation re-evaluates", active.rebound.final_value == 11 && !active.rebound.binding_broken, "final=" + std::to_string(active.rebound.final_value) + ", expected=11");

        record("-", "naive-none results", "fresh=" + std::to_string(none.fresh.final_value) + ", moved=" + std::to_string(none.moved.final_value) + ", imperative=" + std::to_string(none.imperative.final_value) + ", rebound=" + std::to_string(none.rebound.final_value) + " (the naive elimination overwrites the imperative override)");

        // T3.7.3: no shadow record of any kind is left behind in the none mode (§28.4)
        check(verdict, "T3.7.3", "none mode: zero shadow records after all scenarios", none.shadow_entries == 0, "records=" + std::to_string(none.shadow_entries));

        // the bookkeeping bytes at the committed animated scale, all three modes, measured while active (§28.2)
        std::printf("\n  animation record at %zu animated properties:\n", ANIMATED_PROPERTIES);
        const double full_bytes = animation_bytes_per_property(albor::prototype_d2::AnimationShadowing::Full);
        const double active_bytes = animation_bytes_per_property(albor::prototype_d2::AnimationShadowing::ActiveSet);
        const double none_bytes = animation_bytes_per_property(albor::prototype_d2::AnimationShadowing::None);
        std::printf("    full:        %s B/animated property\n", format(full_bytes, 1).c_str());
        std::printf("    active-set:  %s B/animated property\n", format(active_bytes, 1).c_str());
        std::printf("    none:        %s B/animated property\n", format(none_bytes, 1).c_str());

        // T3.7.2: the active-set mode's record must stay under the slot's own bookkeeping (§28.4)
        check(verdict, "T3.7.2", "active-set animation record <= 8 B/property", active_bytes <= T3_7_2_ANIMATION_BYTES, format(active_bytes, 1) + " <= " + format(T3_7_2_ANIMATION_BYTES, 0) + " B (full: " + format(full_bytes, 1) + " B, D2 T8.6 recorded 64.0 B; none: " + format(none_bytes, 1) + " B)");
    }

} // namespace albor::prototype_d3

int main() {
    std::printf("Prototype D3 R7 — animation shadow audit (AD-04d)\n");
    std::printf("hardware: i7-9750H macOS, recorded in plan.md; build: cmake --preset release (-O3, C++23)\n");
    std::printf("thresholds: prototypes/d3/plan.md R7, committed before this program existed\n\n");

    albor::prototype_d3::Verdict verdict;

    albor::prototype_d3::run_shadow_audit(verdict);

    std::printf("\n=== total: %zu passed, %zu failed ===\n", verdict.passed, verdict.failed);

    // return non-zero when any threshold failed, so a script can gate on it (§28.4)
    return verdict.failed == 0 ? 0 : 1;
}
