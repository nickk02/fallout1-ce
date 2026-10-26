#include "sim/procedure_table.h"

namespace fallout {

namespace {

    const size_t kFlagsOffset = 4;
    const size_t kDeadlineOffset = 8;

    uint32_t load(const unsigned char* data, size_t position)
    {
        return (static_cast<uint32_t>(data[position]) << 24)
            | (static_cast<uint32_t>(data[position + 1]) << 16)
            | (static_cast<uint32_t>(data[position + 2]) << 8)
            | static_cast<uint32_t>(data[position + 3]);
    }

    void store(unsigned char* data, size_t position, uint32_t value)
    {
        data[position] = static_cast<unsigned char>(value >> 24);
        data[position + 1] = static_cast<unsigned char>(value >> 16);
        data[position + 2] = static_cast<unsigned char>(value >> 8);
        data[position + 3] = static_cast<unsigned char>(value);
    }

} // namespace

void procedure_table_shift_timed(unsigned char* table, size_t entrySize, uint32_t timedFlag, uint32_t delta)
{
    uint32_t count = load(table, 0);

    // The first entry follows the count. Every entry moves the pointer on.
    unsigned char* entry = table + 4;
    for (uint32_t index = 0; index < count; index++, entry += entrySize) {
        if ((load(entry, kFlagsOffset) & timedFlag) != 0) {
            store(entry, kDeadlineOffset, load(entry, kDeadlineOffset) + delta);
        }
    }
}

uint32_t interpreter_units(uint32_t duration, int tick)
{
    if (tick <= 0) {
        return duration;
    }

    return static_cast<uint32_t>(static_cast<uint64_t>(duration) * 1000 / static_cast<uint64_t>(tick));
}

} // namespace fallout
