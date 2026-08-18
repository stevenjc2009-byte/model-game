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
	const char* kitName;
} tutorialInfo;

// Draws the whole top-screen tutorial page. The caller has already cleared the
// target and made it current; this only draws.
void tutorialDrawSheet(C2D_TextBuf buf, const tutorialInfo* info);
