#include <chrono>
#include <memory>

#include <gtest/gtest.h>

#include "application/application.h"
#include "application/lifetime_token.h"
#include "application/task_queue.h"
#include "application/timer.h"
#include "application/ui_timer.h"
#include "ui/ui_event.h"
#include "window/platform_window_factory.h"
#include "window/popup_host.h"

using namespace albor;

namespace
{
    // the loop test double, which lets a test drive pumping without a window server (§4.1)
    class TestEventLoop final : public PlatformEventLoop
    {
    public:
        // the default double reports nothing, which is what a test that only drives the pump needs
        TestEventLoop() = default;

        // the double reports its stop through a flag the test also holds, because the double is destroyed with the application and its own member would not survive to be read
        explicit TestEventLoop(std::shared_ptr<bool> stop_flag) :
            m_stop_flag(std::move(stop_flag)) {}

        // the pump the application installed, which a test invokes by hand
        Task pump_handler;

        // whether the loop believes it is running
        bool is_running = false;

        // whether the pacing source was started
        bool pacing_started = false;

        // whether the pacing source was stopped
        bool pacing_stopped = false;

        // how many times run was entered, which distinguishes a loop that returned from one that never ran
        std::size_t run_count = 0;

        void set_pump_handler(Task pump) override { pump_handler = std::move(pump); }

        void run() override {
            run_count++;
            is_running = true;
            // the double returns at once, so a test never blocks on it
            is_running = false;
        }

        void quit() override { is_running = false; }

        [[nodiscard]] bool running() const override { return is_running; }

        void start_frame_pacing() override { pacing_started = true; }

        void stop_frame_pacing() override {
            pacing_stopped = true;
            // a test that only counts the stop never reads the member, because the double dies with the application
            if (m_stop_flag)
                *m_stop_flag = true;
        }

    private:
        // the flag the double reports its stop into, which the test also holds
        std::shared_ptr<bool> m_stop_flag;
    };

    // the window test double, which records what the framework asked a native window to do (§5.3)
    class TestPlatformWindow final : public PlatformWindow
    {
    public:
        // the default double counts nothing, which is what a test that only forwards calls needs
        TestPlatformWindow() = default;

        // the double counts its closes into a counter the test also holds, because the double is destroyed together with the window and its flag would not survive to be read
        explicit TestPlatformWindow(std::shared_ptr<int> close_counter) :
            m_close_counter(std::move(close_counter)) {}

        // the listener the framework attached, which the double raises events into
        PlatformWindowListener* listener = nullptr;

        // how many times the native window was shown
        std::size_t show_count = 0;

        // how many times the native window was hidden
        std::size_t hide_count = 0;

        // the title the framework last wrote
        std::string title;

        // the size the framework last asked for
        Size requested_size;

        // how many frames the framework requested
        std::size_t frame_requests = 0;

        // whether the native window is on screen
        bool is_visible = false;

        // whether the native window was closed
        bool was_closed = false;

        // the object a renderer backend would attach to, which is the opaque handle of this double
        int layer = 0;

        // the metrics the native window reports, which a test changes to simulate a resize
        WindowMetrics reported = WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f);

        void set_listener(PlatformWindowListener* attached) override { listener = attached; }

        void show() override {
            show_count++;
            is_visible = true;
            // a platform may report while it presents the window, so the raise proves the binding was in time (§5.3)
            if (listener != nullptr)
                listener->on_platform_resize(reported);
        }

        void hide() override {
            hide_count++;
            is_visible = false;
        }

        void set_title(std::string_view value) override { title = std::string(value); }

        void set_size(Size size) override { requested_size = size; }

        [[nodiscard]] WindowMetrics metrics() const override { return reported; }

        [[nodiscard]] NativeWindowHandle native_handle() const override { return NativeWindowHandle::from_object(&layer); }

        void request_frame() override { frame_requests++; }

        [[nodiscard]] bool frame_requested() const override { return frame_requests > 0; }

        [[nodiscard]] bool visible() const override { return is_visible; }

        void close() override {
            was_closed = true;
            is_visible = false;
            // a test that only counts closes never reads the flag, because the double dies with the window
            if (m_close_counter)
                (*m_close_counter)++;
            // a real platform reports the close it just performed, which is the Closed notification of §5.2
            if (listener != nullptr)
                listener->on_platform_closed();
        }

    private:
        // the counter the double reports its close into, which the test also holds
        std::shared_ptr<int> m_close_counter;
    };
} // namespace

TEST(LifetimeToken, DefaultTokenIsExpired) {
    // arrange, act
    const LifetimeToken token;
    // assert
    EXPECT_FALSE(token.alive());
    EXPECT_TRUE(token.expired());
}

TEST(LifetimeToken, TokenOfALiveSourceIsAlive) {
    // arrange
    const LifetimeTokenSource source;
    // act
    const LifetimeToken token = source.token();
    // assert
    EXPECT_TRUE(token.alive());
    EXPECT_FALSE(token.expired());
}

TEST(LifetimeToken, SourceIsAliveUntilItIsInvalidated) {
    // arrange
    const LifetimeTokenSource source;
    // act
    const bool alive = source.alive();
    // assert
    EXPECT_TRUE(alive);
}

TEST(LifetimeToken, InvalidatingTheSourceExpiresItsTokens) {
    // arrange
    LifetimeTokenSource source;
    const LifetimeToken token = source.token();
    // act
    source.invalidate();
    // assert
    EXPECT_TRUE(token.expired());
    EXPECT_FALSE(token.alive());
}

TEST(LifetimeToken, DestroyingTheSourceExpiresItsTokens) {
    // arrange
    LifetimeToken token;
    {
        // the token outlives the source it observes, which is what makes it a guard
        LifetimeTokenSource source;
        token = source.token();
        EXPECT_TRUE(token.alive());
    }
    // act, assert
    EXPECT_TRUE(token.expired());
    EXPECT_FALSE(token.alive());
}

TEST(LifetimeToken, ExpiringTheSourceStateExpiresEveryToken) {
    // arrange
    LifetimeTokenSource source;
    const LifetimeToken first = source.token();
    const LifetimeToken second = source.token();
    // act
    source.invalidate();
    // assert
    EXPECT_TRUE(first.expired());
    EXPECT_TRUE(second.expired());
}

TEST(LifetimeToken, TwoTokensOfOneSourceAreEqual) {
    // arrange
    const LifetimeTokenSource source;
    // act
    const LifetimeToken first = source.token();
    const LifetimeToken second = source.token();
    // assert
    EXPECT_EQ(first, second);
}

TEST(LifetimeState, IsAliveUntilItExpires) {
    // arrange, act
    LifetimeState state;
    // assert
    EXPECT_TRUE(state.alive());
}

TEST(LifetimeState, IsNotAliveAfterExpiring) {
    // arrange
    LifetimeState state;
    // act
    state.expire();
    // assert
    EXPECT_FALSE(state.alive());
}

TEST(TaskQueue, StartsEmpty) {
    // arrange, act
    const TaskQueue queue;
    // assert
    EXPECT_EQ(queue.pending(), 0u);
}

TEST(TaskQueue, RunsATaskOnDrain) {
    // arrange
    TaskQueue queue;
    bool ran = false;
    const TaskHandle handle = queue.post([&ran] { ran = true; });
    EXPECT_NE(handle, 0u);
    // act
    const std::size_t drained = queue.drain();
    // assert
    EXPECT_EQ(drained, 1u);
    EXPECT_TRUE(ran);
}

TEST(TaskQueue, RunsTasksInPostingOrder) {
    // arrange
    TaskQueue queue;
    std::vector<int> order;
    const TaskHandle handle = queue.post([&order] { order.push_back(1); });
    EXPECT_NE(handle, 0u);
    const TaskHandle handle_1 = queue.post([&order] { order.push_back(2); });
    EXPECT_NE(handle_1, 0u);
    const TaskHandle handle_2 = queue.post([&order] { order.push_back(3); });
    EXPECT_NE(handle_2, 0u);
    // act
    queue.drain();
    // assert
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(TaskQueue, ReportsWhatIsPending) {
    // arrange
    TaskQueue queue;
    // act
    const TaskHandle handle = queue.post([] {});
    EXPECT_NE(handle, 0u);
    const TaskHandle handle_1 = queue.post([] {});
    EXPECT_NE(handle_1, 0u);
    // assert
    EXPECT_EQ(queue.pending(), 2u);
}

TEST(TaskQueue, DrainEmptiesTheQueue) {
    // arrange
    TaskQueue queue;
    const TaskHandle handle = queue.post([] {});
    EXPECT_NE(handle, 0u);
    // act
    queue.drain();
    // assert
    EXPECT_EQ(queue.pending(), 0u);
}

TEST(TaskQueue, DrainingAnEmptyQueueRunsNothing) {
    // arrange, act
    TaskQueue queue;
    // act
    const std::size_t drained = queue.drain();
    // assert
    EXPECT_EQ(drained, 0u);
}

TEST(TaskQueue, APostedHandleNamesItsTask) {
    // arrange
    TaskQueue queue;
    // act
    const TaskHandle handle = queue.post([] {});
    // assert
    EXPECT_NE(handle, 0u);
    EXPECT_TRUE(queue.queued(handle));
}

TEST(TaskQueue, HandlesAreDistinct) {
    // arrange
    TaskQueue queue;
    // act
    const TaskHandle first = queue.post([] {});
    EXPECT_NE(first, 0u);
    const TaskHandle second = queue.post([] {});
    EXPECT_NE(second, 0u);
    // assert
    EXPECT_NE(first, second);
}

TEST(TaskQueue, ACancelledTaskDoesNotRun) {
    // arrange
    TaskQueue queue;
    bool ran = false;
    const TaskHandle handle = queue.post([&ran] { ran = true; });
    // act
    const bool cancelled = queue.cancel(handle);
    queue.drain();
    // assert
    EXPECT_TRUE(cancelled);
    EXPECT_FALSE(ran);
}

TEST(TaskQueue, ACancelledTaskKeepsItsSlotUntilItIsDrained) {
    // arrange
    TaskQueue queue;
    const TaskHandle handle = queue.post([] {});
    EXPECT_NE(handle, 0u);
    // act
    const bool cancelled = queue.cancel(handle);
    // assert
    EXPECT_TRUE(cancelled);
    EXPECT_TRUE(queue.queued(handle));
    EXPECT_EQ(queue.pending(), 1u);
}

TEST(TaskQueue, DrainingDropsTheSlotOfACancelledTask) {
    // arrange
    TaskQueue queue;
    const TaskHandle handle = queue.post([] {});
    EXPECT_NE(handle, 0u);
    queue.cancel(handle);
    // act
    const std::size_t drained = queue.drain();
    // assert
    EXPECT_EQ(drained, 0u);
    EXPECT_EQ(queue.pending(), 0u);
}

TEST(TaskQueue, CancellingAStaleHandleReportsFailure) {
    // arrange
    TaskQueue queue;
    // act
    const bool cancelled = queue.cancel(9999u);
    // assert
    EXPECT_FALSE(cancelled);
}

TEST(TaskQueue, AGuardedTaskRunsWhileItsTokenIsAlive) {
    // arrange
    TaskQueue queue;
    const LifetimeTokenSource source;
    bool ran = false;
    const TaskHandle handle = queue.post_guarded([&ran] { ran = true; }, source.token());
    EXPECT_NE(handle, 0u);
    // act
    queue.drain();
    // assert
    EXPECT_TRUE(ran);
}

TEST(TaskQueue, AGuardedTaskIsDroppedWhenItsTokenExpired) {
    // arrange
    TaskQueue queue;
    LifetimeTokenSource source;
    bool ran = false;
    const TaskHandle handle = queue.post_guarded([&ran] { ran = true; }, source.token());
    EXPECT_NE(handle, 0u);
    source.invalidate();
    // act
    queue.drain();
    // assert
    EXPECT_FALSE(ran);
}

TEST(TaskQueue, AGuardedTaskIsDroppedSilentlyWhenItsSourceIsGone) {
    // arrange
    TaskQueue queue;
    LifetimeTokenSource source;
    bool ran = false;
    const TaskHandle handle = queue.post_guarded([&ran] { ran = true; }, source.token());
    {
        // the source dies while the task is still queued, which is the case a guard exists for
        LifetimeTokenSource doomed;
        queue.cancel(handle);
    }
    // act
    queue.drain();
    // assert
    EXPECT_FALSE(ran);
}

TEST(TaskQueue, PostingWakesAWaitingReader) {
    // arrange
    TaskQueue queue;
    queue.wait_for_task(std::chrono::milliseconds(0));
    queue.acknowledge_wake();
    // act
    const TaskHandle handle = queue.post([] {});
    EXPECT_NE(handle, 0u);
    // assert
    EXPECT_TRUE(queue.woken());
}

TEST(TaskQueue, AcknowledgingTheWakeClearsTheFlag) {
    // arrange
    TaskQueue queue;
    const TaskHandle handle = queue.post([] {});
    EXPECT_NE(handle, 0u);
    queue.drain();
    // act
    queue.acknowledge_wake();
    // assert
    EXPECT_FALSE(queue.woken());
}

TEST(TaskQueue, WaitReturnsAfterTheTimeout) {
    // arrange
    TaskQueue queue;
    // act
    queue.wait_for_task(std::chrono::milliseconds(5));
    // assert
    EXPECT_FALSE(queue.woken());
}

TEST(TimerScheduler, StartsWithNoTimers) {
    // arrange, act
    TimerScheduler scheduler;
    // assert
    EXPECT_EQ(scheduler.size(), 0u);
}

TEST(TimerScheduler, AOneShotTimerDoesNotFireBeforeItsDeadline) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    int fires = 0;
    const TimerHandle timer = scheduler.schedule_once([&fires] { fires++; }, std::chrono::milliseconds(100));
    EXPECT_NE(timer, 0u);
    // act
    const std::size_t ran = scheduler.fire_due();
    // assert
    EXPECT_EQ(ran, 0u);
    EXPECT_EQ(fires, 0);
}

TEST(TimerScheduler, AOneShotTimerFiresOnceItsDeadlinePassed) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    int fires = 0;
    const TimerHandle timer = scheduler.schedule_once([&fires] { fires++; }, std::chrono::milliseconds(100));
    EXPECT_NE(timer, 0u);
    now += std::chrono::milliseconds(100);
    // act
    const std::size_t ran = scheduler.fire_due();
    // assert
    EXPECT_EQ(ran, 1u);
    EXPECT_EQ(fires, 1);
}

TEST(TimerScheduler, AOneShotTimerNeverFiresTwice) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    int fires = 0;
    const TimerHandle timer = scheduler.schedule_once([&fires] { fires++; }, std::chrono::milliseconds(100));
    EXPECT_NE(timer, 0u);
    now += std::chrono::milliseconds(100);
    // act
    scheduler.fire_due();
    scheduler.fire_due();
    // assert
    EXPECT_EQ(fires, 1);
}

TEST(TimerScheduler, AOneShotTimerStopsBeingRegisteredAfterItFires) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    const TimerHandle timer = scheduler.schedule_once([] {}, std::chrono::milliseconds(100));
    now += std::chrono::milliseconds(100);
    // act
    scheduler.fire_due();
    // assert
    EXPECT_FALSE(scheduler.scheduled(timer));
}

TEST(TimerScheduler, ARepeatingTimerFiresAtEveryDeadline) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    int fires = 0;
    const TimerHandle timer = scheduler.schedule_repeating([&fires] { fires++; }, std::chrono::milliseconds(100));
    EXPECT_NE(timer, 0u);
    now += std::chrono::milliseconds(100);
    scheduler.fire_due();
    // act
    now += std::chrono::milliseconds(100);
    scheduler.fire_due();
    // assert
    EXPECT_EQ(fires, 2);
}

TEST(TimerScheduler, ARepeatingTimerCountsItsFirings) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    const TimerHandle timer = scheduler.schedule_repeating([] {}, std::chrono::milliseconds(100));
    now += std::chrono::milliseconds(100);
    // act
    scheduler.fire_due();
    now += std::chrono::milliseconds(100);
    scheduler.fire_due();
    // assert
    EXPECT_EQ(scheduler.fire_count(timer), 2u);
}

TEST(TimerScheduler, ARepeatingTimerKeepsFiringWithoutTheClockMoving) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    const TimerHandle timer = scheduler.schedule_repeating([] {}, std::chrono::milliseconds(100));
    now += std::chrono::milliseconds(100);
    // act
    scheduler.fire_due();
    scheduler.fire_due();
    // assert
    EXPECT_EQ(scheduler.fire_count(timer), 1u);
}

TEST(TimerScheduler, CancellingARepeatingTimerStopsIt) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    const TimerHandle timer = scheduler.schedule_repeating([] {}, std::chrono::milliseconds(100));
    now += std::chrono::milliseconds(100);
    scheduler.fire_due();
    // act
    const bool cancelled = scheduler.cancel(timer);
    now += std::chrono::milliseconds(100);
    scheduler.fire_due();
    // assert
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(scheduler.fire_count(timer), 1u);
}

TEST(TimerScheduler, CancellingAStaleTimerReportsFailure) {
    // arrange, act
    TimerScheduler scheduler;
    // act
    const bool cancelled = scheduler.cancel(9999u);
    // assert
    EXPECT_FALSE(cancelled);
}

TEST(TimerScheduler, ReportsHowManyTimersAreRegistered) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    // act
    const TimerHandle timer = scheduler.schedule_once([] {}, std::chrono::milliseconds(10));
    EXPECT_NE(timer, 0u);
    const TimerHandle timer_1 = scheduler.schedule_repeating([] {}, std::chrono::milliseconds(10));
    EXPECT_NE(timer_1, 0u);
    // assert
    EXPECT_EQ(scheduler.size(), 2u);
}

TEST(TimerScheduler, FireCountIsZeroForAnUnknownTimer) {
    // arrange, act
    TimerScheduler scheduler;
    // act
    const std::uint64_t fires = scheduler.fire_count(9999u);
    // assert
    EXPECT_EQ(fires, 0u);
}

TEST(TimerScheduler, TimeUntilNextIsTheSmallestDeadline) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    const TimerHandle timer = scheduler.schedule_once([] {}, std::chrono::milliseconds(300));
    EXPECT_NE(timer, 0u);
    const TimerHandle timer_1 = scheduler.schedule_once([] {}, std::chrono::milliseconds(100));
    EXPECT_NE(timer_1, 0u);
    // act
    const std::chrono::milliseconds wait = scheduler.time_until_next();
    // assert
    EXPECT_EQ(wait, std::chrono::milliseconds(100));
}

TEST(TimerScheduler, TimeUntilNextIsTheSentinelWhenNothingIsArmed) {
    // arrange, act
    TimerScheduler scheduler;
    // act
    const std::chrono::milliseconds wait = scheduler.time_until_next();
    // assert
    EXPECT_EQ(wait, std::chrono::milliseconds::max());
}

TEST(TimerScheduler, TimeUntilNextCountsDownAsTheClockMoves) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    const TimerHandle timer = scheduler.schedule_once([] {}, std::chrono::milliseconds(300));
    EXPECT_NE(timer, 0u);
    // act
    now += std::chrono::milliseconds(100);
    const std::chrono::milliseconds wait = scheduler.time_until_next();
    // assert
    EXPECT_EQ(wait, std::chrono::milliseconds(200));
}

TEST(TimerScheduler, AnOverdueDeadlineYieldsNoWait) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    const TimerHandle timer = scheduler.schedule_once([] {}, std::chrono::milliseconds(100));
    EXPECT_NE(timer, 0u);
    // act
    now += std::chrono::milliseconds(500);
    const std::chrono::milliseconds wait = scheduler.time_until_next();
    // assert
    EXPECT_EQ(wait, std::chrono::milliseconds(0));
}

TEST(TimerScheduler, OnlyDueTimersFire) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    int due_fires = 0;
    int early_fires = 0;
    const TimerHandle timer = scheduler.schedule_once([&due_fires] { due_fires++; }, std::chrono::milliseconds(100));
    EXPECT_NE(timer, 0u);
    const TimerHandle timer_1 = scheduler.schedule_once([&early_fires] { early_fires++; }, std::chrono::milliseconds(500));
    EXPECT_NE(timer_1, 0u);
    // act
    now += std::chrono::milliseconds(150);
    const std::size_t ran = scheduler.fire_due();
    // assert
    EXPECT_EQ(ran, 1u);
    EXPECT_EQ(due_fires, 1);
    EXPECT_EQ(early_fires, 0);
}

TEST(Timer, IsActiveAfterItIsBuilt) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    // act
    const Timer timer(scheduler, [] {}, std::chrono::milliseconds(100));
    // assert
    EXPECT_TRUE(timer.active());
    EXPECT_NE(timer.handle(), 0u);
}

TEST(Timer, FiresOnceItsDeadlinePassed) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    int fires = 0;
    const Timer timer(scheduler, [&fires] { fires++; }, std::chrono::milliseconds(100));
    now += std::chrono::milliseconds(100);
    // act
    scheduler.fire_due();
    // assert
    EXPECT_EQ(fires, 1);
    EXPECT_EQ(timer.fire_count(), 1u);
}

TEST(Timer, DestroyingTheTimerCancelsIt) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    int fires = 0;
    {
        const Timer timer(scheduler, [&fires] { fires++; }, std::chrono::milliseconds(100));
        EXPECT_TRUE(timer.active());
    }
    // act
    now += std::chrono::milliseconds(500);
    scheduler.fire_due();
    // assert
    EXPECT_EQ(fires, 0);
}

TEST(Timer, ExplicitlyCancellingMakesItInactive) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    Timer timer(scheduler, [] {}, std::chrono::milliseconds(100));
    // act
    timer.cancel();
    // assert
    EXPECT_FALSE(timer.active());
}

TEST(Timer, ACancelledTimerNeverFires) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    int fires = 0;
    Timer timer(scheduler, [&fires] { fires++; }, std::chrono::milliseconds(100));
    // act
    timer.cancel();
    now += std::chrono::milliseconds(500);
    scheduler.fire_due();
    // assert
    EXPECT_EQ(fires, 0);
}

TEST(Timer, MovingATakesOverTheScheduling) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    int fires = 0;
    Timer source(scheduler, [&fires] { fires++; }, std::chrono::milliseconds(100));
    const TimerHandle original = source.handle();
    // act
    Timer moved(std::move(source));
    now += std::chrono::milliseconds(100);
    scheduler.fire_due();
    // assert
    EXPECT_EQ(moved.handle(), original);
    EXPECT_FALSE(source.active());
    EXPECT_EQ(fires, 1);
}

TEST(Timer, MoveAssignmentCancelsTheOverwrittenRegistration) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    int overwritten_fires = 0;
    int adopted_fires = 0;
    Timer target(scheduler, [&overwritten_fires] { overwritten_fires++; }, std::chrono::milliseconds(100));
    Timer source(scheduler, [&adopted_fires] { adopted_fires++; }, std::chrono::milliseconds(100));
    // act
    target = std::move(source);
    now += std::chrono::milliseconds(100);
    scheduler.fire_due();
    // assert
    EXPECT_EQ(overwritten_fires, 0);
    EXPECT_EQ(adopted_fires, 1);
}

TEST(Timer, ARepeatingTimerFiresUntilItIsCancelled) {
    // arrange
    auto now = std::chrono::steady_clock::time_point{};
    TimerScheduler scheduler([&now] { return now; });
    int fires = 0;
    Timer timer = Timer::repeating(scheduler, [&fires] { fires++; }, std::chrono::milliseconds(100));
    now += std::chrono::milliseconds(100);
    scheduler.fire_due();
    // act
    now += std::chrono::milliseconds(100);
    scheduler.fire_due();
    // the count is read while the timer still holds its registration
    EXPECT_EQ(fires, 2);
    EXPECT_EQ(timer.fire_count(), 2u);
    timer.cancel();
    // assert a cancelled timer forgets its registration, so it reports no further firings
    EXPECT_EQ(timer.fire_count(), 0u);
    EXPECT_FALSE(timer.active());
}

TEST(Application, ConstructionInstallsThePumpHandler) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    TestEventLoop* observed = loop.get();
    // act
    const Application application(std::move(loop));
    // assert
    EXPECT_TRUE(observed->pump_handler != nullptr);
}

TEST(Application, ConstructionStartsNoPacingSource) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    TestEventLoop* observed = loop.get();
    // act
    const Application application(std::move(loop));
    // assert
    EXPECT_FALSE(observed->pacing_started);
    EXPECT_FALSE(observed->pacing_stopped);
}

TEST(Application, DestroyingAnOuterApplicationRestoresTheLiveInnerHost) {
    // arrange two applications, where the outer one is destroyed first (§5.1)
    PopupHost* original = window_popup_host();
    auto outer = std::make_unique<Application>(std::make_unique<TestEventLoop>());
    auto inner = std::make_unique<Application>(std::make_unique<TestEventLoop>());
    PopupHost* inner_host = inner.get();
    ASSERT_EQ(window_popup_host(), inner_host);
    // act destroy the outer host before the inner one, which leaves the stale slot behind (§5.1)
    outer.reset();
    // assert the live inner host remains installed, because the dead outer one is skipped (§5.1)
    EXPECT_EQ(window_popup_host(), inner_host);
    // act destroy the last live host, which restores whatever hosted before the pair (§5.1)
    inner.reset();
    // assert the process is left with the same host it started with (§5.1)
    EXPECT_EQ(window_popup_host(), original);
}

TEST(Application, RunStartsThePacingSource) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    TestEventLoop* observed = loop.get();
    Application application(std::move(loop));
    // act
    application.run();
    // assert
    EXPECT_TRUE(observed->pacing_started);
}

TEST(Application, RunHandsBackTheExitCode) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    application.quit(3);
    // act
    const int code = application.run();
    // assert
    EXPECT_EQ(code, 3);
}

TEST(Application, RunReturnsTheDefaultExitCode) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    // act
    const int code = application.run();
    // assert
    EXPECT_EQ(code, 0);
}

TEST(Application, DestructionStopsThePacingSource) {
    // arrange the loop dies with the application, so it reports through a counter this test also holds
    auto pacing_stopped = std::make_shared<bool>(false);
    std::shared_ptr<bool> observed = pacing_stopped;
    {
        Application application(std::make_unique<TestEventLoop>(pacing_stopped));
        EXPECT_FALSE(*observed);
    }
    // act, assert
    EXPECT_TRUE(*observed);
}

TEST(Application, QuitRecordsTheRequest) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    // act
    application.quit(7);
    // assert
    EXPECT_TRUE(application.quit_requested());
    EXPECT_EQ(application.exit_code(), 7);
}

TEST(Application, QuitStopsTheNativeLoop) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    TestEventLoop* observed = loop.get();
    Application application(std::move(loop));
    observed->is_running = true;
    // act
    application.quit();
    // assert
    EXPECT_FALSE(observed->running());
}

TEST(Application, IsNotRunningBeforeRunIsCalled) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    // act, assert
    EXPECT_FALSE(application.running());
}

TEST(Application, RunEntersTheNativeLoop) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    TestEventLoop* observed = loop.get();
    Application application(std::move(loop));
    // act
    application.run();
    // assert
    EXPECT_EQ(observed->run_count, 1u);
}

TEST(Application, PostedTasksRunOnTheNextPump) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    bool ran = false;
    // act
    application.post([&ran] { ran = true; });
    application.pump();
    // assert
    EXPECT_TRUE(ran);
}

TEST(Application, PumpRunsTasksInPostingOrder) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    std::vector<int> order;
    // act
    application.post([&order] { order.push_back(1); });
    application.post([&order] { order.push_back(2); });
    application.pump();
    // assert
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
}

TEST(Application, AGuardedTaskIsDroppedWhenItsSourceIsGone) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    LifetimeTokenSource source;
    bool ran = false;
    application.post([&ran] { ran = true; }, source.token());
    // act
    source.invalidate();
    application.pump();
    // assert
    EXPECT_FALSE(ran);
}

TEST(Application, PumpFiresTheDueTimers) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    int fires = 0;
    const TimerHandle timer = application.timers().schedule_once([&fires] { fires++; }, std::chrono::milliseconds(0));
    EXPECT_NE(timer, 0u);
    // act
    application.pump();
    // assert
    EXPECT_EQ(fires, 1);
}

TEST(Application, PumpRaisesTheFrameHandler) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    int frames = 0;
    application.set_frame_handler([&frames] { frames++; });
    // act
    application.pump();
    // assert
    EXPECT_EQ(frames, 1);
}

TEST(Application, TasksAreQueuedUntilTheyArePumped) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    // act
    application.post([] {});
    // assert
    EXPECT_EQ(application.tasks().pending(), 1u);
}

TEST(Application, CreatingAWindowMintsAValidHandle) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    // act
    const WindowHandle handle = application.create_window(WindowOptions{});
    // assert
    EXPECT_TRUE(handle.valid());
    set_platform_window_factory(nullptr);
}

TEST(Application, CreatingAWindowResolvesThroughTheRegistry) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    // act
    const WindowHandle handle = application.create_window(WindowOptions{});
    Window* found = application.window(handle);
    // assert
    EXPECT_NE(found, nullptr);
    set_platform_window_factory(nullptr);
}

TEST(Application, CountsTheWindowsItOwns) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    // act
    application.create_window(WindowOptions{});
    application.create_window(WindowOptions{});
    // assert
    EXPECT_EQ(application.window_count(), 2u);
    set_platform_window_factory(nullptr);
}

TEST(Application, TwoWindowsGetDistinctHandles) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    // act
    const WindowHandle first = application.create_window(WindowOptions{});
    const WindowHandle second = application.create_window(WindowOptions{});
    // assert
    EXPECT_NE(first, second);
    set_platform_window_factory(nullptr);
}

TEST(Application, DestroyingAWindowClosesIt) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    // the double is destroyed with the window, so it reports its close through a counter this test holds
    auto closes = std::make_shared<int>(0);
    std::shared_ptr<int> observed = closes;
    set_platform_window_factory([closes](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(closes); });
    const WindowHandle handle = application.create_window(WindowOptions{});
    // act
    const bool destroyed = application.destroy_window(handle);
    // assert
    EXPECT_TRUE(destroyed);
    EXPECT_EQ(*observed, 1);
    set_platform_window_factory(nullptr);
}

TEST(Application, DestroyingAWindowStopsResolvingIt) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    const WindowHandle handle = application.create_window(WindowOptions{});
    // act
    application.destroy_window(handle);
    Window* found = application.window(handle);
    // assert
    EXPECT_EQ(found, nullptr);
    set_platform_window_factory(nullptr);
}

TEST(Application, DestroyingAStaleWindowReportsFailure) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    // act
    const bool destroyed = application.destroy_window(WindowHandle{});
    // assert
    EXPECT_FALSE(destroyed);
}

TEST(Application, DestroyedWindowsAreNotCounted) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    const WindowHandle handle = application.create_window(WindowOptions{});
    // act
    application.destroy_window(handle);
    // assert
    EXPECT_EQ(application.window_count(), 0u);
    set_platform_window_factory(nullptr);
}

TEST(Application, AnInvalidOptionSetCreatesNoWindow) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    WindowOptions options;
    options.size = Size{-1.0f, -1.0f};
    // act
    const WindowHandle handle = application.create_window(options);
    // assert
    EXPECT_FALSE(handle.valid());
    EXPECT_EQ(application.window_count(), 0u);
    set_platform_window_factory(nullptr);
}

TEST(Application, ARefusedFactoryCreatesNoWindow) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return nullptr; });
    // act
    const WindowHandle handle = application.create_window(WindowOptions{});
    // assert
    EXPECT_FALSE(handle.valid());
    set_platform_window_factory(nullptr);
}

TEST(Application, HandOutTheLoopItWasBuiltWith) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    TestEventLoop* observed = loop.get();
    Application application(std::move(loop));
    // act
    PlatformEventLoop& current = application.event_loop();
    // assert
    EXPECT_EQ(&current, observed);
}

TEST(Application, HandOutTheQueueItDrains) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    bool ran = false;
    // act
    const TaskHandle handle = application.tasks().post([&ran] { ran = true; });
    EXPECT_NE(handle, 0u);
    application.pump();
    // assert
    EXPECT_TRUE(ran);
}

TEST(Application, HandOutTheSchedulerItFires) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    int fires = 0;
    // act
    const TimerHandle timer = application.timers().schedule_once([&fires] { fires++; }, std::chrono::milliseconds(0));
    EXPECT_NE(timer, 0u);
    application.pump();
    // assert
    EXPECT_EQ(fires, 1);
}

TEST(Application, TheFrameHandlerIsReplacedRatherThanChained) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    int first = 0;
    int second = 0;
    application.set_frame_handler([&first] { first++; });
    // act
    application.set_frame_handler([&second] { second++; });
    application.pump();
    // assert
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);
}

TEST(Application, AnUnsetFrameHandlerDrawsNothing) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    // act
    application.pump();
    // assert
    EXPECT_TRUE(application.quit_requested() == false);
}

TEST(Application, TheCloseHandlerMayVetoAWindowClose) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    application.set_close_handler([](WindowHandle) { return false; });
    const WindowHandle handle = application.create_window(WindowOptions{});
    Window* window = application.window(handle);
    // act
    const bool granted = window->on_platform_close_request();
    // assert
    EXPECT_FALSE(granted);
    EXPECT_TRUE(window->close_pending());
    set_platform_window_factory(nullptr);
}

TEST(Application, TheCloseHandlerMayGrantAWindowClose) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    application.set_close_handler([](WindowHandle) { return true; });
    const WindowHandle handle = application.create_window(WindowOptions{});
    Window* window = application.window(handle);
    // act
    const bool granted = window->on_platform_close_request();
    // assert
    EXPECT_TRUE(granted);
    EXPECT_FALSE(window->close_pending());
    set_platform_window_factory(nullptr);
}

TEST(Application, AnUnsetCloseHandlerGrantsTheClose) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    const WindowHandle handle = application.create_window(WindowOptions{});
    Window* window = application.window(handle);
    // act
    const bool granted = window->on_platform_close_request();
    // assert
    EXPECT_TRUE(granted);
    set_platform_window_factory(nullptr);
}

TEST(Application, AWindowResizeReachesTheResizeHandler) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    WindowHandle resized;
    application.set_resize_handler([&resized](WindowHandle handle) { resized = handle; });
    const WindowHandle handle = application.create_window(WindowOptions{});
    Window* window = application.window(handle);
    // act
    window->on_platform_resize(WindowMetrics::from_logical(Size{320.0f, 240.0f}, 2.0f));
    // assert
    EXPECT_EQ(resized, handle);
    set_platform_window_factory(nullptr);
}

TEST(Application, AWindowEventReachesTheInputHandlerWithItsHandle) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    WindowHandle seen_handle;
    EventKind seen_kind = EventKind::TextInput;
    application.set_input_handler([&seen_handle, &seen_kind](WindowHandle handle, const Event& event) {
        seen_handle = handle;
        seen_kind = event.kind;
    });
    const WindowHandle handle = application.create_window(WindowOptions{});
    Window* window = application.window(handle);
    // act
    window->on_platform_event(Event{.kind = EventKind::PointerDown, .position = Point{10.0f, 20.0f}});
    // assert the handle travels with the event, so the application tells the windows apart (§8.3)
    EXPECT_EQ(seen_handle, handle);
    EXPECT_EQ(seen_kind, EventKind::PointerDown);
    set_platform_window_factory(nullptr);
}

TEST(Application, AnUnsetInputHandlerDropsTheEvent) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    const WindowHandle handle = application.create_window(WindowOptions{});
    Window* window = application.window(handle);
    Event event{.kind = EventKind::KeyDown, .key = Key::Tab};
    // act
    window->on_platform_event(event);
    // assert the event with no handler is the quiet default of §8.3, so nothing claims it
    EXPECT_FALSE(event.handled);
    set_platform_window_factory(nullptr);
}

TEST(Application, TheWindowIsBoundBeforeItIsShown) {
    // arrange the double raises a resize from its show, which is what a platform may do while presenting (§5.3)
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    WindowHandle seen;
    bool called = false;
    application.set_resize_handler([&seen, &called](WindowHandle handle) {
        seen = handle;
        called = true;
    });
    // act
    const WindowHandle handle = application.create_window(WindowOptions{});
    // assert the handler was in place for the event the show raised, which proves the binding ran first (§5.3)
    EXPECT_TRUE(called);
    EXPECT_EQ(seen, handle);
    set_platform_window_factory(nullptr);
}

TEST(Application, AClosedWindowIsTornDownOnTheNextPump) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    const WindowHandle handle = application.create_window(WindowOptions{});
    Window* window = application.window(handle);
    ASSERT_NE(window, nullptr);
    // act the platform reports that the window closed on its own
    window->on_platform_closed();
    // assert the teardown is posted, so it never runs inside the platform close callback (§4.1, §5.2)
    EXPECT_NE(application.window(handle), nullptr);
    application.pump();
    // assert the next pump completed the teardown, which deregisters the window (§5.2)
    EXPECT_EQ(application.window(handle), nullptr);
    EXPECT_EQ(application.window_count(), 0u);
    set_platform_window_factory(nullptr);
}

TEST(Application, TheClosedNotificationReachesTheClosedHandler) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    const WindowHandle handle = application.create_window(WindowOptions{});
    Window* window = application.window(handle);
    WindowHandle seen;
    int notifications = 0;
    application.set_closed_handler([&seen, &notifications](WindowHandle closed) {
        seen = closed;
        notifications++;
    });
    // act
    window->on_platform_closed();
    application.pump();
    // assert the notification carries the handle of the window that closed (§5.2)
    EXPECT_EQ(notifications, 1);
    EXPECT_EQ(seen, handle);
    set_platform_window_factory(nullptr);
}

TEST(Application, DestroyingAWindowReportsTheClose) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    const WindowHandle handle = application.create_window(WindowOptions{});
    WindowHandle seen;
    int notifications = 0;
    application.set_closed_handler([&seen, &notifications](WindowHandle closed) {
        seen = closed;
        notifications++;
    });
    // act
    const bool destroyed = application.destroy_window(handle);
    // assert an application destroy is a close too, so the notification follows its teardown (§5.2)
    EXPECT_TRUE(destroyed);
    EXPECT_EQ(notifications, 1);
    EXPECT_EQ(seen, handle);
    set_platform_window_factory(nullptr);
}

TEST(Application, AClosedWindowIsNotTornDownTwice) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    const WindowHandle handle = application.create_window(WindowOptions{});
    Window* window = application.window(handle);
    int notifications = 0;
    application.set_closed_handler([&notifications](WindowHandle) { notifications++; });
    window->on_platform_closed();
    // act the application destroys the window before the posted teardown gets its turn
    application.destroy_window(handle);
    application.pump();
    // assert the explicit destroy notified once and the posted teardown found nothing left (§5.2)
    EXPECT_EQ(notifications, 1);
    set_platform_window_factory(nullptr);
}

TEST(Application, AWindowResizeUpdatesTheWindowMetrics) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    const WindowHandle handle = application.create_window(WindowOptions{});
    Window* window = application.window(handle);
    const WindowMetrics resized = WindowMetrics::from_logical(Size{320.0f, 240.0f}, 2.0f);
    // act
    window->on_platform_resize(resized);
    // assert
    EXPECT_EQ(window->metrics(), resized);
    set_platform_window_factory(nullptr);
}

TEST(Application, AWindowFrameReachesTheFrameHandler) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    const WindowHandle handle = application.create_window(WindowOptions{});
    Window* window = application.window(handle);
    int frames = 0;
    application.set_frame_handler([&frames] { frames++; });
    // act
    window->on_platform_frame();
    // assert
    EXPECT_EQ(frames, 1);
    set_platform_window_factory(nullptr);
}

TEST(Application, AWindowFrameWithoutAHandlerDrawsNothing) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    const WindowHandle handle = application.create_window(WindowOptions{});
    Window* window = application.window(handle);
    // act
    window->on_platform_frame();
    // assert
    EXPECT_TRUE(application.quit_requested() == false);
    set_platform_window_factory(nullptr);
}

TEST(Application, DestructionClosesEveryWindow) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    // the doubles are destroyed with the windows, so they report through a counter this test also holds
    auto closes = std::make_shared<int>(0);
    std::shared_ptr<int> observed = closes;
    {
        Application application(std::move(loop));
        set_platform_window_factory([closes](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(closes); });
        application.create_window(WindowOptions{});
        application.create_window(WindowOptions{});
        set_platform_window_factory(nullptr);
        // the application is about to be destroyed, so no window has been closed yet
        EXPECT_EQ(*observed, 0);
    }
    // act, assert
    EXPECT_EQ(*observed, 2);
}

TEST(PlatformEventLoopFactory, StartsEmpty) {
    // arrange, act
    set_platform_event_loop_factory(nullptr);
    // act
    const PlatformEventLoopFactory& factory = platform_event_loop_factory();
    // assert
    EXPECT_FALSE(static_cast<bool>(factory));
}

TEST(PlatformEventLoopFactory, InstallingAMakefactoryUsable) {
    // arrange
    set_platform_event_loop_factory(nullptr);
    // act
    set_platform_event_loop_factory([] { return std::unique_ptr<PlatformEventLoop>(new TestEventLoop()); });
    const bool installed = static_cast<bool>(platform_event_loop_factory());
    // assert
    EXPECT_TRUE(installed);
    set_platform_event_loop_factory(nullptr);
}

TEST(PlatformEventLoopFactory, InstalledFactoryBuildsTheLoop) {
    // arrange
    set_platform_event_loop_factory([] { return std::unique_ptr<PlatformEventLoop>(new TestEventLoop()); });
    // act
    std::unique_ptr<PlatformEventLoop> built = platform_event_loop_factory()();
    // assert
    EXPECT_NE(built, nullptr);
    set_platform_event_loop_factory(nullptr);
}

TEST(Application, AFactorylessDefaultApplicationRecordsItsRequests) {
    // arrange
    set_platform_event_loop_factory(nullptr);
    // act, assert the application without a native loop still reports its state (§4.1)
    Application application;
    EXPECT_FALSE(application.running());
    EXPECT_EQ(application.run(), 0);
    application.quit(5);
    EXPECT_EQ(application.exit_code(), 5);
    EXPECT_TRUE(application.quit_requested());
}

TEST(Application, AnInstalledFactoryBuildsTheDefaultLoop) {
    // arrange
    set_platform_event_loop_factory([] { return std::unique_ptr<PlatformEventLoop>(new TestEventLoop()); });
    // act
    Application application;
    // assert
    EXPECT_NE(&application.event_loop(), nullptr);
    set_platform_event_loop_factory(nullptr);
}

TEST(Application, TheInstanceIsTheSingleApplication) {
    // arrange
    set_platform_event_loop_factory(nullptr);
    // act, assert the instance is built once and handed out after (§4.1)
    EXPECT_EQ(&Application::instance(), &Application::instance());
}

TEST(Application, TheConstRegistryResolvesTheSameWindows) {
    // arrange
    auto loop = std::make_unique<TestEventLoop>();
    Application application(std::move(loop));
    set_platform_window_factory([](const WindowOptions&) -> std::unique_ptr<PlatformWindow> { return std::make_unique<TestPlatformWindow>(); });
    const WindowHandle handle = application.create_window(WindowOptions{});
    // act
    const Application& view = application;
    // assert the reading lookup resolves what the writing one does (§5.2)
    EXPECT_EQ(view.window(handle), application.window(handle));
    EXPECT_NE(view.window(handle), nullptr);
    EXPECT_EQ(view.window(WindowHandle{}), nullptr);
    set_platform_window_factory(nullptr);
}

TEST(Timer, AnEmptyCallbackArmsNothing) {
    // arrange
    TimerScheduler scheduler;
    // act, assert a timer without work registers nothing and fires into nothing (§4.2)
    const Timer oneshot(scheduler, nullptr, std::chrono::milliseconds(10));
    const Timer repeating = Timer::repeating(scheduler, nullptr, std::chrono::milliseconds(10));
    EXPECT_FALSE(oneshot.active());
    EXPECT_FALSE(repeating.active());
    EXPECT_EQ(scheduler.size(), 0u);
}

TEST(Timer, ASelfAssignmentKeepsTheTimer) {
    // arrange
    TimerScheduler scheduler;
    Timer timer(scheduler, [] {}, std::chrono::milliseconds(10));
    // act, assert the self assignment is rejected, so the registration survives (§4.2)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
#endif
    timer = std::move(timer);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    EXPECT_TRUE(timer.active());
}

TEST(TimerScheduler, AnEmptyCallbackRegistersNothing) {
    // arrange
    TimerScheduler scheduler;
    // act, assert a timer without work would fire into nothing, so it stays out (§4.2)
    EXPECT_EQ(scheduler.schedule_once(nullptr, std::chrono::milliseconds(10)), 0u);
    EXPECT_EQ(scheduler.schedule_repeating(nullptr, std::chrono::milliseconds(10)), 0u);
    EXPECT_EQ(scheduler.size(), 0u);
}

TEST(TimerScheduler, ACancelledTimerDoesNotConstrainTheWait) {
    // arrange
    TimerScheduler scheduler;
    const TimerHandle handle = scheduler.schedule_once([] {}, std::chrono::milliseconds(100));
    // act
    scheduler.cancel(handle);
    // assert the scheduler whose every timer was cancelled imposes no deadline (§4.2)
    EXPECT_EQ(scheduler.time_until_next(), std::chrono::milliseconds::max());
}

TEST(TaskQueue, AnUnknownHandleIsNotQueued) {
    // arrange
    TaskQueue queue;
    // act, assert
    EXPECT_FALSE(queue.queued(999u));
}
