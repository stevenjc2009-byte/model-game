#include "save.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// Bumped whenever the meaning of the payload changes in a way the size and
// layout id would not catch on their own. A file from an older version is
// refused, not migrated: there is one screen of progress in it and re-snipping a
// kit is cheaper than a migration path nobody can test.
#define SAVE_MAGIC   0x564B4D31u   // "MKV1" read as bytes
#define SAVE_VERSION 1u

typedef struct
{
	unsigned int magic;
	unsigned int version;
	unsigned int layout;    // the caller's own idea of what the blob means
	unsigned int bytes;     // payload length, checked against what is asked for
	unsigned int checksum;  // over the payload only
} saveHeader;

static const char* reason = "not tried";

#if TEST_SAVELOAD_AUDIT
bool saveTestTearNextWrite = false;
#endif

const char* saveLastReason(void)
{
	return reason;
}

// FNV-1a. Not a security check - it is here to catch a half-written file after
// the console was switched off mid-save, which is the one corruption this can
// actually expect to meet.
static unsigned int hashBytes(const unsigned char* p, unsigned int n)
{
	unsigned int h = 2166136261u;
	for (unsigned int i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
	return h;
}

// Shared implementation for writing a blob to any path. Both saveWriteBlob and
// saveWriteSettingsBlob delegate to this, passing their target path.
static bool saveWritePath(const char* path, const void* blob, unsigned int bytes, unsigned int layout)
{
	if (!blob || bytes == 0) { reason = "nothing to write"; return false; }

	// First run on a fresh card has no 3ds/modelkit yet. An existing directory
	// comes back EEXIST, which is not a failure - only fopen can tell us whether
	// the card is really writable, so that is what decides.
	mkdir("sdmc:/3ds", 0777);
	mkdir(SAVE_DIR, 0777);

	// The new bytes go to a sibling temp file and only become the save once every
	// one of them is on the card. Written straight over the live file - which is
	// what this used to do - fopen("wb") truncates the good save to nothing before
	// the first byte of the replacement is written, so a console switched off, a
	// flat battery or a card pulled anywhere in the middle left the player with
	// neither the old build nor the new one.
	//
	// One save file is kept, not two: the temp exists only while the write is
	// running and is deleted whether it succeeds or fails, so nothing piles up on
	// the card. Same directory as the target, so the rename below moves a
	// directory entry rather than copying the payload a second time.
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);

	FILE* f = fopen(tmp, "wb");
	if (!f) { reason = "cannot open for write"; return false; }

	saveHeader h;
	h.magic    = SAVE_MAGIC;
	h.version  = SAVE_VERSION;
	h.layout   = layout;
	h.bytes    = bytes;
	h.checksum = hashBytes((const unsigned char*)blob, bytes);

	// How much of the payload actually goes down. Always all of it outside the
	// audit build - see saveTestTearNextWrite in save.h.
	unsigned int put = bytes;
#if TEST_SAVELOAD_AUDIT
	if (saveTestTearNextWrite) { put = bytes / 2; saveTestTearNextWrite = false; }
#endif

	// Compared against bytes, not put: a short write is a failed write, which is
	// exactly what a card that ran out mid-save would produce.
	bool ok = fwrite(&h, sizeof(h), 1, f) == 1
	       && fwrite(blob, 1, put, f) == bytes;

	// fclose is what actually pushes the last block at the card, so a write that
	// looked fine can still fail here. Treat that as a failed save.
	if (fclose(f) != 0) ok = false;

	if (!ok)
	{
		// The half-written file never becomes the save, and it does not stay on the
		// card either. Whatever was there before this call is still there.
		remove(tmp);
		reason = "write failed";
		return false;
	}

	// devkitPro's sdmc devoptab maps rename onto FSUSER_RenameFile, which refuses
	// a destination that already exists - so unlike a POSIX rename this cannot be
	// one atomic swap and the old file has to go first. The window in which
	// neither file exists is now these two calls instead of the whole write, which
	// is the improvement. Closing it completely would mean keeping a second copy
	// on the card permanently, and a second copy is the thing that was explicitly
	// not wanted.
	remove(path);
	if (rename(tmp, path) != 0)
	{
		remove(tmp);
		reason = "could not replace old save";
		return false;
	}

	reason = "ok";
	return true;
}

// Shared implementation for reading a blob from any path. Both saveReadBlob and
// saveReadSettingsBlob delegate to this, passing their target path.
static bool saveReadPath(const char* path, void* blob, unsigned int bytes, unsigned int layout)
{
	if (!blob || bytes == 0) { reason = "nothing to read"; return false; }

	FILE* f = fopen(path, "rb");
	if (!f) { reason = "no file"; return false; }

	saveHeader h;
	if (fread(&h, sizeof(h), 1, f) != 1)
	{
		fclose(f); reason = "truncated header"; return false;
	}
	if (h.magic != SAVE_MAGIC)     { fclose(f); reason = "not a save file";  return false; }
	if (h.version != SAVE_VERSION) { fclose(f); reason = "old version";      return false; }
	if (h.layout != layout)        { fclose(f); reason = "layout changed";   return false; }
	if (h.bytes != bytes)          { fclose(f); reason = "size changed";     return false; }

	// Straight into the caller's blob rather than via a scratch copy: the state
	// this holds is tens of kilobytes, and a second buffer that size would cost
	// more RAM than the rest of the game's bookkeeping put together. What makes
	// that safe is zeroing the blob on any failure below - all-zero is exactly
	// the state a console that has never saved starts in, so a corrupt file
	// degrades to a fresh start rather than to half a build.
	bool read = fread(blob, 1, bytes, f) == bytes;
	fclose(f);
	if (!read)
	{
		memset(blob, 0, bytes); reason = "truncated payload"; return false;
	}

	if (hashBytes((const unsigned char*)blob, bytes) != h.checksum)
	{
		memset(blob, 0, bytes); reason = "bad checksum"; return false;
	}

	reason = "ok";
	return true;
}

bool saveWriteBlob(const void* blob, unsigned int bytes, unsigned int layout)
{
	return saveWritePath(SAVE_PATH, blob, bytes, layout);
}

bool saveReadBlob(void* blob, unsigned int bytes, unsigned int layout)
{
	return saveReadPath(SAVE_PATH, blob, bytes, layout);
}

bool saveWriteSettingsBlob(const void* blob, unsigned int bytes, unsigned int layout)
{
	return saveWritePath(SETTINGS_PATH, blob, bytes, layout);
}

bool saveReadSettingsBlob(void* blob, unsigned int bytes, unsigned int layout)
{
	return saveReadPath(SETTINGS_PATH, blob, bytes, layout);
}
