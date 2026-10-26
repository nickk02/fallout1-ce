#include "test_harness.h"

#include <string.h>

#include <vector>

#include "sim/procedure_table.h"

using namespace fallout;

namespace {

const size_t kEntrySize = 24;
const uint32_t kTimed = 0x01;
const uint32_t kConditional = 0x02;

void put(std::vector<unsigned char>& data, size_t position, uint32_t value)
{
    data[position] = static_cast<unsigned char>(value >> 24);
    data[position + 1] = static_cast<unsigned char>(value >> 16);
    data[position + 2] = static_cast<unsigned char>(value >> 8);
    data[position + 3] = static_cast<unsigned char>(value);
}

uint32_t get(const std::vector<unsigned char>& data, size_t position)
{
    return (static_cast<uint32_t>(data[position]) << 24)
        | (static_cast<uint32_t>(data[position + 1]) << 16)
        | (static_cast<uint32_t>(data[position + 2]) << 8)
        | static_cast<uint32_t>(data[position + 3]);
}

// A table with a guard word after the last entry, to catch writes past it.
struct Table {
    std::vector<unsigned char> data;
    uint32_t count;

    explicit Table(uint32_t entries)
        : data(4 + entries * kEntrySize + 4, 0)
        , count(entries)
    {
        put(data, 0, entries);
        put(data, guard(), 0xDEADBEEFu);
    }

    size_t guard() const { return 4 + count * kEntrySize; }
    size_t entry(uint32_t index) const { return 4 + index * kEntrySize; }

    void set(uint32_t index, uint32_t flags, uint32_t deadline)
    {
        put(data, entry(index) + 4, flags);
        put(data, entry(index) + 8, deadline);

        // Fields that must never change.
        put(data, entry(index), 0x11111111u * (index + 1));
        put(data, entry(index) + 12, 0x22222222u);
        put(data, entry(index) + 16, 0x33333333u);
        put(data, entry(index) + 20, 0x44444444u);
    }

    uint32_t flags(uint32_t index) const { return get(data, entry(index) + 4); }
    uint32_t deadline(uint32_t index) const { return get(data, entry(index) + 8); }
};

} // namespace

TEST(procedure_table_shifts_every_timed_entry)
{
    Table table(4);
    table.set(0, kTimed, 1000);
    table.set(1, kTimed, 2000);
    table.set(2, kTimed, 3000);
    table.set(3, kTimed, 4000);

    procedure_table_shift_timed(table.data.data(), kEntrySize, kTimed, 500);

    // The traversal bug moved the first entry once per entry and left the rest.
    CHECK_EQ(table.deadline(0), 1500u);
    CHECK_EQ(table.deadline(1), 2500u);
    CHECK_EQ(table.deadline(2), 3500u);
    CHECK_EQ(table.deadline(3), 4500u);
}

TEST(procedure_table_leaves_entries_without_the_timed_flag_alone)
{
    Table table(5);
    table.set(0, kConditional, 111);
    table.set(1, kTimed, 222);
    table.set(2, 0, 333);
    table.set(3, kTimed | kConditional, 444);
    table.set(4, kConditional, 555);

    procedure_table_shift_timed(table.data.data(), kEntrySize, kTimed, 10);

    CHECK_EQ(table.deadline(0), 111u);
    CHECK_EQ(table.deadline(1), 232u);
    CHECK_EQ(table.deadline(2), 333u);
    CHECK_EQ(table.deadline(3), 454u);
    CHECK_EQ(table.deadline(4), 555u);
}

TEST(procedure_table_changes_nothing_but_the_deadlines)
{
    Table table(3);
    table.set(0, kTimed, 100);
    table.set(1, 0, 200);
    table.set(2, kTimed, 300);
    std::vector<unsigned char> before = table.data;

    procedure_table_shift_timed(table.data.data(), kEntrySize, kTimed, 7);

    for (size_t position = 0; position < before.size(); position++) {
        bool isDeadline = false;
        for (uint32_t index = 0; index < 3; index++) {
            size_t at = table.entry(index) + 8;
            if (position >= at && position < at + 4) {
                isDeadline = true;
            }
        }

        if (!isDeadline) {
            CHECK_EQ(table.data[position], before[position]);
        }
    }

    CHECK_EQ(table.deadline(0), 107u);
    CHECK_EQ(table.deadline(1), 200u);
    CHECK_EQ(table.deadline(2), 307u);
    CHECK_EQ(get(table.data, table.guard()), 0xDEADBEEFu);
}

TEST(procedure_table_empty_table_is_untouched)
{
    Table table(0);
    std::vector<unsigned char> before = table.data;
    procedure_table_shift_timed(table.data.data(), kEntrySize, kTimed, 99);
    CHECK(table.data == before);
}

TEST(procedure_table_deadline_wraps_like_the_interpreter_clock)
{
    Table table(2);
    table.set(0, kTimed, 0xFFFFFFF0u);
    table.set(1, kTimed, 5);

    procedure_table_shift_timed(table.data.data(), kEntrySize, kTimed, 0x20);

    CHECK_EQ(table.deadline(0), 0x10u);
    CHECK_EQ(table.deadline(1), 0x25u);
}

TEST(procedure_table_zero_delta_changes_nothing)
{
    Table table(2);
    table.set(0, kTimed, 1234);
    table.set(1, kTimed, 5678);
    std::vector<unsigned char> before = table.data;
    procedure_table_shift_timed(table.data.data(), kEntrySize, kTimed, 0);
    CHECK(table.data == before);
}

TEST(interpreter_units_scales_without_wrapping)
{
    CHECK_EQ(interpreter_units(0, 1000), 0u);
    CHECK_EQ(interpreter_units(250, 1000), 250u);

    // 72 minutes. Multiplying by 1000 in 32 bits would have wrapped here.
    CHECK_EQ(interpreter_units(4320000, 1000), 4320000u);

    // The largest duration still fits.
    CHECK_EQ(interpreter_units(0xFFFFFFFFu, 1000), 0xFFFFFFFFu);

    // A coarser tick scales up, a finer one scales down.
    CHECK_EQ(interpreter_units(100, 100), 1000u);
    CHECK_EQ(interpreter_units(100, 2000), 50u);

    // No usable tick: the duration is returned as it is.
    CHECK_EQ(interpreter_units(77, 0u), 77u);
}

namespace {

// The conversion the interpreter used before: 1000 * timer in 32 bits.
uint32_t legacyUnits(uint32_t timer, uint32_t tick)
{
    return 1000u * timer / tick;
}

// 2^32 / 1000 ms, rounded up: the first timer reading where the old product
// wraps. About 71.6 minutes.
const uint32_t kWrapReading = 4294968;

// The test checkWait() makes: a wait is over once the time passes its end.
bool stillWaiting(uint32_t now, uint32_t waitEnd)
{
    return now <= waitEnd;
}

} // namespace

TEST(interpreter_units_matches_the_old_conversion_before_the_wrap)
{
    uint32_t readings[] = { 0, 1, 999, 1000, 60000, 3600000, kWrapReading - 1 };
    for (uint32_t reading : readings) {
        CHECK_EQ(interpreter_units(reading, 1000), legacyUnits(reading, 1000));
        CHECK_EQ(interpreter_units(reading, 1000), reading);
    }
}

TEST(interpreter_units_stays_in_step_with_the_timer_past_71_minutes)
{
    // The old product wrapped here, and the old value jumped back.
    CHECK(legacyUnits(kWrapReading, 1000) != kWrapReading);
    CHECK(legacyUnits(kWrapReading, 1000) < legacyUnits(kWrapReading - 1, 1000));

    for (uint32_t reading = kWrapReading - 5; reading < kWrapReading + 5; reading++) {
        CHECK_EQ(interpreter_units(reading, 1000), reading);
    }

    // Two hours and a day.
    CHECK_EQ(interpreter_units(7200000, 1000), 7200000u);
    CHECK_EQ(interpreter_units(86400000, 1000), 86400000u);
}

TEST(interpreter_wait_started_before_the_wrap_ends_on_time)
{
    // wait(500) called 200 ms before the old product wrapped.
    uint32_t start = interpreter_units(kWrapReading - 200, 1000);
    uint32_t waitEnd = start + 500;

    CHECK(stillWaiting(interpreter_units(kWrapReading - 200, 1000), waitEnd));
    CHECK(stillWaiting(interpreter_units(kWrapReading, 1000), waitEnd));
    CHECK(stillWaiting(interpreter_units(kWrapReading + 299, 1000), waitEnd));
    CHECK(!stillWaiting(interpreter_units(kWrapReading + 301, 1000), waitEnd));

    // With the old units the clock jumped back to about zero at the wrap, so the
    // wait would not have ended until the reading caught up again: more than 70
    // minutes.
    uint32_t oldEnd = legacyUnits(kWrapReading - 200, 1000) + 500;
    CHECK(stillWaiting(legacyUnits(kWrapReading + 301, 1000), oldEnd));
    CHECK(stillWaiting(legacyUnits(kWrapReading + 5000, 1000), oldEnd));
}

TEST(interpreter_wait_started_after_the_wrap_lasts_its_full_time)
{
    uint32_t start = interpreter_units(kWrapReading + 1000, 1000);
    uint32_t waitEnd = start + 2000;

    CHECK(stillWaiting(interpreter_units(kWrapReading + 2999, 1000), waitEnd));
    CHECK(!stillWaiting(interpreter_units(kWrapReading + 3001, 1000), waitEnd));
}

TEST(interpreter_timed_deadline_set_across_the_wrap_fires_once_when_due)
{
    // doEvents() fires a timed procedure once its deadline is below the time.
    uint32_t deadline = interpreter_units(kWrapReading - 100, 1000) + 1000;

    CHECK(!(deadline < interpreter_units(kWrapReading - 100, 1000)));
    CHECK(!(deadline < interpreter_units(kWrapReading + 500, 1000)));
    CHECK(deadline < interpreter_units(kWrapReading + 901, 1000));
}

TEST(interpreter_units_scales_other_ticks_past_the_wrap)
{
    // A timer in 1/100 s: 5,000,000 ticks is 50,000,000 ms.
    CHECK_EQ(interpreter_units(5000000, 100), 50000000u);
}
