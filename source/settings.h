// Settings that outlive the screen that sets them.
//
// Master volume lives here rather than inside either menu because the title
// screen and the pause menu both set the same one. Two copies would let a
// player change the level in one place and have the other contradict it.

#pragma once

// The bar is ten cells wide and moves one cell a press, so the level can never
// change without the player seeing it change.
#define VOLUME_CELLS 10
#define VOLUME_STEP  10

// 0 to 100. Nothing reads it yet - the app has no audio at all - so this is a
// setting waiting for a mixer rather than a control that does anything today.
// It is in now so the Options page is real the moment sound lands, and so the
// level a player picks holds for the rest of the session.
int settingsVolume(void);

// Moves the level by delta and clamps to 0..100. It clamps rather than wraps: a
// player winding the volume down to silence should not have the next press drop
// them back to full.
void settingsVolumeStep(int delta);
