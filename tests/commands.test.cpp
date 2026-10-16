#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "commands/command.h"
#include "commands/command_registry.h"
#include "commands/shortcut.h"
#include "commands/undo_command.h"
#include "commands/undo_stack.h"

using namespace albor;

namespace
{
    // the consumer a publication test binds, which records the state it was handed rather than acting on it (§18.2)
    struct Observer
    {
        // the state the consumer last saw, which is what the shared object published (§18.2)
        CommandState seen;

        // how many publications arrived, which is what a detachment test asserts on (§18.2)
        std::size_t publications = 0;
    };

    // the publication a bound consumer receives, which is a plain function pointer over its own context (§18.1, §13.3)
    void observe(void* context, const CommandState& state) {
        // the context is the consumer that bound, so the publication reaches exactly it (§18.1)
        auto* observer = static_cast<Observer*>(context);
        // the state is recorded whole, because a consumer never reads a half of a pair (§18.2)
        observer->seen = state;
        observer->publications++;
    }
} // namespace

TEST(CommandState, AFreshCommandIsEnabledVisibleAndInvocable) {
    // arrange
    Command command{"file.save", "Save"};
    // act
    const CommandState& state = command.state();
    // assert a command is permitted and applicable until something says otherwise (§18.2)
    EXPECT_TRUE(state.enabled);
    EXPECT_TRUE(state.visible);
    EXPECT_EQ(state.label, "Save");
    EXPECT_TRUE(invocable(state));
}

TEST(CommandState, AnIdenticalWritePublishesNothing) {
    // arrange
    Command command{"file.save", "Save"};
    Observer observer;
    (void)command.bind_static(&observe, &observer);
    const std::size_t after_bind = observer.publications;
    // act
    command.set_label("Save");
    // assert a write that changes nothing does not reach a consumer, because it has nothing to redraw (§18.2)
    EXPECT_EQ(observer.publications, after_bind);
}

TEST(Command, BindingHandsTheCurrentStateToALateConsumer) {
    // arrange
    Command command{"file.save", "Save"};
    command.set_enabled(false);
    Observer observer;
    // act
    (void)command.bind_static(&observe, &observer);
    // assert a late binder starts from the published state rather than from a default (§18.1)
    EXPECT_FALSE(observer.seen.enabled);
    EXPECT_EQ(observer.publications, std::size_t{1});
}

TEST(Command, ThreeConsumersObserveOneEnablementFromOneObject) {
    // arrange
    Command command{"file.save", "Save"};
    Observer toolbar;
    Observer menu;
    Observer shortcut;
    (void)command.bind_static(&observe, &toolbar);
    (void)command.bind_static(&observe, &menu);
    (void)command.bind_static(&observe, &shortcut);
    // act
    command.set_enabled(false);
    // assert one write reaches all three, which is what the shared object exists for (§18.2)
    EXPECT_FALSE(toolbar.seen.enabled);
    EXPECT_FALSE(menu.seen.enabled);
    EXPECT_FALSE(shortcut.seen.enabled);
}

TEST(Command, EnabledAndVisibleAreTwoSeparatePublishedStates) {
    // arrange
    Command command{"file.save", "Save"};
    Observer observer;
    (void)command.bind_static(&observe, &observer);
    // act
    command.set_visible(false);
    // assert withdrawing applicability leaves permission alone, because the two route differently (§18.2)
    EXPECT_FALSE(observer.seen.visible);
    EXPECT_TRUE(observer.seen.enabled);
}

TEST(Command, UnbindingStopsThePublications) {
    // arrange
    Command command{"file.save", "Save"};
    Observer observer;
    const ObserverHandle handle = command.bind_static(&observe, &observer);
    // act
    const bool removed = command.unbind(handle);
    command.set_enabled(false);
    // assert a detached consumer hears nothing further (§18.1)
    EXPECT_TRUE(removed);
    EXPECT_TRUE(observer.seen.enabled);
    EXPECT_EQ(command.observer_count(), std::size_t{0});
}

TEST(Command, UnbindingAHandleThatBindsNothingIsRefused) {
    // arrange
    Command command{"file.save", "Save"};
    // act
    const bool removed = command.unbind(99);
    // assert a handle that never bound is reported rather than silently accepted (§18.1)
    EXPECT_FALSE(removed);
}

TEST(Command, ANullObserverBindsNothing) {
    // arrange
    Command command{"file.save", "Save"};
    // act
    const ObserverHandle handle = command.bind_static(nullptr, nullptr);
    // assert there is no callback to publish to, so nothing is registered (§18.1)
    EXPECT_EQ(handle, ObserverHandle{0});
    EXPECT_EQ(command.observer_count(), std::size_t{0});
}

TEST(Command, AnIdentifiedCommandCarriesTheAuthorAssignedId) {
    // arrange
    Command named{"file.save", "Save"};
    Command anonymous{"", "Save"};
    // act and assert the author assigned id is the identity automation selects by (AD-11, §18.1)
    EXPECT_TRUE(named.identified());
    EXPECT_EQ(named.explicit_id(), "file.save");
    EXPECT_FALSE(anonymous.identified());
}

TEST(Command, ADisabledCommandRefusesRatherThanRunning) {
    // arrange
    Command command{"file.save", "Save"};
    std::size_t runs = 0;
    command.set_action([&runs](const CommandInvocation&) { runs++; });
    command.set_enabled(false);
    // act
    const InvocationOutcome outcome = command.invoke();
    // assert the refusal is the same one a user would get from a disabled control (§18.3)
    EXPECT_EQ(outcome, InvocationOutcome::Disabled);
    EXPECT_EQ(runs, std::size_t{0});
}

TEST(Command, AHiddenCommandRefusesRatherThanRunning) {
    // arrange
    Command command{"file.save", "Save"};
    std::size_t runs = 0;
    command.set_action([&runs](const CommandInvocation&) { runs++; });
    command.set_visible(false);
    // act
    const InvocationOutcome outcome = command.invoke();
    // assert a command that does not apply reports the reason rather than pretending to have run (§18.3)
    EXPECT_EQ(outcome, InvocationOutcome::Hidden);
    EXPECT_EQ(runs, std::size_t{0});
}

TEST(Command, ACommandDeclaredWithoutAnActionRefuses) {
    // arrange
    Command command{"file.save", "Save"};
    // act
    const InvocationOutcome outcome = command.invoke();
    // assert there is no work to do, which is better than reporting a run that never happened (§18.3)
    EXPECT_EQ(outcome, InvocationOutcome::NoAction);
}

TEST(Command, AnInvocationCarriesTheCommandAndTheRouteItArrivedBy) {
    // arrange
    Command command{"file.save", "Save"};
    CommandId observed = invalid_command_id();
    InvocationSource route = InvocationSource::Pointer;
    command.set_action([&observed, &route](const CommandInvocation& invocation) {
        observed = invocation.command;
        route = invocation.source;
    });
    // act
    (void)command.invoke(CommandInvocation{.source = InvocationSource::Keyboard});
    // assert the handler reads what ran and how it arrived rather than capturing either (§18.3)
    EXPECT_EQ(observed, command.id());
    EXPECT_EQ(route, InvocationSource::Keyboard);
}

TEST(Command, AnActionThatCarriesAnArgumentReceivesIt) {
    // arrange
    Command command{"row.erase", "Delete Rows"};
    std::string observed;
    command.set_action([&observed](const CommandInvocation& invocation) { observed = std::get<std::string>(invocation.argument); });
    // act
    (void)command.invoke(CommandInvocation{.argument = std::string("row-7")});
    // assert the same command bound to many rows carries the identity of the row that was pressed (§18.3)
    EXPECT_EQ(observed, "row-7");
}

TEST(Command, ASelectionScopedActionRefusesAnEmptySelection) {
    // arrange
    CommandRegistry registry;
    registry.set_selection_source([] { return SelectionSpan{.first = 0, .last = 0}; });
    auto erase_owned = registry.declare("row.erase", "Delete Rows");
    Command& erase = *erase_owned;
    erase.set_selection_required(true);
    std::size_t runs = 0;
    erase.set_action([&runs](const CommandInvocation&) { runs++; });
    // act
    const InvocationOutcome outcome = erase.invoke();
    // assert a command that must not run does not, which is what the refusal exists for (§18.3)
    EXPECT_EQ(outcome, InvocationOutcome::NoSelection);
    EXPECT_EQ(runs, std::size_t{0});
}

TEST(Command, ASelectionScopedActionResolvesTheRangeAtInvocationTime) {
    // arrange
    CommandRegistry registry;
    SelectionSpan current{.first = 2, .last = 5};
    registry.set_selection_source([&current] { return current; });
    auto erase_owned = registry.declare("row.erase", "Delete Rows");
    Command& erase = *erase_owned;
    erase.set_selection_required(true);
    std::size_t observed = 0;
    erase.set_action([&observed](const CommandInvocation& invocation) { observed = invocation.selection.count(); });
    // act
    current = SelectionSpan{.first = 0, .last = 9};
    (void)erase.invoke();
    // assert the range is read when the action runs, because one command serves many controls (§18.3)
    EXPECT_EQ(observed, std::size_t{9});
}

TEST(UndoStack, AnEmptyStackOffersNeitherDirection) {
    // arrange
    UndoStack stack;
    // act and assert there is nothing to reverse and nothing to replay (§18.4)
    EXPECT_FALSE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());
    EXPECT_EQ(stack.depth(), std::size_t{0});
}

TEST(UndoStack, APushedStepReversesAndReplays) {
    // arrange
    UndoStack stack;
    int value = 1;
    stack.push(UndoEntry{.label = "Add", .undo = [&value](const CommandInvocation&) { value -= 1; }, .redo = [&value](const CommandInvocation&) { value += 1; }});
    // act
    const bool reversed = stack.undo();
    const bool replayed = stack.redo();
    // assert the step ran in both directions and the replay restored what the reversal undid (§18.4)
    EXPECT_TRUE(reversed);
    EXPECT_TRUE(replayed);
    EXPECT_EQ(value, 1);
}

TEST(UndoStack, ANewStepDiscardsTheReplayedDirection) {
    // arrange
    UndoStack stack;
    stack.push(UndoEntry{.label = "First"});
    (void)stack.undo();
    // act
    stack.push(UndoEntry{.label = "Second"});
    // assert history after a reversal no longer applies once a new step is pushed (§18.4)
    EXPECT_FALSE(stack.can_redo());
    EXPECT_EQ(stack.top_label(), "Second");
}

TEST(UndoStack, AGroupFoldsSeveralStepsIntoOne) {
    // arrange
    UndoStack stack;
    std::vector<int> order;
    int applied = 0;
    // act
    {
        // the scope folds every step pushed inside it, which is what a drag is (§18.4)
        UndoGroup group = stack.open_group("Drag");
        EXPECT_TRUE(group.open());
        for (int step = 0; step < 3; step++)
            stack.push(UndoEntry{.label = "Drag", .undo = [&order, step, &applied](const CommandInvocation&) {
                                     order.push_back(step);
                                     applied++;
                                 }});
    }
    // assert three steps became one entry before anything was reversed (§18.4)
    EXPECT_EQ(stack.depth(), std::size_t{1});
    EXPECT_EQ(applied, 0);
    (void)stack.undo();
    // assert the folded entry runs every step it absorbed, which is what the group promised (§18.4)
    EXPECT_EQ(order.size(), std::size_t{3});
    // assert the stack counts one step for the whole fold, because the fold is what the history now holds (§18.4)
    EXPECT_EQ(stack.applied_count(), std::size_t{1});
}

TEST(UndoStack, AFoldedGroupReversesNewestFirstAndReplaysOldestFirst) {
    // arrange
    UndoStack stack;
    std::vector<int> order;
    // act
    {
        UndoGroup group = stack.open_group("Drag");
        for (int step = 0; step < 3; step++)
            stack.push(UndoEntry{.label = "Drag", .undo = [&order, step](const CommandInvocation&) { order.push_back(step); }, .redo = [&order, step](const CommandInvocation&) { order.push_back(step); }});
    }
    (void)stack.undo();
    (void)stack.redo();
    // assert the reversal ran three newest first and the replay three oldest first (§18.4)
    ASSERT_EQ(order.size(), std::size_t{6});
    EXPECT_EQ(order[0], 2);
    EXPECT_EQ(order[1], 1);
    EXPECT_EQ(order[2], 0);
    EXPECT_EQ(order[3], 0);
    EXPECT_EQ(order[4], 1);
    EXPECT_EQ(order[5], 2);
}

TEST(UndoStack, ARefusedNestedGroupChangesNothing) {
    // arrange
    UndoStack stack;
    // act
    UndoGroup outer = stack.open_group("Outer");
    UndoGroup inner = stack.open_group("Inner");
    // assert a nested group is refused rather than inventing a fold order the caller never wrote (§18.4)
    EXPECT_TRUE(outer.open());
    EXPECT_FALSE(inner.open());
}

TEST(UndoStack, AGroupIsNotReversibleWhileItIsOpen) {
    // arrange
    UndoStack stack;
    UndoGroup group = stack.open_group("Drag");
    stack.push(UndoEntry{.label = "Pending"});
    // act and assert a half formed action is never reversed, because that would leave the caller part way (§18.4)
    EXPECT_FALSE(stack.can_undo());
    EXPECT_FALSE(stack.undo());
}

TEST(UndoStack, TheDepthLimitDropsTheOldestSteps) {
    // arrange
    UndoStack stack;
    stack.set_depth_limit(2);
    // act
    stack.push(UndoEntry{.label = "First"});
    stack.push(UndoEntry{.label = "Second"});
    stack.push(UndoEntry{.label = "Third"});
    // assert the cap bounds a long session and keeps the newest history (§18.4)
    EXPECT_EQ(stack.depth(), std::size_t{2});
    EXPECT_EQ(stack.top_label(), "Third");
}

TEST(UndoStack, AReversalRecordsNoHistoryOfItsOwn) {
    // arrange
    UndoStack stack;
    int applied = 0;
    stack.push(UndoEntry{.label = "Step", .undo = [&applied](const CommandInvocation&) { applied++; }});
    // act
    (void)stack.undo();
    // assert the reversal did not push a step, or undo would never make progress (§18.4)
    EXPECT_EQ(stack.depth(), std::size_t{0});
    EXPECT_EQ(applied, 1);
}

TEST(UndoCommand, ItsEnablementIsTheStacksOwnAnswer) {
    // arrange
    UndoStack stack;
    UndoCommand undo{stack};
    Observer observer;
    (void)undo.command().bind_static(&observe, &observer);
    // act
    stack.push(UndoEntry{.label = "First"});
    // assert the command publishes what the stack holds without any consumer polling it (§18.4)
    EXPECT_TRUE(observer.seen.enabled);
    EXPECT_EQ(observer.seen.label, "First");
}

TEST(UndoCommand, ItRunsTheReversalItPublishes) {
    // arrange
    UndoStack stack;
    UndoCommand undo{stack};
    int reversed = 0;
    stack.push(UndoEntry{.label = "First", .undo = [&reversed](const CommandInvocation&) { reversed++; }});
    // act
    (void)undo.command().invoke();
    // assert the command is the same action a key or a menu would run (§18.4)
    EXPECT_EQ(reversed, 1);
    EXPECT_FALSE(stack.can_undo());
}

TEST(RedoCommand, ItBecomesEnabledOnlyAfterAReversal) {
    // arrange
    UndoStack stack;
    RedoCommand redo{stack};
    Observer observer;
    (void)redo.command().bind_static(&observe, &observer);
    // act
    stack.push(UndoEntry{.label = "First"});
    (void)stack.undo();
    // assert the replay direction follows the reversal, from the one stack (§18.4)
    EXPECT_TRUE(observer.seen.enabled);
    (void)redo.command().invoke();
    EXPECT_TRUE(stack.can_undo());
}

TEST(CommandRegistry, ACommandIsFoundByItsAuthorAssignedId) {
    // arrange
    CommandRegistry registry;
    auto save_owned = registry.declare("file.save", "Save");
    Command& save = *save_owned;
    // act
    Command* found = registry.find("file.save");
    // assert the author assigned id is the selector, and the runtime identity is not (§18.1, AD-11)
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id(), save.id());
    EXPECT_EQ(registry.size(), std::size_t{1});
}

TEST(CommandRegistry, AnAbsentIdNamesNoCommand) {
    // arrange
    CommandRegistry registry;
    auto absent_owned = registry.declare("file.save", "Save");
    // act and assert a selector that matches nothing is refused rather than guessed at (§18.1, AD-11)
    EXPECT_EQ(registry.find("file"), nullptr);
    EXPECT_EQ(registry.find(""), nullptr);
}

TEST(CommandRegistry, AnUnnamedCommandIsNotAddressable) {
    // arrange
    CommandRegistry registry;
    auto unnamed_owned = registry.declare("", "Save");
    // act and assert a command the author never named stays out of the selector index (§18.1, AD-11)
    EXPECT_EQ(registry.find(""), nullptr);
}

TEST(CommandRegistry, EachInvocationContributesExactlyOneEntryWhateverInvokedIt) {
    // arrange
    CommandRegistry registry;
    auto save_owned = registry.declare("file.save", "Save");
    Command& save = *save_owned;
    std::vector<InvocationSource> reversed;
    save.set_action([](const CommandInvocation&) {});
    save.set_undo_entry(UndoEntry{.label = "Save", .undo = [&reversed](const CommandInvocation& invocation) { reversed.push_back(invocation.source); }, .redo = [](const CommandInvocation&) {}});
    // act: a pointer, a key and an automation call all perform the same action
    (void)save.invoke(CommandInvocation{.source = InvocationSource::Pointer});
    (void)save.invoke(CommandInvocation{.source = InvocationSource::Keyboard});
    (void)save.invoke(CommandInvocation{.source = InvocationSource::Automation});
    // assert three user actions are three steps, because the entry is pushed inside the invocation (§18.4)
    EXPECT_EQ(registry.undo_stack().depth(), std::size_t{3});
    // assert each step reverses the route that recorded it, which is what the binding carried (§18.4)
    (void)registry.undo_stack().undo();
    (void)registry.undo_stack().undo();
    ASSERT_EQ(reversed.size(), std::size_t{2});
    EXPECT_EQ(reversed[0], InvocationSource::Automation);
    EXPECT_EQ(reversed[1], InvocationSource::Keyboard);
}

TEST(CommandRegistry, ARepeatedActionInsideOneGroupContributesOneEntry) {
    // arrange
    CommandRegistry registry;
    auto erase_owned = registry.declare("row.erase", "Delete Rows");
    Command& erase = *erase_owned;
    std::size_t undone = 0;
    std::size_t redone = 0;
    erase.set_action([](const CommandInvocation&) {});
    erase.set_undo_entry(UndoEntry{.label = "Delete", .undo = [&undone](const CommandInvocation&) { undone++; }, .redo = [&redone](const CommandInvocation&) { redone++; }});
    // act: the same action performed repeatedly, which is what a drag over rows is
    {
        // the group is what makes the repetition one step rather than one step per row (§18.4)
        UndoGroup group = registry.undo_stack().open_group("Delete Rows");
        for (int row = 0; row < 5; row++)
            (void)erase.invoke();
    }
    // assert five invocations inside one group are one history entry (§18.4)
    EXPECT_EQ(registry.undo_stack().depth(), std::size_t{1});
    (void)registry.undo_stack().undo();
    (void)registry.undo_stack().redo();
    // assert one reversal reached every step the group absorbed, which is what folding promises (§18.4)
    EXPECT_EQ(undone, std::size_t{5});
    EXPECT_EQ(redone, std::size_t{5});
}

TEST(CommandRegistry, TheHistoryFeedsTheUndoAndRedoCommands) {
    // arrange
    CommandRegistry registry;
    UndoCommand& undo = registry.undo_command();
    RedoCommand& redo = registry.redo_command();
    // act
    registry.undo_stack().push(UndoEntry{.label = "First"});
    // assert both commands read the one stack, so neither can answer a question the other denies (§18.4)
    EXPECT_TRUE(undo.command().state().enabled);
    EXPECT_FALSE(redo.command().state().enabled);
    EXPECT_EQ(undo.command().state().label, "First");
}

TEST(ShortcutTable, AnAcceleratorRoutesToItsCommand) {
    // arrange
    CommandRegistry registry;
    auto save_owned = registry.declare("file.save", "Save");
    Command& save = *save_owned;
    const Shortcut shortcut{.key = Key::S, .modifiers = Modifiers{.command = true}};
    // act
    registry.shortcuts().bind(shortcut, save.id());
    // assert the accelerator names the command rather than copying it (§18.5)
    EXPECT_TRUE(registry.shortcuts().bound(shortcut));
    EXPECT_EQ(registry.shortcuts().lookup(shortcut), save.id());
    ASSERT_NE(registry.find_shortcut(shortcut), nullptr);
    EXPECT_EQ(registry.find_shortcut(shortcut)->id(), save.id());
}

TEST(ShortcutTable, AnUnboundAcceleratorNamesNoCommand) {
    // arrange
    ShortcutTable table;
    // act and assert the sentinel is the answer, so a route ends without inventing a target (§18.5)
    EXPECT_FALSE(table.bound(Shortcut{.key = Key::S}));
    EXPECT_EQ(table.lookup(Shortcut{.key = Key::S}), invalid_command_id());
}

TEST(ShortcutTable, ACommandWithNoModifierRoutesOnItsOwn) {
    // arrange
    CommandRegistry registry;
    auto save_owned = registry.declare("file.save", "Save");
    Command& save = *save_owned;
    const Shortcut plain{.key = Key::S};
    const Shortcut modified{.key = Key::S, .modifiers = Modifiers{.command = true}};
    // act
    registry.shortcuts().bind(plain, save.id());
    // assert a plain letter and a modified one are two different accelerators (§18.5)
    EXPECT_TRUE(plain.plain());
    EXPECT_FALSE(modified.plain());
    EXPECT_TRUE(registry.shortcuts().bound(plain));
    EXPECT_FALSE(registry.shortcuts().bound(modified));
}

TEST(ShortcutTable, ACommandPresentsEveryAcceleratorBoundToIt) {
    // arrange
    CommandRegistry registry;
    auto save_owned = registry.declare("file.save", "Save");
    Command& save = *save_owned;
    // act
    registry.shortcuts().bind(Shortcut{.key = Key::S, .modifiers = Modifiers{.command = true}}, save.id());
    registry.shortcuts().bind(Shortcut{.key = Key::F1, .modifiers = Modifiers{.control = true}}, save.id());
    // assert a menu reads the reverse index rather than a second copy of the mapping (§18.5)
    EXPECT_EQ(registry.shortcuts().for_command(save.id()).size(), std::size_t{2});
}

TEST(ShortcutTable, AnAcceleratorIsThePortableSpellingOfAKey) {
    // arrange
    ShortcutTable table;
    // act and assert the text is the portable spelling, which a platform may replace with its own glyph (§18.5)
    EXPECT_EQ(table.display(Shortcut{.key = Key::S, .modifiers = Modifiers{.command = true}}), "Cmd+S");
    EXPECT_EQ(table.display(Shortcut{.key = Key::F5, .modifiers = Modifiers{.shift = true, .control = true}}), "Ctrl+Shift+F5");
    EXPECT_EQ(table.display(Shortcut{.key = Key::Escape}), "Esc");
    EXPECT_EQ(table.display(Shortcut{.key = Key::One}), "1");
}

TEST(ShortcutTable, RebindingAnAcceleratorReleasesItsPreviousOwner) {
    // arrange
    CommandRegistry registry;
    auto save_owned = registry.declare("file.save", "Save");
    Command& save = *save_owned;
    auto other_owned = registry.declare("file.open", "Open");
    Command& other = *other_owned;
    const Shortcut shortcut{.key = Key::S, .modifiers = Modifiers{.command = true}};
    registry.shortcuts().bind(shortcut, save.id());
    // act
    registry.shortcuts().bind(shortcut, other.id());
    // assert a menu shows the accelerator once, against the command that owns it now (§18.5)
    EXPECT_EQ(registry.shortcuts().lookup(shortcut), other.id());
    EXPECT_TRUE(registry.shortcuts().for_command(save.id()).empty());
    EXPECT_EQ(registry.shortcuts().for_command(other.id()).size(), std::size_t{1});
}

TEST(ShortcutTable, AnAcceleratorNamingNoKeyBindsNothing) {
    // arrange
    CommandRegistry registry;
    auto save_owned = registry.declare("file.save", "Save");
    Command& save = *save_owned;
    // act
    registry.shortcuts().bind(Shortcut{.key = Key::None}, save.id());
    // assert no key can ever match it, so nothing is registered (§18.5)
    EXPECT_EQ(registry.shortcuts().size(), std::size_t{0});
}
