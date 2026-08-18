// The breathing red dot: a marker drawn on the bottom screen, sitting on the
// actual piece of the kit the tutorial is currently asking the player to touch.
//
// The bottom screen is a resistive touch panel and the things being tapped are
// small - a gate is 0.09 units across, a socket is empty space. The tutorial
// sheet on the top screen already says what to do and draws a diagram of it,
// but the diagram is a picture of a generic box or runner, not of the kit on the
// bench, so it cannot say *where*. This does: it takes the same box the pick
// functions test a tap against, projects it through the same matrices, and marks
// the middle of it.
//
// It pulses rather than sitting still. A static dot on a busy 3D bench reads as
// part of the scenery; a slow expand-and-fade is the one thing on screen that
// moves while the player is deciding what to do, so the eye goes to it.

#pragma once

#include <3ds.h>
#include <citro3d.h>
#include <stdbool.h>

// Where the given tutorial step wants the stylus, in bottom-screen touch
// coordinates - x right, y down, the same space pickGate and friends work in,
// and the same space citro2d draws the bench overlay in.
//
// step is main.c's beginnerAction(): 0 open the box, 1 snip a gate, 2 rub a
// part smooth, 3 seat it. Answers false when the step has no single target
// (nothing left to do, the piece is off screen, the kit is finished), and the
// caller draws nothing - a dot with nowhere to be is worse than no dot.
bool hintPoint(const C3D_Mtx* modelView, int step, float* outX, float* outY);

// Draws the dot at a point hintPoint returned. citro2d must already be prepared
// and the bench target already the current scene; this only draws, the same
// contract tutorialDrawSheet works to.
void hintDraw(float x, float y);
