#pragma once
#include <citro2d.h>

typedef struct
{
	int   step;        // 1..4, which page to draw
	int   cutDone;     // parts snipped off so far
	int   filedDone;   // parts filed smooth so far
	int   builtDone;   // parts seated so far
	int   partTotal;   // parts in this kit (10)
	float filePct;     // 0..1, filing progress of the held part
	int   runnerNow;   // 1-based current runner
	int   runnerTotal; // runners in this kit
	// Which build step the manual is open at, and how many the kit has. This is
	// the build's own position - one step per socket, ten on every kit shipped
	// so far - and is a different question from `step` above, which is only
	// which of the four tutorial pages is on screen.
	int   stepNow;     // 1-based, 0 when there is no kit to be part-way through
	int   stepTotal;
	const char* kitName;
} tutorialInfo;

// Draws the whole top-screen tutorial page. The caller has already cleared the
// target and made it current; this only draws.
void tutorialDrawSheet(C2D_TextBuf buf, const tutorialInfo* info);

// The build-step bar across the top of the sheet, on its own.
//
// The tutorial-off card needs the same strip: it is the only progress readout
// left once the sheet is gone, and drawing it from a copy over there is how the
// two quietly drift apart. `now` is 1-based and clamped, `total` may be
// anything - it is the socket count of whatever kit is loaded.
void tutorialDrawStepBar(C2D_TextBuf buf, int now, int total);
