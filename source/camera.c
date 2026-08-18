#include "camera.h"

#include "mesh.h"

#include <math.h>
#include <stdio.h>

// Kit-box progression state and the animation curve derived from it - both
// owned and defined by main.c. blockerLive needs to know whether the box is
// still shut; buildRunnerView needs the same 0..1 curve boxStage already
// produces for the lid and tray. Declared here rather than duplicated, so
// there is exactly one definition of "how open is the box".
extern float boxOpen;
float boxStage(float start, float end);

// How far the scene is lifted while the camera is still pulled back to see
// the whole bench, easing out to nothing as focusAmt reaches a locked-on
// part. Only used by the two functions below, so it stays private to them
// rather than joining the shared camera.h limits.
#define SCENE_LIFT 1.00f

float angleX = 0.70f, angleY = 3.4915927f;
float camDist = CAM_START;

// What the camera turns and zooms around. With nothing selected it sits at
// the middle of the bench; select a part and it eases across to that part,
// so from then on the view orbits the thing being worked on. focusAmt is how
// far that move has got - 0 is the whole bench, 1 is locked onto the part.
float focus[3] = { 0.0f, 0.0f, 0.0f };
float focusAmt = 0.0f;

// How far along the bench the player has slid the view with the Circle Pad.
// Held as an offset rather than written into focus, because focus is
// re-derived every frame from whatever is being worked on - a slide written
// straight into it would be eased away again on the very next frame.
float camPanX = 0.0f;
float camPanZ = 0.0f;

bool photoMode = false;
int  photoShot = 0;

// The normal level view is a seated working view, not a room-tour view.  It is
// aimed at the middle of the cutting mat, which is both the middle of the desk
// between the cut-parts area and the kit box, and the place the work actually
// happens.  Everything comes from mesh accessors to stay aligned if the
// workstation moves again.
//
// It used to be aimed at a weighted point between the stand and the kit box.
// That put the aim point hard against the left face of the box stack, so with
// the camera no longer allowed through furniture the view collapsed onto the
// pivot and the opening frame was the inside of a cardboard box.
//
// The stand-back is as far as the ceiling allows: at this pitch the camera sits
// at y 2.08 against a ceiling underside of 2.32, and going further would push it
// through the roof and drop the ceiling on the opening frame.  Measured on
// device, this framing gives the shut kit box 4080 of 19200 sampled touch points
// against 1620 for the old one, so it is also the easier thing to tap.
//
// The pivot is a function rather than two copies of the same three lines
// because it was two copies, and they drifted apart: this one was moved to the
// mat while the one updateFocus eases towards was left on the old weighted
// point.  The opening frame was therefore correct for about a second, then the
// ease pulled the pivot onto the front face of the kit box at x 2.577, the
// furniture block allowed zero stand-back from a pivot inside a solid, and the
// camera parked in the cardboard.  One definition, so they cannot disagree again.
// How far the pivot is nudged along the desk from the middle of the mat towards
// the kit box, in world units. The whole mat and the whole box cannot both be
// in the opening shot: the frame is about 0.71 * camDist wide, so even at the
// furthest stand-back the ceiling allows it covers 4.63 either side of the aim
// point, and the box's far end at x 7.20 is 2.4 units outside that. This buys
// back roughly a fifth of the box - enough for it to read as the thing to tap -
// while keeping all of the mat in frame. It also stays 1.38 clear of the box's
// front face at x 2.58, which is where a pivot must not end up.
#define PIVOT_TOWARDS_BOX 1.035f
void benchPivot(float out[3], float* amt)
{
	out[0] = meshMatX() + PIVOT_TOWARDS_BOX;
	out[1] = MAT_TOP + 0.24f;
	out[2] = meshMatZ();
	*amt = 0.20f;
}

void frameWorkbenchCamera(void)
{
	angleX = 0.74f;
	angleY = 3.4915927f;
	camDist = 5.60f;
	// A framing is a fresh start, so the slide goes with it. Left standing, a
	// pan from the last kit would push the opening shot off the bench.
	camPanX = camPanZ = 0.0f;
	benchPivot(focus, &focusAmt);
}

// Photo mode: a clean look at what has been built.
//
// The top screen is a text console, so hiding the interface here means the
// manual and the live readout stop printing and a short caption takes their
// place; the bottom screen is only ever the model, so it needs nothing doing
// to it beyond a better angle.
//
// The framings are authored rather than borrowed. There is already a table
// of eight camera presets next door under TEST_CAPTURE_VIEW, but those exist
// to audit the bedroom - the wide room shot, the rear wall, the view along
// the bed - and none of them is pointed at the thing the player made. These
// four are: the box-art three-quarter, side on, down from above, and a low
// angle along the bench, all pivoting on the stand so they hold whatever is
// on it.
//
// The distances are set for the tallest kit in the game, the rocket, because
// a shot that crops the model is worse than one with room around it and L/R
// still zooms in photo mode. The low angle is a shallow 0.06 rather than the
// negative pitch it started as: below zero the camera passes under the desk
// top and the photograph is two-thirds desk edge. PITCH_LIMIT is 1.45 and
// does not catch that, since it exists to stop the bench rolling over, not
// to keep the camera above the table.
typedef struct { float pitch, yaw, dist, lift; } photoPreset;

static const photoPreset photoShots[PHOTO_SHOT_COUNT] =
{
	{  0.22f, 3.4915927f, 6.00f, 0.55f },  // three-quarter, the box-art angle
	{  0.18f, 4.7600000f, 5.90f, 0.55f },  // straight down the side
	{  0.68f, 3.4915927f, 5.60f, 0.35f },  // from above, over the bench
	{  0.06f, 3.4915927f, 5.30f, 0.70f },  // low, looking along the bench
};

void framePhotoCamera(void)
{
	const photoPreset* p = &photoShots[photoShot];
	angleX = p->pitch;
	angleY = p->yaw;
	camDist = p->dist;
	focus[0] = meshStandX();
	focus[1] = MAT_TOP + p->lift;
	focus[2] = meshStandZ();
	focusAmt = 1.0f;
}

void frameRunnerCamera(void)
{
	angleX = 0.92f;
	angleY = 3.4915927f;
	// The flat runner is wider than the finished model; back out just enough
	// to retain the rails, loose-part area, mat and stand in one working view.
	camDist = 4.75f;
	focus[0] = meshRunnerX();
	focus[1] = MAT_TOP + 0.24f;
	focus[2] = meshRunnerZ();
	focusAmt = 0.55f;
}

void frameAssemblyCamera(void)
{
	// Once the runner is empty, return from the rail-wide view to the centred
	// model stand. updateFocus supplies the exact socket/model centre each frame.
	angleX = 0.74f;
	angleY = 3.4915927f;
	camDist = 4.35f;
}

// How far the camera sits from the point it is looking at, as a vector.
//
// buildModelView stands the camera back and lifts the scene, both in eye
// space, then turns the whole thing; undoing that gives this. Working it out
// directly instead of inverting the matrix is what makes it cheap enough to
// test a sight line against every piece of furniture every frame.
static void cameraOffsetVecAt(float pitch, float out[3])
{
	float cx = cosf(pitch), sx = sinf(pitch);
	float cy = cosf(angleY), sy = sinf(angleY);
	float lift = SCENE_LIFT * (1.0f - focusAmt);
	out[0] = -lift*sx*sy - camDist*cx*sy;
	out[1] = -lift*cx    + camDist*sx;
	out[2] =  lift*sx*cy + camDist*cx*cy;
}

// The blocker list, copied out once per kit rather than asked for per test.
// Only the kit box stack changes between levels - it grows a box per runner -
// but the whole list is cheap to refresh and one code path is easier to trust
// than two.
#define MAX_BLOCKERS 32
static float blockerMin[MAX_BLOCKERS][3], blockerMax[MAX_BLOCKERS][3];
static int   blockerCount = 0;

void refreshBlockers(void)
{
	blockerCount = meshBlockerCount();
	if (blockerCount > MAX_BLOCKERS)
	{
		// Dropping the tail silently would let the camera pass through whichever
		// furniture fell off the end, which reads as a rendering glitch rather
		// than a list that needs growing. mesh.c reports its own overflows the
		// same way with meshDroppedCount().
		printf("BLOCKERS TRUNCATED: %d of %d (raise MAX_BLOCKERS)\n",
			MAX_BLOCKERS, blockerCount);
		blockerCount = MAX_BLOCKERS;
	}
	for (int b = 0; b < blockerCount; b++)
		meshBlockerBounds(b, blockerMin[b], blockerMax[b]);
}

// Whether a blocker is actually in the room right now. Everything on the list
// is permanent except the kit box stack, which stops being drawn the moment the
// box finishes opening - and the runner then sits exactly where it was, so
// leaving it in would fence off the workspace.
static bool blockerLive(int b)
{
	if (b == meshBlockerKitBoxIndex()) return boxOpen < 1.0f;
	return true;
}

static bool pointInBlocker(const float p[3], int b)
{
	return p[0] > blockerMin[b][0] && p[0] < blockerMax[b][0]
	    && p[1] > blockerMin[b][1] && p[1] < blockerMax[b][1]
	    && p[2] > blockerMin[b][2] && p[2] < blockerMax[b][2];
}

// How much of that offset the camera may actually use.
//
// The walls are hidden because the camera stands outside them; the desk, the
// stand, the box stack, the bed and the shelf sit in the middle of the room, and
// hiding one of those would just leave a hole in the scene. So the line from the
// point being looked at out to the camera is tested against every solid thing,
// and the camera is drawn in to the first one it meets - it slides along the
// desk edge instead of sinking through it.
//
// Testing the whole line rather than the camera's own position is the point:
// stopping at the first thing in the way is what stops the view passing
// *through* something to a clear spot on the far side.
//
// The pivot is the point being looked at, which is a spot on the mat or a part
// resting on it, and so is always in open air - a ray has to start somewhere
// clear or there is no line to shorten. Both the standing back and the scene
// lift shrink together, because they are one offset from that pivot.
static float cameraBlockFractionAt(float pitch)
{
	float v[3];
	cameraOffsetVecAt(pitch, v);
	float len = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
	if (len < 1e-4f) return 1.0f;
	float dir[3] = { v[0]/len, v[1]/len, v[2]/len };

	float limit = len;
	for (int b = 0; b < blockerCount; b++)
	{
		if (!blockerLive(b)) continue;
		// A pivot inside something cannot be backed out of it, and treating it as
		// a wall would pin the camera on the spot instead of shortening the view.
		if (pointInBlocker(focus, b)) continue;
		// Standard slab test: the ray is inside the box between the last entry
		// and the first exit, so the last entry is where it first hits.
		float enter = 0.0f, leave = limit;
		bool hit = true;
		for (int a = 0; a < 3 && hit; a++)
		{
			if (fabsf(dir[a]) < 1e-6f)
			{
				if (focus[a] <= blockerMin[b][a] || focus[a] >= blockerMax[b][a]) hit = false;
			}
			else
			{
				float inv = 1.0f / dir[a];
				float ta = (blockerMin[b][a] - focus[a]) * inv;
				float tb = (blockerMax[b][a] - focus[a]) * inv;
				if (ta > tb) { float swap = ta; ta = tb; tb = swap; }
				if (ta > enter) enter = ta;
				if (tb < leave) leave = tb;
				if (enter > leave) hit = false;
			}
		}
		if (hit && enter < limit) limit = enter;
	}

	limit -= CAM_BLOCK_SKIN;
	// Everything between here and the pivot is clear, so collapsing all the way
	// onto it is always safe even though it is an unusually close view.
	if (limit < 0.0f) limit = 0.0f;
	return limit / len;
}

// How far back the camera has to be able to stand before the view stops
// counting as jammed. The player's own zoom refuses to come closer than
// CAM_NEAR_LIMIT, so any view the furniture forces that is tighter than that is
// one they could never have chosen for themselves - which is exactly what it
// felt like. If they have deliberately zoomed closer than that, their own
// distance is the target instead; the camera is not going to argue them back
// out.
static float cameraWantedStand(void)
{
	return camDist < CAM_NEAR_LIMIT ? camDist : CAM_NEAR_LIMIT;
}

// Look down over the obstacle rather than collapse onto the pivot.
//
// Blocking used to have one answer for a solid in the way: shorten the view
// until it stops short of it. Orbiting round to the kit-box side of the bench
// with the box still shut is the case where that answer is wrong - the box is
// right beside the mat, so the straight line back is blocked almost at once and
// the camera ends up 1.2 units from the pivot, sat at mat level with the stand's
// base filling the screen. Measured, not guessed: the block audit's worst
// sample is L1, box shut, yaw 4.71, pitch 0.24, 6.50 requested and 1.33 given.
//
// A camera that cannot back away from something can still rise above it, so
// that is what this does: steepen the pitch a step at a time until the line
// back is clear enough, and build the view at that pitch. Nothing here moves
// the camera off the line the block test checked, so it still cannot end up
// inside anything - it just approaches from higher up.
//
// The search stops at the ceiling as well as at PITCH_LIMIT. Blocking only ever
// shortens the offset, so testing the full-length height is the cautious side of
// the check. And it stops the moment the view is comfortable, so every framing
// that was already clear - which is all four the game puts the player in, each
// measured at 0.99 - comes back at exactly the pitch it asked for.
#define CAM_LIFT_STEP   0.06f
#define CAM_LIFT_STEPS  16

// The fraction that goes with the pitch comes back through fraction, because the
// search has already worked it out for every pitch it tried - including the one
// it settles on. Asking for it again afterwards walked the blocker list a second
// time for an answer that was already in hand: measured at 8 slab tests a frame
// where 4 would do, and 20 instead of 16 in the worst orbit.
static float cameraViewPitch(float* fraction)
{
	float want = cameraWantedStand();
	float best = angleX;
	float bestFrac = cameraBlockFractionAt(angleX);
	float bestStand = bestFrac * camDist;
	if (bestStand >= want) { *fraction = bestFrac; return angleX; }

	// The search used to stop at the room ceiling. Behind a shut kit box that
	// left it nothing to find: clearing the lid needs about 1.2 of pitch, which
	// puts the camera at y 4.69 against a ceiling of 2.26, so the view stayed
	// jammed in at 1.49 no matter how far back the player asked to be. Rising
	// through the ceiling in that corner is the trade steve chose over the close
	// view; the pitch clamp below is still the only limit.
	for (int i = 1; i <= CAM_LIFT_STEPS; i++)
	{
		float pitch = angleX + i * CAM_LIFT_STEP;
		if (pitch > PITCH_LIMIT) break;

		float frac  = cameraBlockFractionAt(pitch);
		float stand = frac * camDist;
		if (stand > bestStand) { bestStand = stand; bestFrac = frac; best = pitch; }
		if (stand >= want) { *fraction = frac; return pitch; }
	}
	*fraction = bestFrac;
	return best;
}

void buildModelView(C3D_Mtx* out)
{
	// Read right to left: put the focus point at the origin, turn the scene
	// around it, then stand back. The lift that centres the whole bench is taken
	// away as the camera locks onto a part, so the part ends up dead centre
	// rather than sitting high.
	//
	// The stand-back and the lift are both scaled by however much of the way out
	// to the camera is clear, so the camera stops at the first solid thing in the
	// line rather than passing into or through it.
	Mtx_Identity(out);
	// The pitch the view is built at is not always the pitch the player asked
	// for: when the straight line back is blocked the camera rises over the
	// obstacle instead of collapsing onto the pivot. Everything downstream -
	// picking included - is fed this same matrix, so the tap and the frame it is
	// tested against still cannot disagree.
	float s;
	float pitch = cameraViewPitch(&s);
	Mtx_Translate(out, 0.0f, s * SCENE_LIFT * (1.0f - focusAmt), -s * camDist, true);
	Mtx_RotateX(out, pitch, true);
	Mtx_RotateY(out, angleY, true);
	Mtx_Translate(out, -focus[0], -focus[1], -focus[2], true);
}

void cameraWorldPosition(const C3D_Mtx* modelView, float out[3])
{
	C3D_Mtx inverse = *modelView;
	Mtx_Inverse(&inverse);
	C3D_FVec p = Mtx_MultiplyFVec4(&inverse, FVec4_New(0.0f, 0.0f, 0.0f, 1.0f));
	float iw = p.w != 0.0f ? 1.0f / p.w : 1.0f;
	out[0] = p.x * iw; out[1] = p.y * iw; out[2] = p.z * iw;
}

void buildRunnerView(C3D_Mtx* out, const C3D_Mtx* modelView)
{
	float rise = boxStage(0.16f, 0.42f);
	float lower = boxStage(0.72f, 0.94f);
	float boxed[3];
	meshKitBoxedOffset(boxed);
	*out = *modelView;
	// The authored runner is vertical; rotate it down while it is still in the
	// box, then keep it flat in the box's former place on the desk.
	Mtx_RotateX(out, 1.5707963f, true);
	// After the 90-degree X rotation, local Y is desk depth and local Z is
	// vertical. Map the authored vertical runner onto the rear desk footprint.
	Mtx_Translate(out, boxed[0], boxed[2], -boxed[1] - 0.15f - 0.72f * rise * (1.0f - lower), true);
}
