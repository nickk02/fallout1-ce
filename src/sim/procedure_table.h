#ifndef FALLOUT_SIM_PROCEDURE_TABLE_H_
#define FALLOUT_SIM_PROCEDURE_TABLE_H_

#include <stddef.h>
#include <stdint.h>

namespace fallout {

// Script programs keep a table of procedures as big-endian 32-bit fields: the
// count at offset 0, then `count` entries of `entrySize` bytes. In an entry the
// flags are at offset 4 and the deadline of a timed procedure at offset 8.

// Adds `delta` to the deadline of every entry whose flags include `timedFlag`.
// Other entries and all other fields stay as they are. The sum wraps at 2^32,
// like the interpreter's own clock.
void procedure_table_shift_timed(unsigned char* table, size_t entrySize, uint32_t timedFlag, uint32_t delta);

// Marks entry `index` as timed with a deadline `delaySeconds` from now. The flags
// get `timedFlag` added and nothing else changes.
//
// While an interpreter suspension is held (`paused`), the time is counted from
// `pauseOrigin`, the interpreter-unit reading taken when the suspension began,
// not from `now`. The resume shifts every timed deadline by the whole length of
// the suspension, so a deadline set from the origin ends exactly `delaySeconds`
// after the resume, wherever in the suspension it was set. Outside a suspension
// it is counted from `now`.
//
// The seconds are multiplied by 1000 in 64 bits. The deadline is the sum modulo
// 2^32, the interpreter's own time domain. A zero or negative delay is accepted
// and wraps the same way.
void procedure_table_schedule_timed(unsigned char* table, size_t entrySize, uint32_t timedFlag, uint32_t index, int32_t delaySeconds, uint32_t now, bool paused, uint32_t pauseOrigin);

// Converts a timer reading or a duration in timer ticks to the interpreter's
// millisecond units: value * 1000 / tick, computed in 64 bits so that a long
// duration, or a reading after about 71.6 minutes of uptime, does not wrap
// before the division. The result is cut to 32 bits like every interpreter
// time. A zero `tick` returns the value unchanged.
uint32_t interpreter_units(uint32_t duration, uint32_t tick);

} // namespace fallout

#endif /* FALLOUT_SIM_PROCEDURE_TABLE_H_ */
