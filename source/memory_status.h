// Live application-memory status shared by screens that need to show it.
//
// This deliberately measures libctru's heap and linear allocators, not the
// operating-system region report: the latter is not reliable in Azahar.
#pragma once

// Returns the percentage of the fixed retail Old 3DS application budget that
// is currently occupied by heap and linear allocations. The reading is sampled
// at most four times a second so it remains live without needlessly querying
// the allocators every render frame.
unsigned int memoryStatusAppPercent(void);
