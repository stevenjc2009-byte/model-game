#include <3ds.h>
#include <malloc.h>

#include "memory_status.h"

// A retail Old 3DS gives an application the whole 64 MB FCRAM region, and that
// is the budget used here with nothing subtracted from it. The executable image
// and the stack used to be guessed at and taken off the ceiling instead; they
// are now counted on the used side, where they belong, so the guess is gone.
#define OLD3DS_APP_BUDGET (65536u * 1024u)
#define MEMORY_SAMPLE_MS 250u

// Laid down by the linker. Everything from __start__ to __end__ - code, rodata,
// .data and .bss - is resident in the application's FCRAM before main() runs.
// It is most of what this game occupies: the mesh pool alone is 324 KB of .bss,
// and .bss is in no allocator's books, so leaving this out understated the true
// figure by two thirds.
extern char __start__[];
extern char __end__[];

// The main thread's stack, reserved by the loader alongside the image.
extern u32 __stacksize__;

// Everything resident before a single byte is allocated.
static u32 staticFootprint(void)
{
	return (u32)(__end__ - __start__) + __stacksize__;
}

u32 memoryStatusAppUsedBytes(void)
{
	const struct mallinfo mi = mallinfo();
	const u32 linearTotal = envGetLinearHeapSize();
	const u32 linearFree  = linearSpaceFree();
	const u32 linearUsed  = linearTotal > linearFree ? linearTotal - linearFree : 0;

	return staticFootprint() + (u32)mi.uordblks + linearUsed;
}

u32 memoryStatusAppTotalBytes(void)
{
	return OLD3DS_APP_BUDGET;
}

// VRAM has no size query, so it is measured once before the framebuffers claim
// any of it. libctru hands the whole bank to its pool on the first allocation,
// so one throwaway alloc-and-free leaves the pool reporting the true total.
static u32 vramTotalBytes = 0;

void memoryStatusMeasureVram(void)
{
	void* probe = vramAlloc(16);
	if (probe)
	{
		vramFree(probe);
		vramTotalBytes = vramSpaceFree();
	}
}

u32 memoryStatusVramTotalBytes(void)
{
	return vramTotalBytes;
}

u32 memoryStatusVramUsedBytes(void)
{
	const u32 free = vramSpaceFree();
	return vramTotalBytes > free ? vramTotalBytes - free : 0;
}

static unsigned int percentOf(u32 used, u32 total)
{
	return total ? (unsigned int)(((u64)used * 100u + total / 2u) / total) : 0;
}

// Both readings are sampled together, at most four times a second, so the two
// rows of the Options plaque never disagree about when they were taken.
static void sample(unsigned int* app, unsigned int* vram)
{
	static u64 lastSampleAt = 0;
	static unsigned int cachedApp = 0;
	static unsigned int cachedVram = 0;
	const u64 now = osGetTime();

	if (lastSampleAt == 0 || now - lastSampleAt >= MEMORY_SAMPLE_MS)
	{
		cachedApp  = percentOf(memoryStatusAppUsedBytes(), memoryStatusAppTotalBytes());
		cachedVram = percentOf(memoryStatusVramUsedBytes(), vramTotalBytes);
		lastSampleAt = now;
	}

	if (app)  *app  = cachedApp;
	if (vram) *vram = cachedVram;
}

unsigned int memoryStatusAppPercent(void)
{
	unsigned int app;
	sample(&app, NULL);
	return app;
}

unsigned int memoryStatusVramPercent(void)
{
	unsigned int vram;
	sample(NULL, &vram);
	return vram;
}
