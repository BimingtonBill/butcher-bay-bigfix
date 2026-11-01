#pragma once
#include "config.h"
#include "patch_table.h"
#include <cstdint>
#include <mutex>
#include <utility>

class RawDeltaAccumulator {
    std::mutex mutex_;
    std::int64_t x_ = 0, y_ = 0;
    bool focused_ = true;
public:
    void add(long dx, long dy);
    std::pair<long, long> take();
    void discard();
    void setFocused(bool focused);
};

// DirectInput 7's legacy event format, NOT x64 DIDEVICEOBJECTDATA (24 bytes).
struct MouseRecord { DWORD offset, data, timestamp, sequence; };
static_assert(sizeof(MouseRecord) == 16);
struct MouseBatchResult { HRESULT status; DWORD count; };
MouseBatchResult stripDirectInputMotion(MouseRecord* records, DWORD count, HRESULT status, bool enabled);

class InputTraceLimiter {
    std::uint64_t next_ = 0;
    unsigned lines_ = 0;
public:
    bool allow(std::uint64_t milliseconds);
};

const PatchSite& rawMousePollSite();
std::size_t installRawMouseHooks(HMODULE module, const Settings& settings);
void installRawMouse(const Settings& settings);
