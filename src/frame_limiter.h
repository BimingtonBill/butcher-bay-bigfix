#pragma once

#include <memory>
#include <vector>

// Optional frame-rate cap for the SwapBuffers detour.
//
// The schedule is absolute: each frame's deadline is the previous deadline plus
// one period. A frame that overran its budget (vsync blocking, a loading stall)
// restarts the schedule at the present, so missed deadlines are never replayed
// as a burst of back-to-back presents.
double nextDeadline(double previousDeadlineMs, double nowMs, double periodMs);

// How much of a wait may spin: a fixed ceiling, or a fraction of the frame when
// the cap is high enough for that to be smaller.
double spinBudgetFor(double frameMs);

// Injectable so the scheduler is unit-tested without sleeping.
class FrameClock {
public:
    virtual ~FrameClock() = default;
    virtual double nowMs() = 0;
};

class FrameWaiter {
public:
    virtual ~FrameWaiter() = default;
    // Block until clock.nowMs() reaches deadlineMs.
    virtual void waitUntil(double deadlineMs) = 0;
};

class FrameLimiter {
public:
    // maxFps <= 0 (or not finite) disables the limiter: waitBeforeSwap() returns
    // immediately and no timer is ever created.
    explicit FrameLimiter(double maxFps);
    // logStats: while the cap is on, log average and 1% low frame times every
    // 10 s (DumpFrameTimes).
    FrameLimiter(double maxFps, bool logStats);
    // The wait budget for maxFps comes from the caller's clock and waiter.
    FrameLimiter(double maxFps, bool logStats, FrameClock& clock, FrameWaiter& waiter);
    ~FrameLimiter();
    FrameLimiter(const FrameLimiter&) = delete;
    FrameLimiter& operator=(const FrameLimiter&) = delete;

    // Call once per frame, after the frame is rendered and before presenting.
    // The swap hook runs it inside its own exception boundary.
    void waitBeforeSwap();
    bool enabled() const { return enabled_; }
    double periodMs() const { return periodMs_; }

    // Only the last fraction of a wait spins; the rest is a timer sleep. The
    // spin is at most spinBudgetMs and never more than spinFraction of the
    // frame, so a cap above 1000 FPS cannot turn the wait into a busy loop.
    static constexpr double spinBudgetMs = 0.5;
    static constexpr double spinFraction = 0.005;

private:
    void configure(double maxFps);
    void ownDefaults();
    void recordInterval(double intervalMs);

    bool enabled_ = false;
    bool logStats_ = false;
    double periodMs_ = 0.0;
    FrameClock* clock_ = nullptr;
    FrameWaiter* waiter_ = nullptr;
    // Defaults owned by the FrameLimiter; null when the caller supplied its own.
    std::unique_ptr<FrameClock> ownedClock_;
    std::unique_ptr<FrameWaiter> ownedWaiter_;
    bool started_ = false;
    double deadlineMs_ = 0.0;
    double lastCallMs_ = 0.0;
    // Per-log-interval frame times, only collected when logStats is set.
    std::vector<double> intervals_;
    double windowMs_ = 0.0;
};
