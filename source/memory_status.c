#include <3ds.h>
#include <malloc.h>

#include "memory_status.h"

// A retail Old 3DS gives an application a 64 MB FCRAM region. The measured
// startup reservation is 224 KB, leaving this binary's usable app budget.
#define OLD3DS_APP_BUDGET (65312u * 1024u)
#define MEMORY_SAMPLE_MS 250u

unsigned int memoryStatusAppPercent(void)
{
	static u64 lastSampleAt = 0;
	static unsigned int cachedPercent = 0;
	const u64 now = osGetTime();

	if (lastSampleAt == 0 || now - lastSampleAt >= MEMORY_SAMPLE_MS)
	{
		const struct mallinfo mi = mallinfo();
		const u32 heapUsed = (u32)mi.uordblks;
		const u32 linearTotal = envGetLinearHeapSize();
		const u32 linearFree = linearSpaceFree();
		const u32 linearUsed = linearTotal > linearFree ? linearTotal - linearFree : 0;
		const u32 appUsed = heapUsed + linearUsed;

		cachedPercent = (unsigned int)(((u64)appUsed * 100u
			+ OLD3DS_APP_BUDGET / 2u) / OLD3DS_APP_BUDGET);
		lastSampleAt = now;
	}

	return cachedPercent;
}
