// Live application-memory status shared by screens that need to show it.
//
// This deliberately measures the linker's image sizes and libctru's own heap
// and linear allocators, not the operating-system region report: the latter is
// not reliable in Azahar.
#pragma once

#include <3ds/types.h>

// Bytes of the application's FCRAM slice this game currently occupies: the
// resident executable image (code, rodata, .data and .bss), the main thread's
// stack, the heap bytes malloc is handing out, and the linear-heap bytes the
// GPU allocator is holding. VRAM is separate silicon and is not in this total.
u32 memoryStatusAppUsedBytes(void);

// The ceiling those bytes are measured against: the full 64 MB region a retail
// Original 3DS gives an application, whichever console is actually running.
u32 memoryStatusAppTotalBytes(void);

// Returns the percentage of that budget currently occupied. The reading is
// sampled at most four times a second so it remains live without needlessly
// querying the allocators every render frame.
unsigned int memoryStatusAppPercent(void);

// VRAM is a separate 6 MB bank, not part of the application's FCRAM slice, so
// it is counted on its own. There is no size query for it: call this once at
// startup, before the framebuffers claim any, and the pool reports the true
// total. Everything below returns zero until it has been called.
void memoryStatusMeasureVram(void);

// Bytes of that bank in use - framebuffers, and anything else vramAlloc'd.
u32 memoryStatusVramUsedBytes(void);

// The size of the bank, as measured by memoryStatusMeasureVram().
u32 memoryStatusVramTotalBytes(void);

// Percentage of the VRAM bank in use, sampled on the same schedule as the
// application reading above.
unsigned int memoryStatusVramPercent(void);
