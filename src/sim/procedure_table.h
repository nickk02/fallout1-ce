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

// Converts a duration in timer ticks to the interpreter's millisecond units:
// duration * 1000 / tick, computed in 64 bits so a long duration does not wrap
// before the division. A zero `tick` returns the duration unchanged.
uint32_t interpreter_units(uint32_t duration, int tick);

} // namespace fallout

#endif /* FALLOUT_SIM_PROCEDURE_TABLE_H_ */
