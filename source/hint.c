#include "hint.h"

#include "camera.h"
#include "mesh.h"
#include "picking.h"

#include <citro2d.h>
#include <math.h>

// The bottom screen, for the off-screen test below. Same duplication picking.c
// makes and for the same reason: it is the fixed 320x240 of the 3DS bottom
// screen, wanted here only inside screen-space arithmetic.
#define BOTTOM_W 320
#define BOTTOM_H 240

// How far outside the screen a target may be and still get a dot. A piece half
// off the edge is still worth pointing at - the dot is clipped, but the visible
// half of it tells the player which way to turn the bench. Well outside, and a
// dot pinned to the edge would only lie about where the piece is.
#define HINT_MARGIN 10.0f

// One breath. Slow enough to read as breathing rather than blinking: at 60 FPS
// this is about 78 frames in and 78 back out.
#define HINT_PERIOD_MS 2600.0

// The solid centre, at rest and at full breath. Kept small - it marks a spot,
// it does not cover the thing being marked, and the player still has to be able
// to see the gate they are about to snip.
#define HINT_CORE_MIN 3.6f
#define HINT_CORE_MAX 5.2f

// The halo that expands out of it and fades as it goes.
#define HINT_HALO_MIN 5.0f
#define HINT_HALO_MAX 15.0f

#define HINT_RED   C2D_Color32(0xD9, 0x3D, 0x36, 0xFF)
#define HINT_WHITE C2D_Color32(0xFF, 0xFF, 0xFF, 0xE0)

static const float noOffset[3] = { 0.0f, 0.0f, 0.0f };

// State main.c owns: the build in progress and the rules about what may be
// picked. Declared the same way picking.c declares them - this file asks the
// same questions of the same data, and a second copy of any of it would be a
// second thing to keep in step.
extern partState partStates[];
extern int currentRunner;
extern int selectedPart;
int  socketForPart(int part);

// The middle of the on-screen part of a projected box, or false if none of it
// is in shot. Padding is deliberately not passed through: the pick functions
// fatten their boxes so a near miss still counts, but a fattened box has the
// same centre as the box it grew from, and the dot marks the centre.
//
// The centre is taken after clipping to the screen rather than before. The
// closed kit box is what forced it: the opening framing shows only its
// right-hand end, so its true centre projects off the left edge, the margin
// test below threw the point out, and step 1 - "tap the box" - got no marker on
// the one thing it is asking for. Clipping first puts the dot in the middle of
// what the player can actually see and tap, which is also what they aim at.
static bool boxCentre(const C3D_Mtx* mvp, const float min[3], const float max[3],
	const float offset[3], float* outX, float* outY)
{
	float pad[3] = { 0.0f, 0.0f, 0.0f };
	float minX, minY, maxX, maxY, nearW;

	if (!projectBox(mvp, min, max, offset, pad, &minX, &minY, &maxX, &maxY, &nearW))
		return false;

	// Clipped against the screen the dot is drawn on, not against the padded
	// pick box: a dot has to be somewhere the stylus can reach.
	float visMinX = minX < 0.0f ? 0.0f : minX;
	float visMaxX = maxX > (float)BOTTOM_W ? (float)BOTTOM_W : maxX;
	float visMinY = minY < 0.0f ? 0.0f : minY;
	float visMaxY = maxY > (float)BOTTOM_H ? (float)BOTTOM_H : maxY;
	if (visMinX > visMaxX || visMinY > visMaxY) return false;

	*outX = (visMinX + visMaxX) * 0.5f;
	*outY = (visMinY + visMaxY) * 0.5f;
	return true;
}

// Step 1. The gate the player is being asked to snip: the first piece still on
// the frame that belongs to the runner currently on the bench. Same two tests
// pickGate makes, in the same order, so the dot can never sit on a gate a tap
// would not find.
//
// "First" is by part index rather than by anything cleverer. It is stable - the
// dot does not hop about the runner as the camera turns - and it walks the kit
// in the order the parts were authored in, which is the order the runner is laid
// out in.
//
// The gate is on the frame, and the frame is drawn - and picked, see handleTap -
// through buildRunnerView, not the bare bench view. Projecting it with the bench
// view put the dot hundreds of pixels off the side of the screen, so it was
// thrown out and no marker appeared while anything was still attached: the whole
// of step 1 had no dot. So this builds the frame's own transform, the same one
// pickGate is handed.
static bool gateTarget(const C3D_Mtx* modelView, float* outX, float* outY)
{
	const meshPart* parts = meshParts();

	for (int i = 0; i < meshPartCount(); i++)
	{
		if (partStates[i].cut) continue;
		if (parts[i].runner != currentRunner) continue;

		C3D_Mtx runnerView, mvp;
		buildRunnerView(&runnerView, modelView);
		Mtx_Multiply(&mvp, &pickProjection, &runnerView);
		return boxCentre(&mvp, parts[i].gateMin, parts[i].gateMax, noOffset, outX, outY);
	}

	return false;
}

// Step 2. The loose piece being filed. Whatever is in the player's hand if that
// is what needs rubbing, otherwise the first cut piece that still has a nub on
// it - the same order the tutorial sheet itself falls back on when nothing is
// held, so the dot and the words on the top screen always mean the same piece.
//
// A cut piece is drawn at its own offset, so the offset goes into the
// projection. That is the cut branch of pickPart, and a piece that is still
// sliding home to the mat gets a dot that slides with it, because the offset is
// what the animation moves.
static bool fileTarget(const C3D_Mtx* modelView, float* outX, float* outY)
{
	const meshPart* parts = meshParts();
	int target = -1;

	if (selectedPart >= 0 && selectedPart < meshPartCount()
		&& partStates[selectedPart].cut && !partStates[selectedPart].smooth)
	{
		target = selectedPart;
	}
	else
	{
		for (int i = 0; i < meshPartCount(); i++)
			if (partStates[i].cut && !partStates[i].smooth) { target = i; break; }
	}

	if (target < 0) return false;

	C3D_Mtx mvp;
	Mtx_Multiply(&mvp, &pickProjection, modelView);
	return boxCentre(&mvp, parts[target].min, parts[target].max,
		partStates[target].offset, outX, outY);
}

// Step 3. The hole the piece goes in. The one the ghost is standing in if the
// player is holding something ready to seat - that is the socket pickGhostSocket
// gives its fat allowance to, so the dot lands exactly where the generous hit
// box is - otherwise the first socket whose piece is filed and unseated, which
// is the first place anything can go.
//
// The same tests pickGhostSocket makes, and no more. There used to be a parent
// one here as well, matching the rule that a piece could not go on until the
// piece it hangs off was already on the stand; that rule is gone, so any filed
// piece can be fitted whenever the player likes and the dot follows the ghost
// wherever it now stands.
static bool socketTarget(const C3D_Mtx* mvp, float* outX, float* outY)
{
	const meshSocket* sockets = meshSockets();
	int sock = -1;

	if (selectedPart >= 0 && selectedPart < meshPartCount())
	{
		const partState* st = &partStates[selectedPart];
		if (st->cut && st->smooth && !st->seated)
			sock = socketForPart(selectedPart);
	}

	if (sock < 0)
	{
		for (int i = 0; i < meshSocketCount(); i++)
		{
			int part = sockets[i].part;
			if (part < 0 || part >= meshPartCount()) continue;
			if (partStates[part].seated) continue;
			if (!partStates[part].smooth) continue;
			sock = i;
			break;
		}
	}

	if (sock < 0 || sock >= meshSocketCount()) return false;
	return boxCentre(mvp, sockets[sock].min, sockets[sock].max, noOffset, outX, outY);
}

bool hintPoint(const C3D_Mtx* modelView, int step, float* outX, float* outY)
{
	C3D_Mtx mvp;
	Mtx_Multiply(&mvp, &pickProjection, modelView);

	float x = 0.0f, y = 0.0f;
	bool found = false;

	switch (step)
	{
		case 0:
		{
			// The closed box. Its bounds are fixed and it is drawn where it is
			// authored, so there is no offset to apply - the same call pickKitBox
			// makes.
			float min[3], max[3];
			meshKitBoxBounds(min, max);
			found = boxCentre(&mvp, min, max, noOffset, &x, &y);
			break;
		}
		case 1: found = gateTarget(modelView, &x, &y); break;
		case 2: found = fileTarget(modelView, &x, &y); break;
		case 3: found = socketTarget(&mvp, &x, &y); break;
		default: return false;
	}

	if (!found) return false;
	if (x < -HINT_MARGIN || x > BOTTOM_W + HINT_MARGIN) return false;
	if (y < -HINT_MARGIN || y > BOTTOM_H + HINT_MARGIN) return false;

	*outX = x;
	*outY = y;
	return true;
}

void hintDraw(float x, float y)
{
	// 0 at rest, 1 at full breath, and smooth through both ends. A raw sawtooth
	// on the clock would snap back to nothing once a cycle, which is a blink.
	double t = (double)(osGetTime() % (u64)HINT_PERIOD_MS) / HINT_PERIOD_MS;
	float wave = 0.5f - 0.5f * cosf((float)t * 2.0f * (float)M_PI);

	// The halo: grows the whole breath and fades as it grows, so the dot looks
	// like it is pushing outwards rather than just changing size.
	float halo = HINT_HALO_MIN + (HINT_HALO_MAX - HINT_HALO_MIN) * wave;
	u8 haloAlpha = (u8)(120.0f * (1.0f - wave));
	C2D_DrawEllipseSolid(x - halo, y - halo, 0.5f, halo * 2.0f, halo * 2.0f,
		C2D_Color32(0xD9, 0x3D, 0x36, haloAlpha));

	// A white ring under the core. Parts are authored in every colour the kit
	// palette holds, and a red dot on a red part is invisible; the ring is what
	// makes the marker read the same on all of them.
	float ring = HINT_CORE_MIN + (HINT_CORE_MAX - HINT_CORE_MIN) * wave + 1.6f;
	C2D_DrawEllipseSolid(x - ring, y - ring, 0.5f, ring * 2.0f, ring * 2.0f, HINT_WHITE);

	float core = HINT_CORE_MIN + (HINT_CORE_MAX - HINT_CORE_MIN) * wave;
	C2D_DrawEllipseSolid(x - core, y - core, 0.5f, core * 2.0f, core * 2.0f, HINT_RED);
}
