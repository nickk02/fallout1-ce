#include "test_harness.h"

#include <string.h>

#include <string>

#include "sim/sim_log.h"

using namespace fallout;

TEST(sim_log_format_pause_begin_and_end)
{
    char out[192];

    int length = sim_log_format_pause(out, sizeof(out), true, 3, 1200, 0);
    CHECK_EQ(std::string(out), std::string("simlog pause begin epoch=3 ticks=1200"));
    CHECK_EQ(length, static_cast<int>(strlen(out)));

    length = sim_log_format_pause(out, sizeof(out), false, 3, 1260, 250);
    CHECK_EQ(std::string(out), std::string("simlog pause end epoch=3 ticks=1260 elapsed_ms=250"));
    CHECK_EQ(length, static_cast<int>(strlen(out)));

    // The elapsed time is ignored for a begin line.
    sim_log_format_pause(out, sizeof(out), true, 1, 2, 99);
    CHECK_EQ(std::string(out), std::string("simlog pause begin epoch=1 ticks=2"));
}

TEST(sim_log_format_pause_prints_large_values)
{
    char out[192];
    sim_log_format_pause(out, sizeof(out), false, 18446744073709551615ull, 12345678901234ull, 4294967295u);
    CHECK_EQ(std::string(out), std::string("simlog pause end epoch=18446744073709551615 ticks=12345678901234 elapsed_ms=4294967295"));
}

TEST(sim_log_format_lifecycle_prints_the_paused_flag)
{
    char out[192];

    int length = sim_log_format_lifecycle(out, sizeof(out), "init", 1, 0, false);
    CHECK_EQ(std::string(out), std::string("simlog init epoch=1 ticks=0 paused=0"));
    CHECK_EQ(length, static_cast<int>(strlen(out)));

    sim_log_format_lifecycle(out, sizeof(out), "reset", 2, 600, true);
    CHECK_EQ(std::string(out), std::string("simlog reset epoch=2 ticks=600 paused=1"));

    sim_log_format_lifecycle(out, sizeof(out), "invalidate", 3, 601, false);
    CHECK_EQ(std::string(out), std::string("simlog invalidate epoch=3 ticks=601 paused=0"));

    sim_log_format_lifecycle(out, sizeof(out), "exit", 4, 9, true);
    CHECK_EQ(std::string(out), std::string("simlog exit epoch=4 ticks=9 paused=1"));
}

TEST(sim_log_format_summary_with_and_without_detail)
{
    char out[192];

    int length = sim_log_format_summary(out, sizeof(out), 2, 5, 600, 590, nullptr);
    CHECK_EQ(std::string(out), std::string("simlog summary mode=2 epoch=5 ticks=600 legacy_calendar_ticks=590"));
    CHECK_EQ(length, static_cast<int>(strlen(out)));

    sim_log_format_summary(out, sizeof(out), 1, 5, 1200, 0, " game_time=42 x=1");
    CHECK_EQ(std::string(out), std::string("simlog summary mode=1 epoch=5 ticks=1200 legacy_calendar_ticks=0 game_time=42 x=1"));

    sim_log_format_summary(out, sizeof(out), 2, 5, 1, 2, "");
    CHECK_EQ(std::string(out), std::string("simlog summary mode=2 epoch=5 ticks=1 legacy_calendar_ticks=2"));
}

TEST(sim_log_formatters_truncate_safely)
{
    // Guard bytes after the buffer show an overflow.
    char buffer[16];

    memset(buffer, 'x', sizeof(buffer));
    int length = sim_log_format_pause(buffer, 1, true, 3, 1200, 0);
    CHECK_EQ(buffer[0], '\0');
    CHECK_EQ(buffer[1], 'x');
    CHECK_EQ(length, static_cast<int>(strlen("simlog pause begin epoch=3 ticks=1200")));

    memset(buffer, 'x', sizeof(buffer));
    length = sim_log_format_lifecycle(buffer, 8, "init", 1, 0, false);
    CHECK_EQ(std::string(buffer), std::string("simlog "));
    CHECK_EQ(buffer[8], 'x');
    CHECK_EQ(length, static_cast<int>(strlen("simlog init epoch=1 ticks=0 paused=0")));

    memset(buffer, 'x', sizeof(buffer));
    length = sim_log_format_summary(buffer, 8, 2, 5, 600, 590, " detail");
    CHECK_EQ(std::string(buffer), std::string("simlog "));
    CHECK_EQ(buffer[8], 'x');
    CHECK_EQ(length, static_cast<int>(strlen("simlog summary mode=2 epoch=5 ticks=600 legacy_calendar_ticks=590 detail")));

    memset(buffer, 'x', sizeof(buffer));
    sim_log_format_pause(buffer, 8, false, 3, 1200, 5);
    CHECK_EQ(std::string(buffer), std::string("simlog "));
    CHECK_EQ(buffer[8], 'x');
}

TEST(sim_log_formatters_write_nothing_for_size_0)
{
    char buffer[4];
    memset(buffer, 'x', sizeof(buffer));

    int length = sim_log_format_pause(buffer, 0, true, 3, 1200, 0);
    CHECK_EQ(length, static_cast<int>(strlen("simlog pause begin epoch=3 ticks=1200")));
    CHECK_EQ(buffer[0], 'x');

    length = sim_log_format_lifecycle(buffer, 0, "exit", 1, 0, true);
    CHECK(length > 0);
    CHECK_EQ(buffer[0], 'x');

    length = sim_log_format_summary(buffer, 0, 1, 1, 0, 0, nullptr);
    CHECK(length > 0);
    CHECK_EQ(buffer[0], 'x');
}

TEST(status_log_gate_first_call_logs)
{
    StatusLogGate gate;
    uint32_t suppressed = 99;
    CHECK(gate.shouldLog(1, 1000, &suppressed));
    CHECK_EQ(suppressed, 0u);

    // A null count pointer is allowed.
    StatusLogGate other;
    CHECK(other.shouldLog(1, 0, nullptr));
}

TEST(status_log_gate_holds_back_the_same_status_and_reports_the_count)
{
    StatusLogGate gate(600);
    uint32_t suppressed = 0;
    CHECK(gate.shouldLog(4, 1000, &suppressed));

    suppressed = 77;
    CHECK(!gate.shouldLog(4, 1001, &suppressed));
    CHECK(!gate.shouldLog(4, 1200, &suppressed));
    CHECK(!gate.shouldLog(4, 1599, &suppressed));

    // Held back calls do not write the count.
    CHECK_EQ(suppressed, 77u);

    CHECK(gate.shouldLog(4, 1600, &suppressed));
    CHECK_EQ(suppressed, 3u);

    // The count starts over.
    CHECK(!gate.shouldLog(4, 1601, &suppressed));
    CHECK(gate.shouldLog(4, 2200, &suppressed));
    CHECK_EQ(suppressed, 1u);
}

TEST(status_log_gate_logs_a_changed_status_at_once)
{
    StatusLogGate gate(600);
    uint32_t suppressed = 0;
    CHECK(gate.shouldLog(4, 1000, &suppressed));
    CHECK(!gate.shouldLog(4, 1010, &suppressed));
    CHECK(!gate.shouldLog(4, 1020, &suppressed));

    CHECK(gate.shouldLog(8, 1030, &suppressed));
    CHECK_EQ(suppressed, 2u);

    // The new status is now the one that is held back, from the new time.
    CHECK(!gate.shouldLog(8, 1031, &suppressed));
    CHECK(!gate.shouldLog(8, 1629, &suppressed));
    CHECK(gate.shouldLog(8, 1630, &suppressed));
    CHECK_EQ(suppressed, 2u);
}

TEST(status_log_gate_interval_boundary_is_exact)
{
    StatusLogGate gate(600);
    uint32_t suppressed = 0;
    CHECK(gate.shouldLog(2, 100, &suppressed));
    CHECK(!gate.shouldLog(2, 699, &suppressed));
    CHECK(gate.shouldLog(2, 700, &suppressed));

    // The interval counts from the last logged call, not the last suppressed one.
    CHECK(!gate.shouldLog(2, 1299, &suppressed));
    CHECK(gate.shouldLog(2, 1300, &suppressed));
}

TEST(status_log_gate_default_interval_is_600)
{
    StatusLogGate gate;
    uint32_t suppressed = 0;
    CHECK(gate.shouldLog(1, 0, &suppressed));
    CHECK(!gate.shouldLog(1, 599, &suppressed));
    CHECK(gate.shouldLog(1, 600, &suppressed));
}

TEST(status_log_gate_logs_when_the_counter_goes_backward)
{
    StatusLogGate gate(600);
    uint32_t suppressed = 0;
    CHECK(gate.shouldLog(1, 5000, &suppressed));
    CHECK(!gate.shouldLog(1, 5001, &suppressed));

    // The tick count restarted.
    CHECK(gate.shouldLog(1, 10, &suppressed));
    CHECK_EQ(suppressed, 1u);
    CHECK(!gate.shouldLog(1, 11, &suppressed));
}

TEST(status_log_gate_clear_makes_the_next_call_log)
{
    StatusLogGate gate(600);
    uint32_t suppressed = 5;
    CHECK(gate.shouldLog(3, 100, &suppressed));
    CHECK(!gate.shouldLog(3, 101, &suppressed));
    CHECK(!gate.shouldLog(3, 102, &suppressed));

    gate.clear();
    CHECK(gate.shouldLog(3, 103, &suppressed));

    // Nothing was carried over from before the clear.
    CHECK_EQ(suppressed, 0u);
    CHECK(!gate.shouldLog(3, 104, &suppressed));
}

TEST(status_log_gate_with_interval_0_always_logs)
{
    StatusLogGate gate(0);
    uint32_t suppressed = 0;
    CHECK(gate.shouldLog(1, 5, &suppressed));
    CHECK(gate.shouldLog(1, 5, &suppressed));
    CHECK_EQ(suppressed, 0u);
}
