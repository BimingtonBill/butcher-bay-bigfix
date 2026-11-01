#include <doctest/doctest.h>
#include "raw_mouse.h"
#include <array>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

TEST_CASE("raw_accumulator_survives_burst") {
    RawDeltaAccumulator motion;
    std::vector<std::thread> producers;
    for (int t=0; t<4; ++t) producers.emplace_back([&] {
        for (int i=0; i<2000; ++i) motion.add(17, -29);
    });
    for (auto& producer : producers) producer.join();
    CHECK(motion.take() == std::make_pair(136000L, -232000L));
    CHECK(motion.take() == std::make_pair(0L, 0L));
}

TEST_CASE("raw_accumulator_saturates_without_direction_flip") {
    RawDeltaAccumulator motion;
    constexpr long maximum=(std::numeric_limits<long>::max)(), minimum=(std::numeric_limits<long>::min)();
    motion.add(maximum, minimum); motion.add(maximum, minimum);
    CHECK(motion.take() == std::make_pair(maximum, minimum));
    // Accumulate in 64 bits so cancelling large movements do not drift.
    motion.add(maximum, minimum); motion.add(10, -10); motion.add(-10, 10);
    CHECK(motion.take() == std::make_pair(maximum, minimum));
}

TEST_CASE("deltas_discarded_when_unfocused") {
    RawDeltaAccumulator motion;
    motion.add(10, 20); motion.setFocused(false);
    motion.add(500, -600);
    CHECK(motion.take() == std::make_pair(0L, 0L));
    motion.setFocused(true);
    CHECK(motion.take() == std::make_pair(0L, 0L));
    motion.add(-7, 8); motion.setFocused(true); // Rechecking focus must not erase input.
    CHECK(motion.take() == std::make_pair(-7L, 8L));
    motion.add(5, 6); motion.discard();
    CHECK(motion.take() == std::make_pair(0L, 0L));
}

TEST_CASE("overflow_batch_not_lost") {
    std::array<MouseRecord,100> records{};
    for (auto& record:records) record={12,128,123,456};
    const auto result=stripDirectInputMotion(records.data(),100,S_FALSE,true);
    CHECK(result.status == S_OK);
    CHECK(result.count == 100);
    CHECK(records.back().data == 128);
}

TEST_CASE("buttons_and_wheel_untouched") {
    MouseRecord records[]{{0,999,1,2},{12,128,3,4},{4,DWORD(-999),5,6},{8,DWORD(-120),7,8},{19,0,9,10}};
    const MouseRecord expected[]{{12,128,3,4},{8,DWORD(-120),7,8},{19,0,9,10}};
    const auto result=stripDirectInputMotion(records,5,S_OK,true);
    CHECK(result.status == S_OK);
    REQUIRE(result.count == 3);
    CHECK(std::memcmp(records,expected,sizeof(expected)) == 0);
}

TEST_CASE("raw_off_is_original_path") {
    for (const HRESULT status : {S_OK,S_FALSE,HRESULT(0x8007001e)}) {
        MouseRecord records[]{{0,999,1,2},{4,DWORD(-999),3,4},{12,128,5,6},{8,120,7,8}};
        std::array<unsigned char,sizeof(records)> before{};
        std::memcpy(before.data(),records,sizeof(records));
        const auto result=stripDirectInputMotion(records,4,status,false);
        CHECK(result.status == status);
        CHECK(result.count == 4);
        CHECK(std::memcmp(before.data(),records,sizeof(records)) == 0);
    }
}

TEST_CASE("input_errors_leave_batch_untouched") {
    MouseRecord record{0,123,4,5};
    const auto result=stripDirectInputMotion(&record,1,HRESULT(0x8007000c),true);
    CHECK(result.status == HRESULT(0x8007000c));
    CHECK(result.count == 1);
    CHECK(record.offset == 0);
    CHECK(record.data == 123);
}

TEST_CASE("input_trace_is_rate_limited_and_bounded") {
    InputTraceLimiter limiter;
    CHECK(limiter.allow(0));
    CHECK_FALSE(limiter.allow(0));
    CHECK_FALSE(limiter.allow(99));
    CHECK(limiter.allow(100));
    unsigned accepted=2;
    for (std::uint64_t now=200; now<300000; now+=100) accepted+=limiter.allow(now);
    CHECK(accepted == 1500);
    CHECK_FALSE(limiter.allow(1000000));
}
