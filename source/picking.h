// Ray-picking: turning a stylus touch point into "which gate / part / socket
// / kit box is under this point", plus the shared box-to-screen projection
// the pick functions and one main.c audit (panMarkerScreenX) both build on.
//
// pickProjection is the same view buildModelView produces, just untilted -
// see main.c's sceneInit, which builds both the render projection and this
// one from the same FIELD_OF_VIEW/aspect pair so the tilt is the only
// difference between what is drawn and what a tap is tested against.

#pragma once

#include <3ds.h>
#include <citro3d.h>
#include <stdbool.h>

// Where each part is relative to where it was moulded. Zero while it is
// still on the runner; the offset out to its place on the mat once it has
// been cut free. See main.c's partStates[] for the full field-by-field
// explanation - the type lives here, not there, because picking tests it
// directly and a struct shared across translation units can only be defined
// once.
//
// filed runs 0 to 1 as the stylus wears the nub down. smooth latches when it
// gets there, because that is the flag assembly asks about - a part is
// either ready to seat or it is not, and it never goes back.
//
// A part travels twice - off the runner onto the mat, then off the mat onto
// the build stand - so the move is stored as from -> target rather than as a
// plain target. Without the start point the second trip would snap back to
// the mat before setting off.
typedef struct
{
	bool  cut;
	bool  seated;
	bool  moving;
	u64   startMs;
	float from[3];
	float target[3];
	float offset[3];
	float filed;
	bool  smooth;
	// Item 35: how far this part is posed, in degrees, about the joint
	// mesh.c authored for it - meaningless and left at 0 for a part with no
	// joint. picking.c never reads this; it lives here only because
	// partState is the one per-part record and a second one would have to
	// stay in step with it by hand.
	float poseDeg;
	// Item 32: a player-chosen repaint. PART_COLOUR_NONE keeps the part's
	// authored colour; any other value is one more than the partMaterialFor()
	// palette index in main.c (1-4 for palette 0-3), so that a freshly
	// zeroed partState - every memset(partStates, 0, ...) reset in main.c
	// runs through this, and rebuilding that list by hand to also stamp a
	// "none" sentinel would be one more place to keep in step - already
	// reads as "no override" for free. picking.c never reads this either -
	// same reasoning as poseDeg above.
	unsigned char colourOverride;
} partState;

// partState.colourOverride's "no override" value. Deliberately 0, the same
// value memset() and static zero-init already leave it at.
#define PART_COLOUR_NONE 0

extern C3D_Mtx pickProjection;

bool projectBox(const C3D_Mtx* mvp, const float min[3], const float max[3],
	const float offset[3], float pad[3], float* outMinX, float* outMinY,
	float* outMaxX, float* outMaxY, float* nearW);

// Where a part's tap box lands on the touch screen, in the picker's own terms -
// the same rect pickPart tests the stylus against, before any allowance. False
// if the part is not in play this frame (still on another runner, or the kit box
// has not opened yet) or is entirely behind the camera.
//
// Public only so that TEST_PICKPAD_AUDIT can aim its samples at real parts. A
// second copy of this in the audit would be free to drift away from the picker
// and then agree with itself about the wrong boxes.
bool pickPartScreenRect(const C3D_Mtx* modelView, int index,
	float* outMinX, float* outMinY, float* outMaxX, float* outMaxY, float* nearW);

// How many touch-screen pixels of allowance a part's box gets, given how many
// pixels across it already is. See picking.c for the rules that keep this from
// stealing taps from neighbouring parts.
float pickPadPixels(float extent);

// How much of that allowance is actually applied, 0 or 1. Ships at 1 and is only
// ever moved by TEST_PICKPAD_AUDIT, which has to ask the real picker the same
// question with the allowance switched off - that answer is the reference "no
// tap changed hands" is measured against.
extern float pickPadScale;

int  pickGate(const C3D_Mtx* modelView, int tx, int ty);
bool pickKitBox(const C3D_Mtx* modelView, int tx, int ty);
int  pickPart(const C3D_Mtx* modelView, int tx, int ty);
int  pickSocket(const C3D_Mtx* modelView, int tx, int ty);
int  pickGhostSocket(const C3D_Mtx* modelView, int tx, int ty);
