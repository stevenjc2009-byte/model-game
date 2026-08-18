// Saves the bottom screen's current 3D bench view to the SD card.
//
// One capture path, one format: a plain 24-bit BMP rather than PNG, because
// there is no PNG/zlib encoder in this tree and pulling one in for a single
// button is a bigger change than the feature. A BMP needs no compression
// step at all - the framebuffer's own bytes go straight to disk.

#pragma once

#include <3ds.h>

// Writes /modelkit/shot_NNN.bmp on the SD card, NNN continuing from whatever
// numbers are already there (so repeat sessions never overwrite an earlier
// shot). Returns false if the SD card is missing, full, or write-protected;
// the caller decides what, if anything, to tell the player.
bool screenshotCapture(void);
