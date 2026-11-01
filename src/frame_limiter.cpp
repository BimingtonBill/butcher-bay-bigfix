#include "frame_limiter.h"

#include "log.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {
constexpr double statsWindowMs = 10000.0;

// QPC milliseconds; GetTickCount64 as a coarse fallback if QPC is unusable.
class QpcClock final : public FrameClock {
public:
    QpcClock() {
        LARGE_INTEGER frequency{};
        if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) {
            LARGE_INTEGER start{};
            QueryPerformanceCounter(&start);
            frequency_ = frequency.QuadPart;
            start_ = start.QuadPart;
            useQpc_ = true;
        } else {
            start_ = static_cast<LONGLONG>(GetTickCount64());
        }
    }

    double nowMs() override {
        if (!useQpc_) return static_cast<double>(GetTickCount64() - static_cast<ULONGLONG>(start_));
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - start_) * 1000.0 / static_cast<double>(frequency_);
    }

private:
    bool useQpc_ = false;
    LONGLONG frequency_ = 0;
    LONGLONG start_ = 0;
};

// Sleeps on a high-resolution waitable timer and spins only the short tail
// spinBudgetFor allows, so timer wake-up jitter cannot overshoot the deadline
// into the next vblank.
class TimerWaiter final : public FrameWaiter {
public:
    explicit TimerWaiter(FrameClock& clock) : clock_(clock) {}
    ~TimerWaiter() override {
        if (timer_) CloseHandle(timer_);
    }

    void waitUntil(double deadlineMs) override {
        const double remaining = deadlineMs - clock_.nowMs();
        if (remaining <= 0.0) return;
        // Sleep the bulk, then spin only the short tail the timer cannot place.
        const double budget = spinBudgetFor(remaining);
        if (remaining > budget) sleepFor(remaining - budget);
        while (deadlineMs > clock_.nowMs()) YieldProcessor();
    }

private:
    void sleepFor(double milliseconds) {
        if (!timerAttempted_) {
            timerAttempted_ = true;
            timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
            // Pre-Windows 10 1803 has no high-resolution flag; a plain timer is
            // still better than Sleep's default granularity.
            if (!timer_) timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
            if (!timer_) logLineNoThrow("Frame cap: no waitable timer; falling back to Sleep");
        }
        if (timer_) {
            LARGE_INTEGER due{};
            due.QuadPart = -static_cast<LONGLONG>(milliseconds * 10000.0);
            if (SetWaitableTimerEx(timer_, &due, 0, nullptr, nullptr, nullptr, 0) &&
                WaitForSingleObjectEx(timer_, static_cast<DWORD>(milliseconds) + 100, FALSE) == WAIT_OBJECT_0)
                return;
        }
        Sleep(static_cast<DWORD>(milliseconds));
    }

    FrameClock& clock_;
    HANDLE timer_ = nullptr;
    bool timerAttempted_ = false;
};
}

double nextDeadline(double previousDeadlineMs, double nowMs, double periodMs) {
    if (!(periodMs > 0.0)) return nowMs;
    const double deadline = previousDeadlineMs + periodMs;
    return deadline > nowMs ? deadline : nowMs;
}

double spinBudgetFor(double frameMs) {
    if (!(frameMs > 0.0)) return 0.0;
    // Parenthesized: windows.h defines a min macro.
    return (std::min)(FrameLimiter::spinBudgetMs, frameMs * FrameLimiter::spinFraction);
}

FrameLimiter::FrameLimiter(double maxFps) {
    configure(maxFps);
    ownDefaults();
}

FrameLimiter::FrameLimiter(double maxFps, bool logStats) : logStats_(logStats) {
    configure(maxFps);
    ownDefaults();
}

void FrameLimiter::ownDefaults() {
    ownedClock_ = std::make_unique<QpcClock>();
    ownedWaiter_ = std::make_unique<TimerWaiter>(*ownedClock_);
    clock_ = ownedClock_.get();
    waiter_ = ownedWaiter_.get();
}

FrameLimiter::FrameLimiter(double maxFps, bool logStats, FrameClock& clock, FrameWaiter& waiter)
    : logStats_(logStats), clock_(&clock), waiter_(&waiter) {
    configure(maxFps);
}

FrameLimiter::~FrameLimiter() = default;

void FrameLimiter::configure(double maxFps) {
    enabled_ = std::isfinite(maxFps) && maxFps > 0.0;
    periodMs_ = enabled_ ? 1000.0 / maxFps : 0.0;
}

void FrameLimiter::waitBeforeSwap() {
    if (!enabled_) return;
    const double now = clock_->nowMs();
    if (!started_) {
        // The first frame has no previous deadline to pace against.
        started_ = true;
        deadlineMs_ = now;
        lastCallMs_ = now;
        return;
    }
    const double deadline = nextDeadline(deadlineMs_, now, periodMs_);
    deadlineMs_ = deadline;
    if (logStats_) recordInterval(now - lastCallMs_);
    lastCallMs_ = now;
    // A deadline in the past means the frame already used its budget; waiting is
    // skipped rather than replayed, so a slow frame never causes a catch-up burst.
    if (deadline > now) waiter_->waitUntil(deadline);
}

void FrameLimiter::recordInterval(double intervalMs) {
    if (!(intervalMs > 0.0)) return;
    intervals_.push_back(intervalMs);
    windowMs_ += intervalMs;
    if (windowMs_ < statsWindowMs) return;

    std::vector<double> sorted = intervals_;
    std::sort(sorted.begin(), sorted.end(), std::greater<double>());
    const std::size_t slowCount = std::max<std::size_t>(1, sorted.size() / 100);
    double total = 0.0, slowTotal = 0.0;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        total += sorted[i];
        if (i < slowCount) slowTotal += sorted[i];
    }
    logf("Frame times: %.2f ms average, %.2f ms 1%% low over %zu frames",
         total / static_cast<double>(sorted.size()), slowTotal / static_cast<double>(slowCount),
         sorted.size());
    intervals_.clear();
    windowMs_ = 0.0;
}
