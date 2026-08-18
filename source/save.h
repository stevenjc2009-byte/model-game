// Progress that outlives the session.
//
// The workbench already keeps one build per level in RAM, so nothing here knows
// what a part or a socket is: main.c hands over that block of state as an opaque
// blob and gets it back again next boot. Keeping the layout out of this file is
// the point - the save code cannot go stale against a struct it never reads.
//
// What it does guard is the blob being read back by a build that means something
// different by it. A saved file carries the exact byte size and a layout id the
// caller composes from its own limits, and a file that disagrees with either is
// refused rather than poured into memory that is no longer shaped like it.

#pragma once

#include <stdbool.h>

// Where the files live on the card. Both are used in the on-screen report when a
// save fails, so the player is told what could not be written rather than just
// that something did not work.
#define SAVE_DIR      "sdmc:/3ds/modelkit"
#define SAVE_PATH     SAVE_DIR "/save.bin"
#define SETTINGS_PATH SAVE_DIR "/settings.bin"

// Writes the blob, creating the directory if this is the first run. Returns
// false if the card could not be written; the caller is expected to say so
// rather than swallow it, because a silent failure here loses the player's
// build with nothing on screen to explain it.
bool saveWriteBlob(const void* blob, unsigned int bytes, unsigned int layout);

// Fills the blob from the card. Returns false if there is no file yet, if it is
// truncated, if its checksum does not match, or if it was written by a build
// whose state was a different size or layout - all of which mean "start fresh",
// not "fail". On any of those the blob is left all-zero, which is the state a
// console that has never saved boots in, so the caller does not have to undo a
// partial read.
bool saveReadBlob(void* blob, unsigned int bytes, unsigned int layout);

// Settings file equivalents: same semantics as the blob functions but write to
// settings.bin instead of save.bin. This keeps settings separate from build
// progress so neither can corrupt the other on disk.
bool saveWriteSettingsBlob(const void* blob, unsigned int bytes, unsigned int layout);
bool saveReadSettingsBlob(void* blob, unsigned int bytes, unsigned int layout);

// Why the last saveWriteBlob or saveReadBlob answered the way it did, as a short
// phrase fit for the console: "ok", "no file", "bad checksum", and so on. Never
// NULL.
const char* saveLastReason(void);

#include "debug.h"
#if TEST_SAVELOAD_AUDIT
// Fault injection, compiled out of every build but the save/load audit.
//
// One failure the game must survive cannot be provoked from outside: a card that
// stops taking bytes half way through a write. A directory standing where
// save.bin goes blocks fopen, but nothing available to a 3DS app makes fwrite
// come up short - so the audit sets this, and the very next write stops after
// half the payload and reports the failure honestly. What is left on the card is
// a real torn file, header and all, for the load path to be tested against.
//
// Cleared by the write it tears, so it can never affect a second one.
extern bool saveTestTearNextWrite;
#endif
