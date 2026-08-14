// The front end: the menu the game boots into.
//
// It lives on the bottom screen and is driven entirely by the stylus. The top
// screen is the console and has no touch digitizer at all, so a menu drawn up
// there could only be worked with buttons - and buttons are the one input this
// project cannot verify in the emulator. Putting it on the glass is also the
// foundation the level select needs.

#pragma once

#include <3ds.h>
#include <citro3d.h>

// What the player asked for on this frame.
typedef enum
{
	TITLE_STAY,  ///< Still in the front end.
	TITLE_PLAY,  ///< Play was tapped - hand over to the workbench.
	TITLE_QUIT,  ///< Quit was tapped - close the game.
} titleAction;

// Parses the text the front end draws. Call once, after C2D_Init.
void titleInit(void);
void titleExit(void);

// True while the front end still owns the bottom screen. It owns it from boot.
bool titleActive(void);

// Hands the bottom screen back to the front end on the level select, showing
// the page the last-picked level sits on. This is what the pause menu's Quit
// does: it leaves the level, not the game. Only the Quit on the front end's own
// title page closes the game.
void titleReturnToLevels(void);

// The level the player picked off the grid, 1 to 20, and how many parts that
// level's kit is meant to hold. Both are valid before anything is picked - the
// front end starts on level 1 - so the workbench can print them from boot.
int titleLevel(void);
int titleLevelParts(void);
void titleCaptureStartLevel(int level);

// One frame of front-end input. Reads the stylus itself, so hidScanInput must
// already have run. Returns TITLE_STAY unless the player chose something.
titleAction titleInput(u32 kDown, u32 kHeld, u32 kUp);

// The front end's own top-screen text for whichever page is open. Safe to call
// every frame - it only reprints when the page or the volume has moved, and it
// does nothing once Play has handed the console to the workbench.
void titlePrintTop(bool isNew3DS);

// Draws the current page. Call inside a citro3d frame; it re-binds citro2d's
// own shader state, so the caller has to put the 3D state back before drawing
// the bench again.
void titleDraw(C3D_RenderTarget* target);
