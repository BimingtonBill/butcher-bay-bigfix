#include <doctest/doctest.h>
#include "frame_limiter.h"

#include <chrono>
#include <limits>

namespace {
constexpr double period60 = 1000.0 / 60.0;

class FakeClock final : public FrameClock {
public:
    double nowMs() override { return now; }
    double now = 0.0;
};

// Records what the limiter asked for without sleeping, advancing the fake clock.
class CountingWaiter final : public FrameWaiter {
public:
    explicit CountingWaiter(FakeClock& clock) : clock_(clock) {}
    void waitUntil(double deadlineMs) override {
        ++calls;
        if (deadlineMs > clock_.now) {
            totalWaitMs += deadlineMs - clock_.now;
            clock_.now = deadlineMs;
        }
    }
    int calls = 0;
    double totalWaitMs = 0.0;

private:
    FakeClock& clock_;
};
}

TEST_CASE("cap_zero_is_noop") {
    CHECK_FALSE(FrameLimiter(0.0).enabled());
    CHECK(FrameLimiter(0.0).periodMs() == 0.0);

    FakeClock clock;
    CountingWaiter waiter(clock);
    FrameLimiter limiter(0.0, false, clock, waiter);
    CHECK_FALSE(limiter.enabled());
    for (int i = 0; i < 10; ++i) {
        clock.now += 1.0;
        limiter.waitBeforeSwap();
    }
    CHECK(waiter.calls == 0);
    CHECK(clock.now == doctest::Approx(10.0));

    // Negative and non-finite caps are treated as off rather than as a busy spin.
    FakeClock other;
    CountingWaiter otherWaiter(other);
    FrameLimiter negative(-60.0, false, other, otherWaiter);
    FrameLimiter nan(std::numeric_limits<double>::quiet_NaN(), false, other, otherWaiter);
    CHECK_FALSE(negative.enabled());
    CHECK_FALSE(nan.enabled());
}

TEST_CASE("deadline_catches_up_without_burst") {
    // On schedule, deadlines advance by exactly one period from the previous one.
    CHECK(nextDeadline(1000.0, 1005.0, period60) == doctest::Approx(1000.0 + period60));
    CHECK(nextDeadline(1000.0, 1016.0, period60) == doctest::Approx(1000.0 + period60));
    // A frame that overran (loading stall, vsync block) restarts at the present:
    // the missed deadlines are never replayed back to back.
    CHECK(nextDeadline(1000.0, 1100.0, period60) == doctest::Approx(1100.0));
    CHECK(nextDeadline(1000.0, 1016.67, period60) == doctest::Approx(1016.67));
    // Without a period there is nothing to wait for.
    CHECK(nextDeadline(1000.0, 1100.0, 0.0) == doctest::Approx(1100.0));

    FakeClock clock;
    CountingWaiter waiter(clock);
    FrameLimiter limiter(60.0, false, clock, waiter);
    limiter.waitBeforeSwap(); // The first frame is never delayed.
    CHECK(waiter.calls == 0);

    // Every frame takes twice the cap's budget, as on an already-slow machine:
    // the limiter must not sleep to catch up on the deadlines it cannot meet.
    for (int i = 0; i < 100; ++i) {
        clock.now += 2.0 * period60;
        limiter.waitBeforeSwap();
    }
    CHECK(waiter.calls == 0);
    CHECK(waiter.totalWaitMs == 0.0);
}

TEST_CASE("limiter_sleeps_not_spins") {
    FakeClock clock;
    CountingWaiter waiter(clock);
    FrameLimiter limiter(60.0, false, clock, waiter);
    CHECK(limiter.enabled());
    CHECK(limiter.periodMs() == doctest::Approx(period60));
    limiter.waitBeforeSwap();

    // A frame that used 1 ms is waited out by a single timer sleep for the rest
    // of the period, not by polling the clock in a loop.
    clock.now += 1.0;
    limiter.waitBeforeSwap();
    CHECK(waiter.calls == 1);
    CHECK(waiter.totalWaitMs == doctest::Approx(period60 - 1.0));

    // Steady frames keep one sleep per frame, all at the cap's period.
    for (int i = 0; i < 10; ++i) {
        clock.now += 1.0;
        limiter.waitBeforeSwap();
    }
    CHECK(waiter.calls == 11);
    CHECK(waiter.totalWaitMs == doctest::Approx(11.0 * (period60 - 1.0)));
}

TEST_CASE("spin_budget_is_a_fraction_of_the_frame") {
    // Common caps: a short tail, never more than the 0.5 ms ceiling.
    CHECK(spinBudgetFor(period60) == doctest::Approx(period60 * 0.005));
    CHECK(spinBudgetFor(1000.0 / 144.0) == doctest::Approx(1000.0 / 144.0 * 0.005));
    // A high cap must not spend the frame spinning: the tail shrinks with the
    // frame instead of pinning a core at 1000+ FPS.
    CHECK(spinBudgetFor(1.0) == doctest::Approx(0.005));
    CHECK(spinBudgetFor(0.5) == doctest::Approx(0.0025));
    // A very slow cap waits on the timer rather than spinning for half a second.
    CHECK(spinBudgetFor(1000.0) == doctest::Approx(0.5));
    CHECK(spinBudgetFor(0.0) == 0.0);
    for (const double period : {1000.0 / 60.0, 1000.0 / 240.0, 1.0, 0.5})
        CHECK(spinBudgetFor(period) <= period * 0.005 + 1e-12);
}

TEST_CASE("real_timer_paces_frames") {
    // Exercises the QPC clock and the high-resolution waitable timer; the bounds
    // are wide because CI machines sleep coarsely.
    FrameLimiter limiter(20.0); // 50 ms per frame, far longer than any timer jitter.
    CHECK(limiter.periodMs() == doctest::Approx(50.0));
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 4; ++i) limiter.waitBeforeSwap();
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    CHECK(elapsed >= 45.0);
    CHECK(elapsed < 3000.0);
}
