#include <deque>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "albor/core/paint.h"
#include "albor/runtime/binding_engine.h"
#include "albor/runtime/observable.h"
#include "albor/runtime/property.h"
#include "albor/runtime/property_id.h"

using namespace albor;

TEST(PropertyId, AnInvalidIdentifierIsNotValid) {
    // arrange, act
    const PropertyId id = invalid_property_id();
    // assert
    EXPECT_FALSE(is_valid(id));
}

TEST(PropertyId, TheDefaultIdentifierIsValid) {
    // arrange, act
    const PropertyId id{};
    // assert
    EXPECT_TRUE(is_valid(id));
}

TEST(PropertyId, TwoIdentifiersWithTheSameFieldsCompareEqual) {
    // arrange
    const PropertyId first{.base = 3, .slot = 7};
    const PropertyId second{.base = 3, .slot = 7};
    // act, assert
    EXPECT_EQ(first, second);
}

TEST(PropertyId, TwoIdentifiersWithDifferentSlotsDiffer) {
    // arrange
    const PropertyId first{.base = 3, .slot = 7};
    const PropertyId second{.base = 3, .slot = 8};
    // act, assert
    EXPECT_NE(first, second);
}

TEST(PropertyId, AKeyPacksTheBaseAndTheSlot) {
    // arrange
    const PropertyId id{.base = 2, .slot = 5};
    // act
    const std::uint64_t key = property_key(id);
    // assert
    EXPECT_EQ(key, (static_cast<std::uint64_t>(2) << 32) | 5u);
}

TEST(PropertyId, TwoComponentsWithTheSameSlotProduceDifferentKeys) {
    // arrange
    const PropertyId first{.base = 1, .slot = 0};
    const PropertyId second{.base = 2, .slot = 0};
    // act, assert
    EXPECT_NE(property_key(first), property_key(second));
}

TEST(PropertyIdAllocator, TheFirstComponentTakesTheFirstBase) {
    // arrange
    PropertyIdAllocator allocator(10);
    // act
    const std::uint32_t base = allocator.begin_component();
    // assert
    EXPECT_EQ(base, 10u);
}

TEST(PropertyIdAllocator, EachComponentTakesADifferentBase) {
    // arrange
    PropertyIdAllocator allocator(1);
    const std::uint32_t first = allocator.begin_component();
    // act
    const std::uint32_t second = allocator.begin_component();
    // assert
    EXPECT_EQ(first, 1u);
    EXPECT_EQ(second, 2u);
}

TEST(PropertyIdAllocator, TheSlotsOfOneComponentCountUp) {
    // arrange
    PropertyIdAllocator allocator(1);
    const std::uint32_t base = allocator.begin_component();
    // act
    const PropertyId first = allocator.next();
    const PropertyId second = allocator.next();
    // assert
    EXPECT_EQ(first.base, base);
    EXPECT_EQ(first.slot, 0u);
    EXPECT_EQ(second.slot, 1u);
}

TEST(PropertyIdAllocator, EveryIdentifierCarriesTheOpenBase) {
    // arrange
    PropertyIdAllocator allocator(4);
    const std::uint32_t base = allocator.begin_component();
    // act
    const PropertyId id = allocator.next();
    // assert
    EXPECT_EQ(base, 4u);
    EXPECT_EQ(id.base, 4u);
}

TEST(PropertyIdAllocator, ANewComponentRestartsTheSlots) {
    // arrange
    PropertyIdAllocator allocator(1);
    const std::uint32_t first_base = allocator.begin_component();
    const PropertyId first = allocator.next();
    const PropertyId second = allocator.next();
    // act
    const std::uint32_t base = allocator.begin_component();
    const PropertyId id = allocator.next();
    // assert
    EXPECT_EQ(second.slot, 1u);
    EXPECT_NE(base, first_base);
    EXPECT_NE(base, first.base);
    EXPECT_EQ(id.slot, 0u);
}

TEST(PropertyIdAllocator, TheComponentCountStartsAtZero) {
    // arrange, act
    PropertyIdAllocator allocator(1);
    // assert
    EXPECT_EQ(allocator.component_count(), 0u);
}

TEST(PropertyIdAllocator, TheComponentCountGrowsWithEveryComponent) {
    // arrange
    PropertyIdAllocator allocator(1);
    // act
    const std::uint32_t first = allocator.begin_component();
    const std::uint32_t second = allocator.begin_component();
    // assert
    EXPECT_NE(first, second);
    EXPECT_EQ(allocator.component_count(), 2u);
}

TEST(PropertyIdAllocator, TheSlotCountReportsWhatTheComponentTook) {
    // arrange
    PropertyIdAllocator allocator(1);
    const std::uint32_t base = allocator.begin_component();
    const PropertyId first = allocator.next();
    // act
    const PropertyId second = allocator.next();
    // assert
    EXPECT_EQ(first.base, base);
    EXPECT_EQ(first.slot, 0u);
    EXPECT_EQ(second.slot, 1u);
    EXPECT_EQ(allocator.slot_count(), 2u);
}

TEST(BindingEngine, AnEmptyEngineHasNoBindings) {
    // arrange, act
    BindingEngine engine;
    // assert
    EXPECT_EQ(engine.size(), 0u);
}

TEST(BindingEngine, BindingRegistersOneBinding) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId target = engine.next_property_id();
    // act
    const BindingId binding = engine.bind(target, {}, [] {});
    // assert
    EXPECT_TRUE(engine.bound(binding));
    EXPECT_EQ(engine.size(), 1u);
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, HandleZeroIsNeverBound) {
    // arrange, act
    BindingEngine engine;
    // assert
    EXPECT_FALSE(engine.bound(0));
}

TEST(BindingEngine, AHandlePastTheEndIsNeverBound) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId target = engine.next_property_id();
    const BindingId binding = engine.bind(target, {}, [] {});
    // act, assert
    EXPECT_TRUE(engine.bound(binding));
    EXPECT_FALSE(engine.bound(99));
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, ABindingRemembersItsTarget) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId target = engine.next_property_id();
    // act
    const BindingId binding = engine.bind(target, {}, [] {});
    // assert
    EXPECT_EQ(engine.target(binding), target);
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, AStaleHandleRemembersNoTarget) {
    // arrange, act
    BindingEngine engine;
    // assert
    EXPECT_EQ(engine.target(7), invalid_property_id());
}

TEST(BindingEngine, ABindingRemembersItsDeclaredDependencies) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId source{.base = base, .slot = 0};
    const PropertyId target = engine.next_property_id();
    // act
    const BindingId binding = engine.bind(target, {source}, [] {});
    // assert
    ASSERT_EQ(engine.dependencies(binding).size(), 1u);
    EXPECT_EQ(engine.dependencies(binding)[0], source);
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, AStaleHandleHasNoDependencies) {
    // arrange, act
    BindingEngine engine;
    // assert
    EXPECT_TRUE(engine.dependencies(42).empty());
}

TEST(BindingEngine, ADeclaredDependencyCountsOneDependent) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId source{.base = base, .slot = 0};
    const PropertyId target = engine.next_property_id();
    // act
    const BindingId binding = engine.bind(target, {source}, [] {});
    // assert
    EXPECT_TRUE(engine.bound(binding));
    EXPECT_EQ(engine.dependent_count(source), 1u);
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, APropertyNothingDependsOnHasNoDependents) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    // act
    const PropertyId lonely{.base = base, .slot = 5};
    // assert
    EXPECT_EQ(engine.dependent_count(lonely), 0u);
}

TEST(BindingEngine, TwoBindingsOnOneSourceCountTwoDependents) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId source{.base = base, .slot = 0};
    const PropertyId first = engine.next_property_id();
    const PropertyId second = engine.next_property_id();
    // act
    const BindingId first_binding = engine.bind(first, {source}, [] {});
    const BindingId second_binding = engine.bind(second, {source}, [] {});
    // assert
    EXPECT_TRUE(engine.bound(first_binding));
    EXPECT_TRUE(engine.bound(second_binding));
    EXPECT_EQ(engine.dependent_count(source), 2u);
}

TEST(BindingEngine, UnbindingRemovesTheBinding) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId target = engine.next_property_id();
    const BindingId binding = engine.bind(target, {}, [] {});
    // act
    engine.unbind(binding);
    // assert
    EXPECT_EQ(engine.size(), 0u);
    EXPECT_FALSE(engine.bound(binding));
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, UnbindingTwiceChangesNothingTheSecondTime) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId target = engine.next_property_id();
    const BindingId binding = engine.bind(target, {}, [] {});
    engine.unbind(binding);
    // act
    const bool removed = engine.unbind(binding);
    // assert
    EXPECT_FALSE(removed);
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, UnbindingAStaleHandleReportsFailure) {
    // arrange, act
    BindingEngine engine;
    // assert
    EXPECT_FALSE(engine.unbind(11));
}

TEST(BindingEngine, UnbindingHandleZeroReportsFailure) {
    // arrange, act
    BindingEngine engine;
    // assert
    EXPECT_FALSE(engine.unbind(0));
}

TEST(BindingEngine, ARepeatedRebindReusesItsSlot) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId target = engine.next_property_id();
    BindingId first = 0;
    bool removed_all = true;
    // act
    for (int cycle = 0; cycle < 100; cycle++) {
        const BindingId binding = engine.bind(target, {}, [] {});
        if (cycle == 0)
            first = binding;
        removed_all = engine.unbind(binding) && removed_all;
    }
    // assert
    EXPECT_TRUE(removed_all);
    EXPECT_EQ(engine.binding_slot_count(), 1u);
    EXPECT_EQ(engine.size(), 0u);
    EXPECT_FALSE(engine.bound(first));
    EXPECT_EQ(engine.target(first), invalid_property_id());
}

TEST(BindingEngine, AReusedSlotDoesNotResurrectAStaleHandle) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId target = engine.next_property_id();
    const BindingId first = engine.bind(target, {}, [] {});
    EXPECT_TRUE(engine.unbind(first));
    // act
    const BindingId second = engine.bind(target, {}, [] {});
    // assert
    EXPECT_NE(first, second);
    EXPECT_FALSE(engine.bound(first));
    EXPECT_EQ(engine.target(first), invalid_property_id());
    EXPECT_TRUE(engine.bound(second));
    EXPECT_EQ(engine.target(second), target);
    EXPECT_EQ(engine.binding_slot_count(), 1u);
}

TEST(BindingEngine, AStaleHandleCannotInvalidateAReusedSlot) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId target = engine.next_property_id();
    const BindingId first = engine.bind(target, {}, [] {});
    engine.settle();
    EXPECT_TRUE(engine.unbind(first));
    const BindingId second = engine.bind(target, {}, [] {});
    engine.settle();
    // act
    engine.invalidate(first);
    // assert
    EXPECT_FALSE(engine.bound(first));
    EXPECT_TRUE(engine.bound(second));
    EXPECT_EQ(engine.dirty_count(), 0u);
    EXPECT_EQ(engine.settle(), 0u);
}

TEST(BindingEngine, AReusedSlotKeepsOneConsumerEdge) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId source = engine.next_property_id();
    const PropertyId target = engine.next_property_id();
    bool removed_all = true;
    std::size_t runs = 0;
    BindingId binding = 0;
    // act
    for (int cycle = 0; cycle < 100; cycle++) {
        const BindingId current = engine.bind(target, {source}, [] {});
        engine.settle();
        removed_all = engine.unbind(current) && removed_all;
    }
    binding = engine.bind(target, {source}, [&runs] { runs++; });
    engine.settle();
    engine.notify_changed(source);
    engine.settle();
    // assert
    EXPECT_TRUE(removed_all);
    EXPECT_EQ(engine.dependent_count(source), 1u);
    EXPECT_EQ(engine.binding_slot_count(), 1u);
    EXPECT_EQ(runs, 2u);
    EXPECT_TRUE(engine.bound(binding));
}

TEST(BindingEngine, AReusedSlotDuringSettleNeverRunsTheReplacementTwice) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId source = engine.next_property_id();
    const PropertyId target = engine.next_property_id();
    const PropertyId trigger_target = engine.next_property_id();
    struct Rebind
    {
        BindingEngine* engine = nullptr;
        PropertyId target = {};
        BindingId victim = 0;
        bool armed = false;
        std::size_t* replacement_runs = nullptr;
        static void trigger(void* raw) {
            // the trigger frees the queued victim and binds its replacement on the same slot (§7.3)
            auto* self = static_cast<Rebind*>(raw);
            if (!self->armed)
                return;
            self->engine->unbind(self->victim);
            (void)self->engine->bind(self->target, {}, [runs = self->replacement_runs] { (*runs)++; });
        }
    };
    std::size_t victim_runs = 0;
    std::size_t replacement_runs = 0;
    Rebind context{.engine = &engine, .target = target, .replacement_runs = &replacement_runs};
    const PropertyId dependencies[] = {source};
    const BindingId trigger = engine.bind_static(trigger_target, dependencies, &Rebind::trigger, &context);
    const BindingId victim = engine.bind(target, {source}, [&victim_runs] { victim_runs++; });
    context.victim = victim;
    engine.settle();
    // act
    context.armed = true;
    engine.notify_changed(source);
    const std::size_t passes = engine.settle();
    // assert
    EXPECT_EQ(victim_runs, 1u);
    EXPECT_EQ(replacement_runs, 1u);
    EXPECT_EQ(passes, 2u);
    EXPECT_EQ(engine.binding_slot_count(), 2u);
    EXPECT_FALSE(engine.bound(victim));
    EXPECT_TRUE(engine.bound(trigger));
    EXPECT_EQ(engine.dirty_count(), 0u);
    EXPECT_FALSE(engine.cycle_detected());
}

TEST(BindingEngine, ABindingUnboundDuringSettleIsSkipped) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId source = engine.next_property_id();
    const PropertyId trigger_target = engine.next_property_id();
    const PropertyId target = engine.next_property_id();
    struct Unbind
    {
        BindingEngine* engine = nullptr;
        BindingId target = 0;
        bool armed = false;
        int value = 0;
        int evaluations = 0;
        static void evaluate(void* raw) {
            // the target writes a value the test reads after the settle (§7.3)
            auto* self = static_cast<Unbind*>(raw);
            self->value = 7;
            self->evaluations++;
        }
        static void trigger(void* raw) {
            // the trigger removes the target that the order already queued (§7.3)
            auto* self = static_cast<Unbind*>(raw);
            if (self->armed)
                self->engine->unbind(self->target);
        }
    };
    Unbind context{.engine = &engine};
    const PropertyId dependencies[] = {source};
    const BindingId trigger = engine.bind_static(trigger_target, dependencies, &Unbind::trigger, &context);
    context.target = engine.bind_static(target, dependencies, &Unbind::evaluate, &context);
    engine.settle();
    // act
    const std::size_t evaluations_before = engine.evaluation_count();
    context.armed = true;
    engine.notify_changed(source);
    // assert
    EXPECT_NO_THROW(engine.settle());
    EXPECT_FALSE(engine.bound(context.target));
    EXPECT_TRUE(engine.bound(trigger));
    EXPECT_EQ(context.value, 7);
    EXPECT_EQ(context.evaluations, 1);
    EXPECT_EQ(engine.evaluation_count() - evaluations_before, 1u);
    EXPECT_EQ(engine.dirty_count(), 0u);
    EXPECT_FALSE(engine.cycle_detected());
}

TEST(BindingEngine, ABindingReboundDuringSettleRunsItsReplacement) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId source = engine.next_property_id();
    const PropertyId trigger_target = engine.next_property_id();
    const PropertyId target = engine.next_property_id();
    struct Rebind
    {
        BindingEngine* engine = nullptr;
        PropertyId target = {};
        BindingId previous = 0;
        BindingId replacement = 0;
        bool armed = false;
        int value = 0;
        static void evaluate_first(void* raw) {
            // the first target writes the value the test reads before the rebind (§7.3)
            auto* self = static_cast<Rebind*>(raw);
            self->value = 7;
        }
        static void evaluate_replacement(void* raw) {
            // the replacement writes the value the next pass must apply (§7.3)
            auto* self = static_cast<Rebind*>(raw);
            self->value = 11;
        }
        static void trigger(void* raw) {
            // the trigger rebinds the target that the order already queued (§7.3)
            auto* self = static_cast<Rebind*>(raw);
            if (!self->armed)
                return;
            self->replacement = self->engine->bind_static(self->target, std::span<const PropertyId>(), &Rebind::evaluate_replacement, self);
        }
    };
    Rebind context{.engine = &engine, .target = target};
    const PropertyId dependencies[] = {source};
    const BindingId trigger = engine.bind_static(trigger_target, dependencies, &Rebind::trigger, &context);
    context.previous = engine.bind_static(target, dependencies, &Rebind::evaluate_first, &context);
    engine.settle();
    // act
    context.armed = true;
    engine.notify_changed(source);
    // assert
    EXPECT_NO_THROW(engine.settle());
    EXPECT_FALSE(engine.bound(context.previous));
    EXPECT_TRUE(engine.bound(context.replacement));
    EXPECT_TRUE(engine.bound(trigger));
    EXPECT_EQ(context.value, 11);
    EXPECT_EQ(engine.dirty_count(), 0u);
    EXPECT_FALSE(engine.cycle_detected());
}

TEST(BindingEngine, InvalidatingAnUnboundBindingMarksNothing) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId target = engine.next_property_id();
    struct Noop
    {
        static void run(void*) {}
    };
    const BindingId binding = engine.bind_static(target, {}, &Noop::run, nullptr);
    EXPECT_TRUE(engine.unbind(binding));
    // act
    engine.invalidate(binding);
    // assert
    EXPECT_EQ(engine.dirty_count(), 0u);
    EXPECT_EQ(engine.settle(), 0u);
    EXPECT_FALSE(engine.cycle_detected());
}

TEST(BindingEngine, AnUnboundHandleReportsNoTarget) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId target = engine.next_property_id();
    struct Noop
    {
        static void run(void*) {}
    };
    const BindingId binding = engine.bind_static(target, {}, &Noop::run, nullptr);
    // act
    const bool removed = engine.unbind(binding);
    // assert
    EXPECT_TRUE(removed);
    EXPECT_FALSE(engine.bound(binding));
    EXPECT_EQ(engine.target(binding), invalid_property_id());
}

TEST(BindingEngine, AnEmptyEvaluatorCreatesNoBinding) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId target = engine.next_property_id();
    // act
    const BindingId binding = engine.bind(target, {}, BindingEvaluator{});
    const BindingId static_binding = engine.bind_static(target, {}, nullptr, nullptr);
    // assert
    EXPECT_EQ(binding, 0u);
    EXPECT_EQ(static_binding, 0u);
    EXPECT_EQ(engine.size(), 0u);
    EXPECT_EQ(engine.dirty_count(), 0u);
    EXPECT_EQ(engine.settle(), 0u);
    EXPECT_FALSE(engine.cycle_detected());
}

TEST(BindingEngine, ADuplicateDependencyLeavesNoConsumerEdge) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId source = engine.next_property_id();
    const PropertyId target = engine.next_property_id();
    struct Noop
    {
        static void run(void*) {}
    };
    const PropertyId dependencies[] = {source, source};
    const BindingId binding = engine.bind_static(target, dependencies, &Noop::run, nullptr);
    // act
    const bool removed = engine.unbind(binding);
    engine.notify_changed(source);
    // assert
    EXPECT_TRUE(removed);
    EXPECT_EQ(engine.dependent_count(source), 0u);
    EXPECT_EQ(engine.dirty_count(), 0u);
    EXPECT_EQ(engine.settle(), 0u);
}

TEST(BindingEngine, AnUnboundPropertyNoLongerHasDependents) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId source{.base = base, .slot = 0};
    const PropertyId target = engine.next_property_id();
    const BindingId binding = engine.bind(target, {source}, [] {});
    // act
    engine.unbind(binding);
    // assert
    EXPECT_EQ(engine.dependent_count(source), 0u);
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, BindingASecondTimeOnOneTargetReplacesTheFirst) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId target = engine.next_property_id();
    const BindingId first = engine.bind(target, {}, [] {});
    // act
    const BindingId second = engine.bind(target, {}, [] {});
    // assert
    EXPECT_EQ(engine.size(), 1u);
    EXPECT_NE(first, second);
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, AReplacedBindingReportsTheNewHandleAsBound) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId target = engine.next_property_id();
    const BindingId first = engine.bind(target, {}, [] {});
    // act
    const BindingId second = engine.bind(target, {}, [] {});
    // assert
    EXPECT_FALSE(engine.bound(first));
    EXPECT_TRUE(engine.bound(second));
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, AFreshBindingIsDirtyUntilItSettles) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId target = engine.next_property_id();
    // act
    const BindingId binding = engine.bind(target, {}, [] {});
    // assert
    EXPECT_TRUE(engine.bound(binding));
    EXPECT_EQ(engine.dirty_count(), 1u);
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, SettlingEvaluatesTheDirtyBinding) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId target = engine.next_property_id();
    std::size_t runs = 0;
    const BindingId binding = engine.bind(target, {}, [&runs] { runs++; });
    // act
    engine.settle();
    // assert
    EXPECT_EQ(runs, 1u);
    EXPECT_TRUE(engine.bound(binding));
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, SettlingCountsItsEvaluation) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId target = engine.next_property_id();
    const BindingId binding = engine.bind(target, {}, [] {});
    // act
    engine.settle();
    // assert
    EXPECT_EQ(engine.evaluation_count(), 1u);
    EXPECT_TRUE(engine.bound(binding));
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, ASettledGraphHasNothingDirty) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId target = engine.next_property_id();
    const BindingId binding = engine.bind(target, {}, [] {});
    engine.settle();
    // act
    const std::size_t dirty = engine.dirty_count();
    // assert
    EXPECT_EQ(dirty, 0u);
    EXPECT_TRUE(engine.bound(binding));
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, SettlingAnAlreadyCleanGraphRunsNothing) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId target = engine.next_property_id();
    std::size_t runs = 0;
    const BindingId binding = engine.bind(target, {}, [&runs] { runs++; });
    engine.settle();
    // act
    engine.settle();
    // assert
    EXPECT_EQ(runs, 1u);
    EXPECT_TRUE(engine.bound(binding));
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, AChangedSourceMarksItsConsumerDirty) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId source{.base = base, .slot = 0};
    const PropertyId target = engine.next_property_id();
    const BindingId binding = engine.bind(target, {source}, [] {});
    engine.settle();
    // act
    engine.notify_changed(source);
    // assert
    EXPECT_EQ(engine.dirty_count(), 1u);
    EXPECT_TRUE(engine.bound(binding));
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, AChangedSourceReEvaluatesItsConsumer) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId source{.base = base, .slot = 0};
    const PropertyId target = engine.next_property_id();
    std::size_t runs = 0;
    const BindingId binding = engine.bind(target, {source}, [&runs] { runs++; });
    engine.settle();
    engine.notify_changed(source);
    // act
    engine.settle();
    // assert
    EXPECT_EQ(runs, 2u);
    EXPECT_TRUE(engine.bound(binding));
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, AChangedPropertyNothingDependsOnSettlesNothing) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId lonely{.base = base, .slot = 9};
    const PropertyId target = engine.next_property_id();
    std::size_t runs = 0;
    const BindingId binding = engine.bind(target, {}, [&runs] { runs++; });
    engine.settle();
    // act
    engine.notify_changed(lonely);
    engine.settle();
    // assert
    EXPECT_EQ(runs, 1u);
    EXPECT_TRUE(engine.bound(binding));
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, InvalidatingOneBindingMarksItDirty) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId target = engine.next_property_id();
    const BindingId binding = engine.bind(target, {}, [] {});
    engine.settle();
    // act
    engine.invalidate(binding);
    // assert
    EXPECT_EQ(engine.dirty_count(), 1u);
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, InvalidatingAStaleHandleMarksNothing) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId target = engine.next_property_id();
    const BindingId binding = engine.bind(target, {}, [] {});
    engine.settle();
    // act
    engine.invalidate(77);
    // assert
    EXPECT_EQ(engine.dirty_count(), 0u);
    EXPECT_TRUE(engine.bound(binding));
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, AProducerEvaluatesBeforeItsConsumer) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId source{.base = base, .slot = 0};
    const PropertyId middle = engine.next_property_id();
    const PropertyId derived = engine.next_property_id();
    std::vector<std::size_t> order;
    const BindingId middle_binding = engine.bind(middle, {source}, [&order] { order.push_back(1); });
    const BindingId derived_binding = engine.bind(derived, {middle}, [&order] { order.push_back(2); });
    // act
    engine.settle();
    // assert
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1u);
    EXPECT_EQ(order[1], 2u);
    EXPECT_TRUE(engine.bound(middle_binding));
    EXPECT_TRUE(engine.bound(derived_binding));
}

TEST(BindingEngine, ADiamondSettlesItsTipExactlyOnce) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId root{.base = base, .slot = 0};
    const PropertyId left = engine.next_property_id();
    const PropertyId right = engine.next_property_id();
    const PropertyId tip = engine.next_property_id();
    std::size_t tip_runs = 0;
    const BindingId left_binding = engine.bind(left, {root}, [] {});
    const BindingId right_binding = engine.bind(right, {root}, [] {});
    const BindingId tip_binding = engine.bind(tip, {left, right}, [&tip_runs] { tip_runs++; });
    engine.settle();
    // act
    engine.notify_changed(root);
    engine.settle();
    // assert
    EXPECT_EQ(tip_runs, 2u);
    EXPECT_EQ(engine.dirty_count(), 0u);
    EXPECT_TRUE(engine.bound(left_binding));
    EXPECT_TRUE(engine.bound(right_binding));
    EXPECT_TRUE(engine.bound(tip_binding));
}

TEST(BindingEngine, ADiamondSeesBothOfItsInputsSettledFirst) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId root{.base = base, .slot = 0};
    const PropertyId left = engine.next_property_id();
    const PropertyId right = engine.next_property_id();
    const PropertyId tip = engine.next_property_id();
    std::vector<std::size_t> order;
    const BindingId left_binding = engine.bind(left, {root}, [&order] { order.push_back(1); });
    const BindingId right_binding = engine.bind(right, {root}, [&order] { order.push_back(2); });
    const BindingId tip_binding = engine.bind(tip, {left, right}, [&order] { order.push_back(3); });
    // act
    engine.settle();
    // assert
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[2], 3u);
    EXPECT_TRUE(engine.bound(left_binding));
    EXPECT_TRUE(engine.bound(right_binding));
    EXPECT_TRUE(engine.bound(tip_binding));
}

TEST(BindingEngine, AThreeLevelChainSettlesFromTheRootDown) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId root{.base = base, .slot = 0};
    const PropertyId middle = engine.next_property_id();
    const PropertyId leaf = engine.next_property_id();
    std::vector<std::size_t> order;
    const BindingId middle_binding = engine.bind(middle, {root}, [&order] { order.push_back(1); });
    const BindingId leaf_binding = engine.bind(leaf, {middle}, [&order] { order.push_back(2); });
    // act
    engine.settle();
    // assert
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1u);
    EXPECT_EQ(order[1], 2u);
    EXPECT_TRUE(engine.bound(middle_binding));
    EXPECT_TRUE(engine.bound(leaf_binding));
}

TEST(BindingEngine, AProducerThatWritesItsOwnSourceStillConverges) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId loop{.base = base, .slot = 0};
    std::size_t runs = 0;
    // the evaluator writes the property it reads, which is the degenerate cycle of §7.3
    const BindingId binding = engine.bind(loop, {loop}, [&engine, &runs, loop] {
        runs++;
        engine.notify_changed(loop);
    });
    // act
    engine.settle();
    // assert
    EXPECT_LT(runs, BindingEngine::EVALUATION_BUDGET + 1u);
    EXPECT_TRUE(engine.bound(binding));
}

TEST(BindingEngine, ATwoBindingCycleStopsAtTheEvaluationBudget) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId first{.base = base, .slot = 0};
    const PropertyId second{.base = base, .slot = 1};
    // each binding writes the property the other reads, which is a true dynamic cycle (§7.3)
    const BindingId first_binding = engine.bind(first, {second}, [&engine, second] { engine.notify_changed(second); });
    const BindingId second_binding = engine.bind(second, {first}, [&engine, first] { engine.notify_changed(first); });
    // act
    const std::size_t passes = engine.settle();
    // assert
    EXPECT_EQ(passes, BindingEngine::EVALUATION_BUDGET);
    EXPECT_TRUE(engine.bound(first_binding));
    EXPECT_TRUE(engine.bound(second_binding));
}

TEST(BindingEngine, ATwoBindingCycleIsFlagged) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId first{.base = base, .slot = 0};
    const PropertyId second{.base = base, .slot = 1};
    const BindingId first_binding = engine.bind(first, {second}, [&engine, second] { engine.notify_changed(second); });
    const BindingId second_binding = engine.bind(second, {first}, [&engine, first] { engine.notify_changed(first); });
    // act
    engine.settle();
    // assert
    EXPECT_TRUE(engine.cycle_detected());
    EXPECT_TRUE(engine.bound(first_binding));
    EXPECT_TRUE(engine.bound(second_binding));
}

TEST(BindingEngine, ACycleLeavesWorkBehindAfterTheBudget) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId first{.base = base, .slot = 0};
    const PropertyId second{.base = base, .slot = 1};
    const BindingId first_binding = engine.bind(first, {second}, [&engine, second] { engine.notify_changed(second); });
    const BindingId second_binding = engine.bind(second, {first}, [&engine, first] { engine.notify_changed(first); });
    // act
    engine.settle();
    // assert
    EXPECT_GT(engine.dirty_count(), 0u);
    EXPECT_TRUE(engine.bound(first_binding));
    EXPECT_TRUE(engine.bound(second_binding));
}

TEST(BindingEngine, AnAcyclicSettleIsNotFlaggedAsACycle) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId source{.base = base, .slot = 0};
    const PropertyId target = engine.next_property_id();
    const BindingId binding = engine.bind(target, {source}, [] {});
    // act
    engine.settle();
    // assert
    EXPECT_FALSE(engine.cycle_detected());
    EXPECT_TRUE(engine.bound(binding));
    EXPECT_EQ(target.base, base);
}

TEST(BindingEngine, TheCycleFlagCanBeClearedBetweenCases) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId first{.base = base, .slot = 0};
    const PropertyId second{.base = base, .slot = 1};
    const BindingId first_binding = engine.bind(first, {second}, [&engine, second] { engine.notify_changed(second); });
    const BindingId second_binding = engine.bind(second, {first}, [&engine, first] { engine.notify_changed(first); });
    engine.settle();
    // act
    engine.clear_cycle();
    // assert
    EXPECT_FALSE(engine.cycle_detected());
    EXPECT_TRUE(engine.bound(first_binding));
    EXPECT_TRUE(engine.bound(second_binding));
}

TEST(BindingEngine, TheEvaluationBudgetIsSixtyFour) {
    // arrange, act
    const std::size_t budget = BindingEngine::EVALUATION_BUDGET;
    // assert
    EXPECT_EQ(budget, 64u);
}

TEST(BindingEngine, AnEvaluatorCanRegisterAnotherBindingWhileItRuns) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId first = engine.next_property_id();
    const PropertyId second = engine.next_property_id();
    std::size_t spawned_runs = 0;
    bool registered = false;
    const BindingId binding = engine.bind(first, {}, [&engine, &registered, &spawned_runs, second] {
        // the outer evaluator registers a binding while it is running (§7.3)
        registered = engine.bind(second, {}, [&spawned_runs] { spawned_runs++; }) != 0;
    });
    // act
    engine.settle();
    // assert
    EXPECT_TRUE(registered);
    EXPECT_TRUE(engine.bound(binding));
    EXPECT_EQ(engine.size(), 2u);
    EXPECT_EQ(spawned_runs, 1u);
}

TEST(BindingEngine, AnEvaluatorCanRebindItsOwnTargetWhileItRuns) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId target = engine.next_property_id();
    std::size_t runs = 0;
    bool rebound = false;
    const std::shared_ptr<int> state = std::make_shared<int>(7);
    const BindingId binding = engine.bind(target, {}, [&engine, &rebound, &runs, target, state] {
        // the outer evaluator replaces itself while it is running (§7.3)
        runs++;
        const BindingId replacement = engine.bind(target, {}, [&runs] { runs++; });
        // the capture is read after the rebind; ASan proves the closure stays alive (§7.3)
        rebound = replacement != 0 && *state == 7;
    });
    // act
    engine.settle();
    // assert
    EXPECT_TRUE(rebound);
    EXPECT_FALSE(engine.bound(binding));
    EXPECT_EQ(runs, 2u);
}

TEST(BindingEngine, AChainOfManyBindingsSettlesInOnePass) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId root{.base = base, .slot = 0};
    std::vector<PropertyId> chain;
    chain.push_back(root);
    for (int index = 0; index < 8; index++)
        chain.push_back(engine.next_property_id());
    std::size_t last_runs = 0;
    const BindingId last_binding = engine.bind(chain.back(), {chain[chain.size() - 2]}, [&last_runs] { last_runs++; });
    BindingId middle_binding = 0;
    for (std::size_t index = 1; index + 1 < chain.size(); index++)
        middle_binding = engine.bind(chain[index], {chain[index - 1]}, [] {});
    // act
    const std::size_t passes = engine.settle();
    // assert
    EXPECT_EQ(passes, 1u);
    EXPECT_EQ(last_runs, 1u);
    EXPECT_TRUE(engine.bound(middle_binding));
    EXPECT_TRUE(engine.bound(last_binding));
}

TEST(BindingEngine, TwoComponentsWithTheSameSlotDoNotShareDependencies) {
    // arrange
    BindingEngine engine;
    const std::uint32_t first_base = engine.begin_component();
    const PropertyId first_source{.base = first_base, .slot = 0};
    const PropertyId first_target = engine.next_property_id();
    const std::uint32_t second_base = engine.begin_component();
    const PropertyId second_source{.base = second_base, .slot = 0};
    const PropertyId second_target = engine.next_property_id();
    std::size_t first_runs = 0;
    std::size_t second_runs = 0;
    const BindingId first_binding = engine.bind(first_target, {first_source}, [&first_runs] { first_runs++; });
    const BindingId second_binding = engine.bind(second_target, {second_source}, [&second_runs] { second_runs++; });
    engine.settle();
    // act
    engine.notify_changed(first_source);
    engine.settle();
    // assert
    EXPECT_EQ(first_runs, 2u);
    EXPECT_EQ(second_runs, 1u);
    EXPECT_TRUE(engine.bound(first_binding));
    EXPECT_TRUE(engine.bound(second_binding));
}

TEST(Property, ADefaultPropertyReadsItsDefaultValue) {
    // arrange, act
    Property<int> property;
    // assert
    EXPECT_EQ(property.get(), 0);
}

TEST(Property, AConstructedPropertyReadsItsInitialValue) {
    // arrange, act
    const Property<int> property(7);
    // assert
    EXPECT_EQ(property.get(), 7);
}

TEST(Property, GetConvertsToTheValue) {
    // arrange
    const Property<int> property(42);
    // act
    const int value = property;
    // assert
    EXPECT_EQ(value, 42);
}

TEST(Property, SetChangesThePresentedValue) {
    // arrange
    Property<int> property(1);
    // act
    property.set(9);
    // assert
    EXPECT_EQ(property.get(), 9);
}

TEST(Property, SettingTheSameValueChangesNothing) {
    // arrange
    Property<int> property(3);
    // act
    property.set(3);
    // assert
    EXPECT_EQ(property.get(), 3);
}

TEST(Property, AssignmentWritesThroughTheOperator) {
    // arrange
    Property<int> property(1);
    // act
    property = 5;
    // assert
    EXPECT_EQ(property.get(), 5);
}

TEST(Property, AnUnassignedPropertyCarriesTheInvalidIdentifier) {
    // arrange, act
    Property<int> property(1);
    // assert
    EXPECT_EQ(property.id(), invalid_property_id());
}

TEST(Property, AnAssignedPropertyCarriesItsIdentifier) {
    // arrange
    Property<int> property(1);
    // act
    property.set_id(PropertyId{.base = 2, .slot = 3});
    // assert
    EXPECT_EQ(property.id(), (PropertyId{.base = 2, .slot = 3}));
}

TEST(Property, AnUnboundPropertyIsNotBound) {
    // arrange, act
    Property<int> property(1);
    // assert
    EXPECT_FALSE(property.bound());
}

TEST(Property, AnUnboundPropertyIsNotOverridden) {
    // arrange, act
    Property<int> property(1);
    // assert
    EXPECT_FALSE(property.overridden());
}

TEST(Property, BindingMakesThePropertyBound) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(1);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    // act
    property.bind([] { return 4; }, {});
    // assert
    EXPECT_TRUE(property.bound());
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, ABoundPropertyTakesTheBindingValue) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(1);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    property.bind([] { return 4; }, {});
    // act
    engine.settle();
    // assert
    EXPECT_EQ(property.get(), 4);
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, ABindingWithoutAnEngineChangesNothing) {
    // arrange
    Property<int> property(1);
    // act
    property.bind([] { return 4; }, {});
    // assert
    EXPECT_FALSE(property.bound());
    EXPECT_EQ(property.get(), 1);
}

TEST(Property, ASettledBindingMarksTheGraphDirtyNoLonger) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(1);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    property.bind([] { return 4; }, {});
    engine.settle();
    // act
    const std::size_t dirty = engine.dirty_count();
    // assert
    EXPECT_EQ(dirty, 0u);
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, ASourceChangeReevaluatesItsConsumerProperty) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId source_id{.base = base, .slot = 0};
    Property<int> source(1);
    source.set_id(source_id);
    source.set_engine(&engine);
    Property<int> doubled(0);
    doubled.set_id(engine.next_property_id());
    doubled.set_engine(&engine);
    doubled.bind([&source] { return source.get() * 2; }, {source_id});
    engine.settle();
    // act
    source.set(5);
    engine.settle();
    // assert
    EXPECT_EQ(doubled.get(), 10);
    EXPECT_EQ(doubled.id().base, base);
}

TEST(Property, AnImperativeSetBreaksTheBinding) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(1);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    property.bind([] { return 4; }, {});
    engine.settle();
    // act
    property.set(99);
    // assert
    EXPECT_FALSE(property.bound());
    EXPECT_TRUE(property.overridden());
    EXPECT_EQ(property.get(), 99);
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, ABrokenBindingNoLongerReevaluates) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(1);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    property.bind([] { return 4; }, {});
    engine.settle();
    property.set(99);
    engine.settle();
    // act
    property.set(100);
    engine.settle();
    // assert
    EXPECT_EQ(property.get(), 100);
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, RebindRestoresTheBinding) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(1);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    property.bind([] { return 4; }, {});
    engine.settle();
    property.set(99);
    // act
    property.rebind();
    engine.settle();
    // assert
    EXPECT_TRUE(property.bound());
    EXPECT_EQ(property.get(), 4);
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, RebindDropsTheImperativeOverride) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(1);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    property.bind([] { return 4; }, {});
    engine.settle();
    property.set(99);
    property.rebind();
    engine.settle();
    // act
    const bool overridden = property.overridden();
    // assert
    EXPECT_FALSE(overridden);
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, RebindWithoutAnEngineChangesNothing) {
    // arrange
    Property<int> property(1);
    property.set(5);
    // act
    property.rebind();
    // assert
    EXPECT_EQ(property.get(), 5);
}

TEST(Property, RebindWithoutABindingChangesNothing) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(1);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    property.set(5);
    // act
    property.rebind();
    // assert
    EXPECT_EQ(property.get(), 5);
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, RebindRecomputesFromTheCurrentSourceValue) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId source_id{.base = base, .slot = 0};
    Property<int> source(1);
    source.set_id(source_id);
    source.set_engine(&engine);
    Property<int> doubled(0);
    doubled.set_id(engine.next_property_id());
    doubled.set_engine(&engine);
    doubled.bind([&source] { return source.get() * 2; }, {source_id});
    engine.settle();
    doubled.set(0);
    source.set(8);
    // act
    doubled.rebind();
    engine.settle();
    // assert
    EXPECT_EQ(doubled.get(), 16);
    EXPECT_EQ(doubled.id().base, base);
}

TEST(Property, UnbindingLeavesTheLastBoundValue) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(1);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    property.bind([] { return 4; }, {});
    engine.settle();
    // act
    property.unbind();
    // assert
    EXPECT_FALSE(property.bound());
    EXPECT_EQ(property.get(), 4);
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, AnUnboundPropertyIsNoLongerSettled) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(1);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    property.bind([] { return 4; }, {});
    engine.settle();
    property.unbind();
    // act
    const std::size_t dirty = engine.dirty_count();
    // assert
    EXPECT_EQ(dirty, 0u);
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, UnbindingWithoutAnEngineChangesNothing) {
    // arrange
    Property<int> property(1);
    // act
    property.unbind();
    // assert
    EXPECT_FALSE(property.bound());
}

TEST(Property, UnbindingTwiceChangesNothingTheSecondTime) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(1);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    property.bind([] { return 4; }, {});
    engine.settle();
    property.unbind();
    // act
    property.unbind();
    // assert
    EXPECT_EQ(property.get(), 4);
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, DestroyingABoundPropertyRemovesItsEngineBinding) {
    // arrange
    BindingEngine engine;
    (void)engine.begin_component();
    Property<int> source(1);
    source.set_id(engine.next_property_id());
    source.set_engine(&engine);
    const PropertyId source_id = source.id();
    {
        Property<int> property(1);
        property.set_id(engine.next_property_id());
        property.set_engine(&engine);
        property.bind([&source] { return source.get() * 2; }, {source_id});
        engine.settle();
        ASSERT_EQ(engine.size(), 1u);
    }
    // act
    const std::size_t evaluations = engine.evaluation_count();
    source.set(3);
    engine.settle();
    // assert the source change had no binding to dirty, so a later settle finds no work (§7.3)
    EXPECT_EQ(engine.size(), 0u);
    EXPECT_EQ(engine.dirty_count(), 0u);
    EXPECT_EQ(engine.evaluation_count(), evaluations);
    EXPECT_EQ(engine.dependent_count(source_id), 0u);
}

TEST(Property, DestroyingAStaticallyBoundPropertyRemovesItsEngineBinding) {
    // arrange
    BindingEngine engine;
    (void)engine.begin_component();
    Property<int> source(2);
    source.set_id(engine.next_property_id());
    source.set_engine(&engine);
    struct Evaluator
    {
        static int evaluate(void* context) { return static_cast<Property<int>*>(context)->get() * 2; }
    };
    // the one member source is the dependency array, which the engine reads as a span (§13.3)
    const PropertyId deps[] = {source.id()};
    {
        Property<int> property(1);
        property.set_id(engine.next_property_id());
        property.set_engine(&engine);
        property.bind_static(&Evaluator::evaluate, &source, deps);
        engine.settle();
        ASSERT_EQ(property.get(), 4);
    }
    // act
    source.set(5);
    engine.settle();
    // assert the static context went with the property, so no evaluator reaches it (§13.3)
    EXPECT_EQ(engine.size(), 0u);
    EXPECT_EQ(engine.dirty_count(), 0u);
    EXPECT_EQ(engine.dependent_count(source.id()), 0u);
}

TEST(Property, DestroyingOneBoundPropertyLeavesItsSiblingBindingIntact) {
    // arrange
    BindingEngine engine;
    (void)engine.begin_component();
    Property<int> source(1);
    source.set_id(engine.next_property_id());
    source.set_engine(&engine);
    const PropertyId source_id = source.id();
    Property<int> survivor(0);
    survivor.set_id(engine.next_property_id());
    survivor.set_engine(&engine);
    survivor.bind([&source] { return source.get() * 2; }, {source_id});
    {
        Property<int> doomed(0);
        doomed.set_id(engine.next_property_id());
        doomed.set_engine(&engine);
        doomed.bind([&source] { return source.get() * 3; }, {source_id});
        engine.settle();
        ASSERT_EQ(engine.size(), 2u);
    }
    // act
    source.set(4);
    engine.settle();
    // assert the surviving property still settled and only its binding remains (§7.3)
    EXPECT_EQ(engine.size(), 1u);
    EXPECT_EQ(engine.dependent_count(source_id), 1u);
    EXPECT_EQ(survivor.get(), 8);
}

TEST(Property, AnExplicitlyUnboundPropertyDestroyedDoesNotReleaseItsSlotTwice) {
    // arrange
    BindingEngine engine;
    (void)engine.begin_component();
    {
        Property<int> property(1);
        property.set_id(engine.next_property_id());
        property.set_engine(&engine);
        property.bind([] { return 4; }, {});
        ASSERT_EQ(engine.binding_slot_count(), 1u);
        property.unbind();
        ASSERT_EQ(engine.binding_slot_count(), 1u);
    }
    // act
    Property<int> replacement(2);
    replacement.set_id(engine.next_property_id());
    replacement.set_engine(&engine);
    replacement.bind([] { return 8; }, {});
    engine.settle();
    // assert the destructor did not queue the slot a second time, so one replacement reuses it (§7.3)
    EXPECT_EQ(engine.binding_slot_count(), 1u);
    EXPECT_EQ(engine.size(), 1u);
    EXPECT_EQ(replacement.get(), 8);
}

TEST(Property, DestroyingAPropertyDuringSettleLeavesNoWorkBehind) {
    // arrange
    BindingEngine engine;
    (void)engine.begin_component();
    auto doomed = std::make_unique<Property<int>>(1);
    doomed->set_id(engine.next_property_id());
    doomed->set_engine(&engine);
    Property<int> trigger(0);
    trigger.set_id(engine.next_property_id());
    trigger.set_engine(&engine);
    doomed->bind([&trigger] { return trigger.get() * 2; }, {trigger.id()});
    trigger.bind(
        [&doomed] {
            // the trigger destroys the consumer while the settle walk still holds its handle (§7.3)
            doomed.reset();
            return 7;
        },
        {});
    // act
    engine.settle();
    // assert the dead handle was skipped and only the live trigger binding remains (§7.3)
    EXPECT_EQ(doomed, nullptr);
    EXPECT_EQ(engine.size(), 1u);
    EXPECT_EQ(engine.dirty_count(), 0u);
    EXPECT_EQ(engine.evaluation_count(), 1u);
    EXPECT_EQ(trigger.get(), 7);
}

TEST(Property, ADestroyedPropertyReturnsItsBindingSlotForReuse) {
    // arrange
    BindingEngine engine;
    (void)engine.begin_component();
    {
        Property<int> property(1);
        property.set_id(engine.next_property_id());
        property.set_engine(&engine);
        property.bind([] { return 4; }, {});
        ASSERT_EQ(engine.binding_slot_count(), 1u);
    }
    Property<int> replacement(2);
    replacement.set_id(engine.next_property_id());
    replacement.set_engine(&engine);
    // act
    replacement.bind([] { return 8; }, {});
    engine.settle();
    // assert the slot was reused rather than grown, and the replacement settled (§7.3)
    EXPECT_EQ(engine.binding_slot_count(), 1u);
    EXPECT_EQ(engine.size(), 1u);
    EXPECT_EQ(replacement.get(), 8);
}

TEST(Property, AnAnimationStartsInactive) {
    // arrange, act
    Property<int> property(1);
    // assert
    EXPECT_FALSE(property.presence().active);
    EXPECT_EQ(property.presence().frames, 0u);
}

TEST(Property, AnimatingOpensThePresenceChannel) {
    // arrange
    Property<int> property(1);
    // act
    property.animate(5);
    // assert
    EXPECT_TRUE(property.presence().active);
}

TEST(Property, AnAnimatedValueIsPresentedOverTheBinding) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(1);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    property.bind([] { return 4; }, {});
    engine.settle();
    // act
    property.animate(5);
    // assert
    EXPECT_EQ(property.get(), 5);
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, AnimatingCountsTheFrames) {
    // arrange
    Property<int> property(1);
    // act
    property.animate(2);
    property.animate(3);
    // assert
    EXPECT_EQ(property.presence().frames, 2u);
}

TEST(Property, EndingTheAnimationRestoresTheBindingValue) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(1);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    property.bind([] { return 4; }, {});
    engine.settle();
    property.animate(5);
    // act
    property.end_animation();
    // assert
    EXPECT_FALSE(property.presence().active);
    EXPECT_EQ(property.get(), 4);
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, AnAnimationNeverClobbersAnImperativeOverride) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(1);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    property.bind([] { return 4; }, {});
    engine.settle();
    property.set(99);
    // act
    property.animate(5);
    // assert
    EXPECT_EQ(property.get(), 99);
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, EndingAnAnimationLeavesAnImperativeOverrideIntact) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(1);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    property.bind([] { return 4; }, {});
    engine.settle();
    property.set(99);
    property.animate(5);
    // act
    property.end_animation();
    // assert
    EXPECT_EQ(property.get(), 99);
    EXPECT_TRUE(property.overridden());
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, EndingAnAbsentAnimationChangesNothing) {
    // arrange
    Property<int> property(1);
    // act
    property.end_animation();
    // assert
    EXPECT_EQ(property.presence().frames, 0u);
    EXPECT_EQ(property.get(), 1);
}

TEST(Property, AnAnimationMarksTheGraphSoConsumersFollowIt) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId animated_id{.base = base, .slot = 0};
    Property<int> animated(1);
    animated.set_id(animated_id);
    animated.set_engine(&engine);
    Property<int> mirrored(0);
    mirrored.set_id(engine.next_property_id());
    mirrored.set_engine(&engine);
    mirrored.bind([&animated] { return animated.get(); }, {animated_id});
    engine.settle();
    // act
    animated.animate(5);
    engine.settle();
    // assert
    EXPECT_EQ(mirrored.get(), 5);
    EXPECT_EQ(mirrored.id().base, base);
}

TEST(Property, AStringPropertyCarriesText) {
    // arrange
    Property<std::string> property(std::string{"albor"});
    // act
    property.set("reactive");
    // assert
    EXPECT_EQ(property.get(), "reactive");
}

TEST(Property, AColorPropertyCarriesComponents) {
    // arrange
    Property<Color> property(Color::rgb(0.0f, 0.0f, 0.0f));
    // act
    property.animate(Color::rgb(1.0f, 0.5f, 0.25f));
    // assert
    EXPECT_EQ(property.get(), (Color::rgb(1.0f, 0.5f, 0.25f)));
}

TEST(Property, APropertyIsNotCopyable) {
    // arrange, act, assert
    EXPECT_FALSE(std::is_copy_constructible_v<Property<int>>);
}

TEST(Property, APropertyIsNotMovable) {
    // arrange, act, assert
    EXPECT_FALSE(std::is_move_constructible_v<Property<int>>);
}

TEST(Property, AnImperativeWriteReportsThePropertyOverridden) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(1);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    // act
    property.set(3);
    // assert
    EXPECT_TRUE(property.overridden());
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, ANewlyBoundPropertyIsNotOverridden) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(1);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    property.bind([] { return 2; }, {});
    // act
    const bool overridden = property.overridden();
    // assert
    EXPECT_FALSE(overridden);
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, ABindingBodyThatWritesNothingKeepsThePreviousValue) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property(6);
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    property.bind({}, {});
    // act
    engine.settle();
    // assert
    EXPECT_EQ(property.get(), 6);
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, AChainOfPropertiesSettlesFromTheSourceDown) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId a_id{.base = base, .slot = 0};
    Property<int> a(1);
    a.set_id(a_id);
    a.set_engine(&engine);
    Property<int> b(0);
    b.set_id(engine.next_property_id());
    b.set_engine(&engine);
    b.bind([&a] { return a.get() + 1; }, {a_id});
    Property<int> c(0);
    c.set_id(engine.next_property_id());
    c.set_engine(&engine);
    c.bind([&b] { return b.get() * 10; }, {b.id()});
    engine.settle();
    // act
    a.set(4);
    engine.settle();
    // assert
    EXPECT_EQ(b.get(), 5);
    EXPECT_EQ(c.get(), 50);
    EXPECT_EQ(c.id().base, base);
}

TEST(Property, AChainedDefaultUpdatesItsConsumer) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    Property<int> height(1);
    height.set_id(engine.next_property_id());
    height.set_engine(&engine);
    Property<int> width(0);
    width.set_id(engine.next_property_id());
    width.set_engine(&engine);
    width.bind([&height] { return height.get() * 2; }, {height.id()});
    Property<int> area(0);
    area.set_id(engine.next_property_id());
    area.set_engine(&engine);
    area.bind([&width] { return width.get() * width.get(); }, {width.id()});
    engine.settle();
    // act
    height.set(3);
    engine.settle();
    // assert
    EXPECT_EQ(width.get(), 6);
    EXPECT_EQ(area.get(), 36);
}

TEST(Property, AReboundTargetsConsumerSeesTheReplacement) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    Property<int> source(1);
    source.set_id(engine.next_property_id());
    source.set_engine(&engine);
    Property<int> target(0);
    target.set_id(engine.next_property_id());
    target.set_engine(&engine);
    target.bind([&source] { return source.get() + 1; }, {source.id()});
    Property<int> consumer(0);
    consumer.set_id(engine.next_property_id());
    consumer.set_engine(&engine);
    consumer.bind([&target] { return target.get() * 2; }, {target.id()});
    engine.settle();
    // act
    target.unbind();
    target.bind([] { return 40; }, {});
    engine.settle();
    // assert
    EXPECT_EQ(target.get(), 40);
    EXPECT_EQ(consumer.get(), 80);
}

TEST(Property, AStaticBindingPropagatesToItsConsumer) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    Property<int> root(1);
    root.set_id(engine.next_property_id());
    root.set_engine(&engine);
    struct Increment
    {
        static int evaluate(void* raw) { return static_cast<Property<int>*>(raw)->get() + 1; }
    };
    Property<int> middle(0);
    middle.set_id(engine.next_property_id());
    middle.set_engine(&engine);
    const PropertyId middle_dependencies[] = {root.id()};
    middle.bind_static(&Increment::evaluate, &root, middle_dependencies);
    struct Scale
    {
        static int evaluate(void* raw) { return static_cast<Property<int>*>(raw)->get() * 10; }
    };
    Property<int> leaf(0);
    leaf.set_id(engine.next_property_id());
    leaf.set_engine(&engine);
    const PropertyId leaf_dependencies[] = {middle.id()};
    leaf.bind_static(&Scale::evaluate, &middle, leaf_dependencies);
    engine.settle();
    // act
    root.set(5);
    const std::size_t passes = engine.settle();
    // assert
    EXPECT_EQ(middle.get(), 6);
    EXPECT_EQ(leaf.get(), 60);
    EXPECT_EQ(passes, 1u);
    EXPECT_EQ(engine.dirty_count(), 0u);
    EXPECT_FALSE(engine.cycle_detected());
}

TEST(Property, ADeepBoundChainConvergesInOnePass) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    constexpr int depth = 65;
    std::vector<std::unique_ptr<Property<int>>> chain;
    for (int index = 0; index <= depth; index++) {
        chain.push_back(std::make_unique<Property<int>>(0));
        chain.back()->set_id(engine.next_property_id());
        chain.back()->set_engine(&engine);
    }
    chain[0]->set(100);
    for (int index = 1; index <= depth; index++) {
        Property<int>* previous = chain[static_cast<std::size_t>(index - 1)].get();
        chain[static_cast<std::size_t>(index)]->bind([previous] { return previous->get() + 1; }, {previous->id()});
    }
    engine.settle();
    // act
    chain[0]->set(200);
    const std::size_t passes = engine.settle();
    // assert
    EXPECT_EQ(chain[static_cast<std::size_t>(depth)]->get(), 265);
    EXPECT_EQ(passes, 1u);
    EXPECT_EQ(engine.dirty_count(), 0u);
    EXPECT_FALSE(engine.cycle_detected());
}

TEST(Property, ABindingThatReadsItsOwnTargetIsFlaggedAsACycle) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    Property<int> loop(0);
    loop.set_id(engine.next_property_id());
    loop.set_engine(&engine);
    loop.bind([&loop] { return loop.get() + 1; }, {loop.id()});
    // act
    const std::size_t passes = engine.settle();
    // assert
    EXPECT_EQ(passes, BindingEngine::EVALUATION_BUDGET);
    EXPECT_TRUE(engine.cycle_detected());
    EXPECT_GT(engine.dirty_count(), 0u);
}

TEST(Property, RebindAfterAnOverrideUpdatesTheConsumer) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    Property<int> source(1);
    source.set_id(engine.next_property_id());
    source.set_engine(&engine);
    Property<int> target(0);
    target.set_id(engine.next_property_id());
    target.set_engine(&engine);
    target.bind([&source] { return source.get() + 1; }, {source.id()});
    Property<int> consumer(0);
    consumer.set_id(engine.next_property_id());
    consumer.set_engine(&engine);
    consumer.bind([&target] { return target.get() * 2; }, {target.id()});
    engine.settle();
    target.set(99);
    engine.settle();
    // act
    target.rebind();
    engine.settle();
    // assert
    EXPECT_EQ(target.get(), 2);
    EXPECT_EQ(consumer.get(), 4);
    EXPECT_EQ(engine.dirty_count(), 0u);
}

TEST(Property, ADiamondOfPropertiesSettlesItsTipOnce) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    const PropertyId root_id{.base = base, .slot = 0};
    Property<int> root(2);
    root.set_id(root_id);
    root.set_engine(&engine);
    Property<int> left(0);
    left.set_id(engine.next_property_id());
    left.set_engine(&engine);
    left.bind([&root] { return root.get() + 1; }, {root_id});
    Property<int> right(0);
    right.set_id(engine.next_property_id());
    right.set_engine(&engine);
    right.bind([&root] { return root.get() * 3; }, {root_id});
    Property<int> tip(0);
    tip.set_id(engine.next_property_id());
    tip.set_engine(&engine);
    tip.bind([&left, &right] { return left.get() + right.get(); }, {left.id(), right.id()});
    engine.settle();
    // act
    root.set(4);
    engine.settle();
    // assert
    EXPECT_EQ(tip.get(), 17);
    EXPECT_EQ(tip.id().base, base);
}

TEST(Observable, AConstructedObservableReadsItsInitialValue) {
    // arrange, act
    const Observable<int> observable(3);
    // assert
    EXPECT_EQ(observable.get(), 3);
}

TEST(Observable, SetChangesThePresentedValue) {
    // arrange
    Observable<int> observable(3);
    // act
    observable.set(8);
    // assert
    EXPECT_EQ(observable.get(), 8);
}

TEST(Observable, AnObservableWithoutAPosterCannotPost) {
    // arrange
    Observable<int> observable(3);
    // act
    const bool posted = observable.post_set(9);
    // assert
    EXPECT_FALSE(posted);
    EXPECT_EQ(observable.get(), 3);
}

TEST(Observable, AnObservableWithoutAPosterIsNotPostable) {
    // arrange, act
    Observable<int> observable(3);
    // assert
    EXPECT_FALSE(observable.postable());
}

TEST(Observable, APostedMutationWaitsForThePosterToRunIt) {
    // arrange
    Observable<int> observable(3);
    std::deque<PostedWork> queue;
    observable.set_poster([&queue](PostedWork work) { queue.push_back(std::move(work)); });
    // act
    const bool posted = observable.post_set(9);
    // assert
    EXPECT_TRUE(posted);
    EXPECT_EQ(observable.get(), 3);
    EXPECT_EQ(queue.size(), 1u);
}

TEST(Observable, ThePostedMutationLandsWhenThePosterRunsIt) {
    // arrange
    Observable<int> observable(3);
    std::deque<PostedWork> queue;
    observable.set_poster([&queue](PostedWork work) { queue.push_back(std::move(work)); });
    observable.post_set(9);
    // act
    queue.front()();
    queue.pop_front();
    // assert
    EXPECT_EQ(observable.get(), 9);
}

TEST(Observable, PostingCountsTheHandoff) {
    // arrange
    Observable<int> observable(3);
    std::deque<PostedWork> queue;
    observable.set_poster([&queue](PostedWork work) { queue.push_back(std::move(work)); });
    // act
    observable.post_set(9);
    // assert
    EXPECT_EQ(observable.post_count(), 1u);
}

TEST(Observable, InstallingAPosterMakesTheObservablePostable) {
    // arrange
    Observable<int> observable(3);
    std::deque<PostedWork> queue;
    // act
    observable.set_poster([&queue](PostedWork work) { queue.push_back(std::move(work)); });
    // assert
    EXPECT_TRUE(observable.postable());
}

TEST(Observable, TheSnapshotIsHandedOutAsAConstPointer) {
    // arrange
    Observable<int> observable(3);
    // act
    const Snapshot<int> snapshot = observable.snapshot();
    // assert
    EXPECT_EQ(*snapshot, 3);
    EXPECT_TRUE((std::is_const_v<std::remove_reference_t<decltype(*snapshot)>>));
}

TEST(Observable, ASetReplacesTheSnapshotRatherThanMutatingIt) {
    // arrange
    Observable<int> observable(3);
    const Snapshot<int> before = observable.snapshot();
    // act
    observable.set(4);
    // assert
    EXPECT_EQ(*before, 3);
    EXPECT_EQ(observable.get(), 4);
}

TEST(Observable, AMutableSnapshotIsIndependentOfTheLiveValue) {
    // arrange
    Observable<int> observable(3);
    // act
    const std::shared_ptr<int> copy = observable.mutable_snapshot();
    *copy = 50;
    // assert
    EXPECT_EQ(observable.get(), 3);
}

TEST(Observable, ASnapshotSwapReplacesTheWholeValue) {
    // arrange
    Observable<int> observable(3);
    const Snapshot<int> bulk = std::make_shared<const int>(12);
    // act
    observable.set_snapshot(bulk);
    // assert
    EXPECT_EQ(observable.get(), 12);
}

TEST(Observable, AnEmptySnapshotSwapIsRefused) {
    // arrange
    Observable<int> observable(3);
    // act
    observable.set_snapshot(nullptr);
    // assert
    EXPECT_EQ(observable.get(), 3);
}

TEST(Observable, AnObservableBuiltOverASnapshotReadsIt) {
    // arrange
    const Snapshot<int> snapshot = std::make_shared<const int>(7);
    // act
    Observable<int> observable(snapshot);
    // assert
    EXPECT_EQ(observable.get(), 7);
}

TEST(Observable, AnObservableBuiltOverAnEmptySnapshotReadsADefault) {
    // arrange, act
    Observable<int> observable(Snapshot<int>(nullptr));
    // assert
    EXPECT_EQ(observable.get(), 0);
}

TEST(Observable, AWorkerPostedValueReachesTheUiThreadThroughTheQueue) {
    // arrange
    Observable<int> observable(0);
    std::deque<PostedWork> queue;
    observable.set_poster([&queue](PostedWork work) { queue.push_back(std::move(work)); });
    std::thread worker([&observable] { observable.post_set(77); });
    worker.join();
    // act
    while (!queue.empty()) {
        queue.front()();
        queue.pop_front();
    }
    // assert
    EXPECT_EQ(observable.get(), 77);
}

TEST(Observable, SeveralPostedMutationsLandInOrder) {
    // arrange
    Observable<int> observable(0);
    std::deque<PostedWork> queue;
    observable.set_poster([&queue](PostedWork work) { queue.push_back(std::move(work)); });
    observable.post_set(1);
    observable.post_set(2);
    observable.post_set(3);
    // act
    while (!queue.empty()) {
        queue.front()();
        queue.pop_front();
    }
    // assert
    EXPECT_EQ(observable.get(), 3);
    EXPECT_EQ(observable.post_count(), 3u);
}

TEST(Observable, AWorkerPostedSnapshotIsReadOnlyToTheUiThread) {
    // arrange
    Observable<int> observable(0);
    std::deque<PostedWork> queue;
    observable.set_poster([&queue](PostedWork work) { queue.push_back(std::move(work)); });
    std::thread worker([&observable] { observable.post_set(21); });
    worker.join();
    while (!queue.empty()) {
        queue.front()();
        queue.pop_front();
    }
    // act
    const Snapshot<int> snapshot = observable.snapshot();
    // assert
    EXPECT_EQ(*snapshot, 21);
}

TEST(Observable, AStringObservableCarriesText) {
    // arrange
    Observable<std::string> observable(std::string{"start"});
    // act
    observable.set("done");
    // assert
    EXPECT_EQ(observable.get(), "done");
}

TEST(Observable, TwoObservablesWithTheSameValueCompareEqual) {
    // arrange
    Observable<int> first(5);
    Observable<int> second(5);
    // act, assert
    EXPECT_TRUE(first == second);
}

TEST(Observable, TwoObservablesWithDifferentValuesDiffer) {
    // arrange
    Observable<int> first(5);
    Observable<int> second(6);
    // act, assert
    EXPECT_FALSE(first == second);
}

TEST(Observable, AnObservableIsNotCopyable) {
    // arrange, act, assert
    EXPECT_FALSE(std::is_copy_constructible_v<Observable<int>>);
}

TEST(Property, AStaticBindingMakesThePropertyBound) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> property;
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    struct Evaluator
    {
        static int evaluate(void*) { return 6; }
    };
    // act
    property.bind_static(&Evaluator::evaluate, nullptr, {});
    // assert
    EXPECT_TRUE(property.bound());
    EXPECT_EQ(property.id().base, base);
}

TEST(Property, ASettledStaticBindingWritesTheValue) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    Property<int> property;
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    struct Evaluator
    {
        static int evaluate(void*) { return 6; }
    };
    property.bind_static(&Evaluator::evaluate, nullptr, {});
    // act
    engine.settle();
    // assert
    EXPECT_EQ(property.get(), 6);
}

TEST(Property, ASourceChangeReevaluatesItsStaticConsumer) {
    // arrange
    BindingEngine engine;
    const std::uint32_t base = engine.begin_component();
    Property<int> source(2);
    source.set_id(engine.next_property_id());
    source.set_engine(&engine);
    Property<int> doubled;
    doubled.set_id(engine.next_property_id());
    doubled.set_engine(&engine);
    struct Evaluator
    {
        static int evaluate(void* context) { return static_cast<const std::pair<Property<int>*, int>*>(context)->first->get() * 2; }
    };
    std::pair<Property<int>*, int> context{&source, 0};
    // the one member source is the dependency array, which the engine reads as a span (§13.3)
    const PropertyId deps[] = {source.id()};
    doubled.bind_static(&Evaluator::evaluate, &context, deps);
    engine.settle();
    // act
    source.set(5);
    engine.settle();
    // assert
    EXPECT_EQ(doubled.get(), 10);
    EXPECT_EQ(doubled.id().base, base);
}

TEST(Property, AnImperativeSetBreaksTheStaticBinding) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    Property<int> property;
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    struct Evaluator
    {
        static int evaluate(void*) { return 4; }
    };
    property.bind_static(&Evaluator::evaluate, nullptr, {});
    engine.settle();
    // act
    property.set(99);
    // assert
    EXPECT_FALSE(property.bound());
    EXPECT_TRUE(property.overridden());
    EXPECT_EQ(property.get(), 99);
}

TEST(Property, RebindRestoresTheStaticBinding) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    Property<int> property;
    property.set_id(engine.next_property_id());
    property.set_engine(&engine);
    struct Evaluator
    {
        static int evaluate(void*) { return 4; }
    };
    property.bind_static(&Evaluator::evaluate, nullptr, {});
    engine.settle();
    property.set(99);
    // act
    property.rebind();
    engine.settle();
    // assert
    EXPECT_FALSE(property.overridden());
    EXPECT_EQ(property.get(), 4);
}

TEST(Property, AStaticBindingWithoutAnEngineChangesNothing) {
    // arrange
    Property<int> property(1);
    struct Evaluator
    {
        static int evaluate(void*) { return 4; }
    };
    // act
    property.bind_static(&Evaluator::evaluate, nullptr, {});
    // assert
    EXPECT_FALSE(property.bound());
    EXPECT_EQ(property.get(), 1);
}

TEST(BindingEngine, SettlingRunsTheStaticBinding) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId target = engine.next_property_id();
    struct Runner
    {
        static void run(void*) {}
    };
    // act
    const BindingId binding = engine.bind_static(target, {}, &Runner::run, nullptr);
    engine.settle();
    // assert
    EXPECT_TRUE(engine.bound(binding));
    EXPECT_EQ(engine.size(), 1u);
    EXPECT_EQ(engine.dirty_count(), 0u);
}

TEST(BindingEngine, AStaticBindingCarriesItsContext) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId target = engine.next_property_id();
    struct Runner
    {
        static void run(void* context) { (*static_cast<std::size_t*>(context))++; }
    };
    std::size_t runs = 0;
    // act
    const BindingId binding = engine.bind_static(target, {}, &Runner::run, &runs);
    engine.settle();
    // assert
    EXPECT_EQ(runs, 1u);
    EXPECT_TRUE(engine.bound(binding));
}

TEST(BindingEngine, AChangedSourceMarksTheStaticConsumerDirty) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    Property<int> source(1);
    source.set_id(engine.next_property_id());
    source.set_engine(&engine);
    const PropertyId target = engine.next_property_id();
    struct Runner
    {
        static void run(void* context) { (*static_cast<std::size_t*>(context))++; }
    };
    std::size_t runs = 0;
    // the one member source is the dependency array, which the engine reads as a span (§13.3)
    const PropertyId deps[] = {source.id()};
    // the binding is asserted through the runner count, so the identifier is discarded (§13.3)
    (void)engine.bind_static(target, deps, &Runner::run, &runs);
    engine.settle();
    // act
    source.set(2);
    engine.settle();
    // assert
    EXPECT_EQ(runs, 2u);
}

TEST(BindingEngine, UnbindingRemovesTheStaticBinding) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId target = engine.next_property_id();
    struct Runner
    {
        static void run(void*) {}
    };
    const BindingId binding = engine.bind_static(target, {}, &Runner::run, nullptr);
    // act
    const bool removed = engine.unbind(binding);
    // assert
    EXPECT_TRUE(removed);
    EXPECT_FALSE(engine.bound(binding));
    EXPECT_EQ(engine.size(), 0u);
    EXPECT_EQ(engine.dirty_count(), 0u);
}

TEST(BindingEngine, AStaticBindingReportsItsDependencies) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId source = engine.next_property_id();
    const PropertyId target = engine.next_property_id();
    struct Runner
    {
        static void run(void*) {}
    };
    const PropertyId deps[] = {source};
    // act
    const BindingId binding = engine.bind_static(target, deps, &Runner::run, nullptr);
    // assert
    EXPECT_EQ(engine.dependent_count(source), 1u);
    EXPECT_EQ(engine.target(binding), target);
    EXPECT_EQ(engine.dependencies(binding).size(), 1u);
    EXPECT_TRUE(engine.bound(binding));
}

TEST(BindingEngine, RebindingATargetReplacesTheProducer) {
    // arrange
    BindingEngine engine;
    // the base is not asserted here, so the identifier is discarded (§7.2)
    (void)engine.begin_component();
    const PropertyId target = engine.next_property_id();
    struct Runner
    {
        static void run(void* context) { (*static_cast<std::size_t*>(context))++; }
    };
    std::size_t runs = 0;
    // act
    const BindingId first = engine.bind_static(target, {}, &Runner::run, &runs);
    const BindingId second = engine.bind_static(target, {}, &Runner::run, &runs);
    engine.settle();
    // assert the second binding won the production, so the first name carries no work (§7.3)
    EXPECT_FALSE(engine.bound(first));
    EXPECT_TRUE(engine.bound(second));
    EXPECT_EQ(runs, 1u);
    EXPECT_EQ(engine.size(), 1u);
}
