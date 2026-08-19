#include "screenshot.h"

#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>

// The bottom screen is left at libctru's default format - only the top
// screen's format is changed, in main.c - which is GSP_BGR8_OES: 3 bytes per
// pixel, stored B,G,R. That is also the byte order a BMP wants, so a pixel
// read off the framebuffer can be written straight into the file with no
// channel-swapping step.
#define SHOT_W   320
#define SHOT_H   240
#define SHOT_BPP 3
#define SHOT_DIR "sdmc:/modelkit"

// Finds the highest existing shot_NNN.bmp so a new capture continues the
// sequence instead of restarting at 1 and overwriting an earlier session's
// shots. Missing directory (first run) just means "start at 1".
static int findNextIndex(void)
{
	int best = 0;
	DIR* d = opendir(SHOT_DIR);
	if (d)
	{
		struct dirent* e;
		while ((e = readdir(d)) != NULL)
		{
			int n = 0;
			if (sscanf(e->d_name, "shot_%d.bmp", &n) == 1 && n > best)
				best = n;
		}
		closedir(d);
	}
	return best + 1;
}

static void putU32(FILE* f, u32 v)
{
	u8 b[4] = { (u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24) };
	fwrite(b, 1, 4, f);
}

static void putU16(FILE* f, u16 v)
{
	u8 b[2] = { (u8)v, (u8)(v >> 8) };
	fwrite(b, 1, 2, f);
}

bool screenshotCapture(void)
{
	// Ignoring the result: the only failure that matters is the fopen below,
	// and if the directory already exists this returns an error that is not
	// one.
	mkdir(SHOT_DIR, 0777);

	u16 fbW, fbH;
	u8* fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &fbW, &fbH);
	if (!fb) return false;

	// By the time main.c calls us, C3D_FrameEnd has already queued this
	// frame into the bottom screen's framebuffer - but that write came from
	// the GPU, not the CPU, so it never went through the CPU's data cache.
	// Reading fb with the pixel loop below, without invalidating first, can
	// hand back whatever the CPU cache happened to be holding for that
	// address range: leftover bytes from an earlier frame, or a half
	// written line if the transfer was still landing when a cache line got
	// pulled. Either way the capture looks fine sometimes and wrong other
	// times with no code change in between, because it depends on cache
	// state that a single test run has no reason to hit. Invalidating the
	// exact byte range the loop below reads (every byte of fb, since the
	// (x*SHOT_H + r) mapping touches all of them) forces the next read of
	// each line to come from RAM instead of stale cache.
	if (R_FAILED(GSPGPU_InvalidateDataCache(fb, SHOT_W * SHOT_H * SHOT_BPP)))
		return false;

	char path[64];
	snprintf(path, sizeof(path), SHOT_DIR "/shot_%03d.bmp", findNextIndex());

	FILE* f = fopen(path, "wb");
	if (!f) return false;

	const u32 rowBytes   = SHOT_W * SHOT_BPP; // 960 - already a multiple of 4, so BMP needs no row padding
	const u32 pixelBytes = rowBytes * SHOT_H;

	// BITMAPFILEHEADER
	fwrite("BM", 1, 2, f);
	putU32(f, 54 + pixelBytes);
	putU32(f, 0);
	putU32(f, 54);

	// BITMAPINFOHEADER
	putU32(f, 40);
	putU32(f, SHOT_W);
	putU32(f, SHOT_H);
	putU16(f, 1);
	putU16(f, 24);
	putU32(f, 0); // BI_RGB - no compression
	putU32(f, pixelBytes);
	putU32(f, 2835); // ~72 DPI
	putU32(f, 2835);
	putU32(f, 0);
	putU32(f, 0);

	// Pixel data. BMP rows are bottom-up, and the 3DS stores the framebuffer
	// rotated: physical column x holds SHOT_H consecutive bytes running from
	// the screen's bottom edge up to its top edge. So BMP output row r (r=0
	// is the image's bottom row) is exactly physical offset (x*SHOT_H + r)
	// for every column x - confirmed against a captured emulator frame
	// rather than assumed, since a sign error here silently produces a
	// rotated or mirrored image instead of a build failure.
	for (u32 r = 0; r < SHOT_H; r++)
	{
		for (u32 x = 0; x < SHOT_W; x++)
		{
			u32 srcOff = (x * SHOT_H + r) * SHOT_BPP;
			fwrite(fb + srcOff, 1, SHOT_BPP, f);
		}
	}

	fclose(f);
	return true;
}
