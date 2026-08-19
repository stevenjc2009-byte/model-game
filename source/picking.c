#include "picking.h"

#include "camera.h"
#include "mesh.h"

#include <math.h>

// The bottom screen's own resolution. Duplicated from main.c's BOTTOM_W/
// BOTTOM_H rather than shared through a header: both are the fixed 320x240
// of the 3DS bottom screen, used here only inside projectBox's screen-space
// maths, and main.c keeps its own copy for building the render target.
#define BOTTOM_W 320
#define BOTTOM_H 240

// A gate is 0.09 units across - a few pixels. Fatten its hit box so the
// stylus only has to land near it, the way nippers only have to reach the
// gate.
#define GATE_PAD_XZ 0.06f
#define GATE_PAD_Y  0.03f

// Assembly. A socket's hit box is the part-sized space it will be filled
// with, fattened so the stylus only has to land near it - the same
// allowance the gates get, and for the same reason: a resistive screen and a
// fingernail-sized target.
#define SOCKET_PAD 0.10f

// The one socket the ghost is standing in gets a much fatter allowance than
// the rest. It is the only socket on screen with a visible shape in it, the
// player is being shown exactly where the part goes, and asking them to hit
// it to within a couple of pixels after that is just annoying - so anywhere
// in or around the ghost counts as a tap on it. It is safe to be this
// generous because the fat box is only ever tested for the part currently
// in hand, once that part is snipped and filed and has somewhere to go.
#define GHOST_PAD 0.20f

// Every part's tap box is grown a little into the empty space around it, so a
// piece a few pixels across can be caught without landing the stylus exactly on
// it. Two rules stop that from turning into taps on the wrong part.
//
// First, the allowance is only consulted for a tap that hit nothing at all.
// pickPart resolves the true, undersized boxes first and falls back to the grown
// ones only if the point was inside none of them. A grown box therefore cannot
// take a tap away from a part that really is under the stylus, however fat it
// is - which is the whole of "the hit boxes must not collide with each other".
//
// Second, when two grown boxes both reach the same empty pixel, the part whose
// own box the tap missed by the smaller fraction of its allowance wins. Each
// part owns the space nearest to it and nothing else, so two neighbours stay
// told apart - the same answer pickSocket already gives for its fat boxes, for
// the same reason.
//
// The numbers are in touch-screen pixels because pixels are what the player is
// aiming with, and a world-space allowance would mean something different at
// every camera distance. PICK_PAD_PX is what everything gets. PICK_MIN_TAP_PX is
// the size a box is grown up to when the flat allowance would still leave it
// smaller than that, which is how the fiddly pieces get more help than the
// structural ones without a size rule of their own. PICK_PAD_MAX_PX stops that
// running away on a part that is a pixel or two across at a distance.
#define PICK_PAD_PX      4.0f
#define PICK_MIN_TAP_PX 20.0f
#define PICK_PAD_MAX_PX 10.0f

float pickPadScale = 1.0f;

float pickPadPixels(float extent)
{
	float pad = PICK_PAD_PX;
	float grow = (PICK_MIN_TAP_PX - extent) * 0.5f;
	if (grow > pad) pad = grow;
	if (pad > PICK_PAD_MAX_PX) pad = PICK_PAD_MAX_PX;
	return pad * pickPadScale;
}

static const float noOffset[3] = { 0.0f, 0.0f, 0.0f };

// Two projections of the same view. The tilted one main.c draws the bottom
// screen with; this plain one is used to work out where a part lands in
// touch coordinates, where x runs right and y runs down. Built once, from
// the same FIELD_OF_VIEW/aspect pair, inside main.c's sceneInit.
C3D_Mtx pickProjection;

// State this file reads and mutates but does not own - main.c owns the
// build in progress and the gameplay rules for what a part or socket needs
// before it can be picked.
extern partState partStates[];
extern int currentRunner;
extern int selectedPart;
float boxStage(float start, float end);
int socketForPart(int part);

// Projects a model-space box, shifted by `offset`, into touch coordinates.
// Returns false if none of it is in front of the camera. `nearW` comes back
// as the closest corner, so overlapping boxes can be ordered front to back.
bool projectBox(const C3D_Mtx* mvp, const float min[3], const float max[3],
	const float offset[3], float pad[3], float* outMinX, float* outMinY,
	float* outMaxX, float* outMaxY, float* nearW)
{
	float lo[3], hi[3];
	for (int a = 0; a < 3; a++)
	{
		lo[a] = min[a] + offset[a] - pad[a];
		hi[a] = max[a] + offset[a] + pad[a];
	}

	*outMinX = *outMinY =  1e9f;
	*outMaxX = *outMaxY = -1e9f;
	*nearW = 1e9f;
	bool visible = false;

	for (int c = 0; c < 8; c++)
	{
		C3D_FVec corner = FVec4_New(
			(c & 1) ? hi[0] : lo[0],
			(c & 2) ? hi[1] : lo[1],
			(c & 4) ? hi[2] : lo[2],
			1.0f);

		C3D_FVec clip = Mtx_MultiplyFVec4(mvp, corner);
		if (clip.w <= 0.01f) continue; // behind the camera

		float sx = (clip.x / clip.w * 0.5f + 0.5f) * BOTTOM_W;
		float sy = (0.5f - clip.y / clip.w * 0.5f) * BOTTOM_H;

		if (sx < *outMinX) *outMinX = sx;
		if (sx > *outMaxX) *outMaxX = sx;
		if (sy < *outMinY) *outMinY = sy;
		if (sy > *outMaxY) *outMaxY = sy;
		if (clip.w < *nearW) *nearW = clip.w;
		visible = true;
	}

	return visible;
}

// Which part's gate is under this touch point? Only parts still on the
// runner have one. Returns -1 if the stylus missed every gate.
int pickGate(const C3D_Mtx* modelView, int tx, int ty)
{
	C3D_Mtx mvp;
	Mtx_Multiply(&mvp, &pickProjection, modelView);

	float pad[3] = { GATE_PAD_XZ, GATE_PAD_Y, GATE_PAD_XZ };
	const meshPart* parts = meshParts();
	int   best  = -1;
	float bestW = 1e9f;

	for (int i = 0; i < meshPartCount(); i++)
	{
		if (partStates[i].cut) continue;
		if (meshParts()[i].runner != currentRunner) continue;

		float minX, minY, maxX, maxY, nearW;
		if (!projectBox(&mvp, parts[i].gateMin, parts[i].gateMax, noOffset, pad,
				&minX, &minY, &maxX, &maxY, &nearW))
			continue;

		if (tx >= minX && tx <= maxX && ty >= minY && ty <= maxY && nearW < bestW)
		{
			bestW = nearW;
			best  = i;
		}
	}

	return best;
}

// The closed box is the only thing on the bench that can be tapped before
// the runner has arrived. Its fixed AABB is deliberately tested ahead of
// all kit picking so the concealed frame cannot be cut through the lid.
bool pickKitBox(const C3D_Mtx* modelView, int tx, int ty)
{
	C3D_Mtx mvp;
	float min[3], max[3], pad[3] = { 0.08f, 0.08f, 0.08f };
	float minX, minY, maxX, maxY, nearW;
	meshKitBoxBounds(min, max);
	Mtx_Multiply(&mvp, &pickProjection, modelView);
	if (!projectBox(&mvp, min, max, noOffset, pad, &minX, &minY, &maxX, &maxY, &nearW))
		return false;
	return tx >= minX && tx <= maxX && ty >= minY && ty <= maxY;
}

// Where a part's tap box lands on the touch screen, before any allowance.
//
// Uncut pieces travel with the rotated, rear-desk runner. Cut pieces use their
// independent mat/stand offset. Everything here has to match what sceneRender
// actually put on screen, or a tap lands on a piece the player cannot see: an
// uncut part only exists while its own runner is the one on the bench, and only
// once the box has finished opening.
bool pickPartScreenRect(const C3D_Mtx* modelView, int index,
	float* outMinX, float* outMinY, float* outMaxX, float* outMaxY, float* nearW)
{
	float pad[3] = { 0.0f, 0.0f, 0.0f };
	const meshPart* parts = meshParts();

	if (!partStates[index].cut)
	{
		if (parts[index].runner != currentRunner) return false;
		if (boxStage(0.16f, 0.42f) <= 0.0f) return false;
	}

	C3D_Mtx partView = *modelView;
	const float* offset = partStates[index].offset;
	if (partStates[index].cut)
		Mtx_Translate(&partView, offset[0], offset[1], offset[2], true);
	else
		buildRunnerView(&partView, modelView);

	// A packed part is drawn shrunk into its runner cell. Picking it by its
	// full authored size would reach well past the drawn piece and into its
	// neighbours' cells, so while it is still on the runner it is picked at
	// the size it is drawn at.
	const float* pickMin = partStates[index].cut ? parts[index].min : parts[index].runnerMin;
	const float* pickMax = partStates[index].cut ? parts[index].max : parts[index].runnerMax;

	C3D_Mtx partMvp;
	Mtx_Multiply(&partMvp, &pickProjection, &partView);
	return projectBox(&partMvp, pickMin, pickMax, noOffset, pad,
		outMinX, outMinY, outMaxX, outMaxY, nearW);
}

// Which part is under this touch point? Takes the nearest box the point
// falls inside. Returns -1 for empty space.
//
// Failing that, the nearest box the point fell just outside of - see the
// allowance rules at the top of this file. The two passes are worked out in one
// loop and chosen between at the end rather than short-circuited, so the answer
// does not depend on which part happens to be first in the list.
int pickPart(const C3D_Mtx* modelView, int tx, int ty)
{
	int   best  = -1;
	float bestW = 1e9f;
	int   nearest      = -1;
	float nearestScore = 1e9f;
	float nearestW     = 1e9f;

	for (int i = 0; i < meshPartCount(); i++)
	{
		float minX, minY, maxX, maxY, w;
		if (!pickPartScreenRect(modelView, i, &minX, &minY, &maxX, &maxY, &w))
			continue;

		if (tx >= minX && tx <= maxX && ty >= minY && ty <= maxY)
		{
			if (w < bestW) { bestW = w; best = i; }
			continue;
		}

		// Outside its real box, so it is only a candidate for the fallback, and
		// only if the miss was inside the allowance. Scored by how far outside it
		// landed as a fraction of that allowance: 0 at the edge of the box, 1 at
		// the edge of the allowance, so the comparison between two parts is fair
		// even when one of them was granted a bigger allowance than the other.
		float padX = pickPadPixels(maxX - minX);
		float padY = pickPadPixels(maxY - minY);
		if (padX <= 0.0f || padY <= 0.0f) continue;

		float outX = tx < minX ? (minX - tx) / padX : (tx > maxX ? (tx - maxX) / padX : 0.0f);
		float outY = ty < minY ? (minY - ty) / padY : (ty > maxY ? (ty - maxY) / padY : 0.0f);
		float score = outX > outY ? outX : outY;
		if (score > 1.0f) continue;

		if (score < nearestScore || (score == nearestScore && w < nearestW))
		{
			nearestScore = score;
			nearestW     = w;
			nearest      = i;
		}
	}

	return best >= 0 ? best : nearest;
}

// Which socket on the build stand is waiting for this part, or -1 if the
// part has nowhere to go. Same nearest-box rule as pickPart, with a fatter
// allowance, since a socket is an empty space rather than something you can
// see the edges of.
int pickSocket(const C3D_Mtx* modelView, int tx, int ty)
{
	C3D_Mtx mvp;
	Mtx_Multiply(&mvp, &pickProjection, modelView);

	float pad[3] = { SOCKET_PAD, SOCKET_PAD, SOCKET_PAD };
	const meshSocket* sockets = meshSockets();
	int   best      = -1;
	float bestScore = 1e9f;
	float bestW     = 1e9f;

	for (int i = 0; i < meshSocketCount(); i++)
	{
		float minX, minY, maxX, maxY, nearW;
		if (!projectBox(&mvp, sockets[i].min, sockets[i].max, noOffset, pad,
				&minX, &minY, &maxX, &maxY, &nearW))
			continue;

		if (tx < minX || tx > maxX || ty < minY || ty > maxY) continue;

		// Sockets sit close together on a small model, so with the fat allowance
		// above their boxes overlap on screen - and picking whichever of them is
		// nearest the camera threw away the few pixels that told the two apart. A
		// tap dead centre of the far socket lost to a near one it had only just
		// clipped the edge of.
		//
		// Score how deep inside its own box the tap landed instead: 0 is dead
		// centre, 1 is right on the edge. The best-aimed box wins, so every
		// socket keeps the whole of its own middle, and depth only breaks a tie
		// between two the tap is equally well inside of.
		float halfX = (maxX - minX) * 0.5f;
		float halfY = (maxY - minY) * 0.5f;
		float offX  = halfX > 0.0f ? fabsf(tx - (minX + halfX)) / halfX : 0.0f;
		float offY  = halfY > 0.0f ? fabsf(ty - (minY + halfY)) / halfY : 0.0f;
		float score = offX > offY ? offX : offY;

		if (score < bestScore || (score == bestScore && nearW < bestW))
		{
			bestScore = score;
			bestW     = nearW;
			best      = i;
		}
	}

	return best;
}

// The generous version, for the one socket the ghost is standing in.
//
// It tests a single box - the home of the part in hand - rather than all of
// them, so a fat allowance costs nothing: there is no other socket for it
// to swallow. It answers -1 unless that part is genuinely ready to go in,
// which is what keeps it from stealing a tap meant for the runner: an
// uncut part has no ghost on the stand, so its box is not in play at all.
//
// "Ready" no longer includes the piece it hangs off already being on the stand.
// The kit builds in any order now, so a filed piece can be tapped into its hole
// whenever the player wants it there - and sceneRender draws the ghost under
// exactly these tests, so what is on screen and what a tap can hit stay one box.
int pickGhostSocket(const C3D_Mtx* modelView, int tx, int ty)
{
	// The same part sceneRender draws the ghost for, so the box on screen and
	// the box a tap can hit are always the same box.
	int ghostPart = selectedPart;
	if (ghostPart < 0) return -1;

	const partState* st = &partStates[ghostPart];
	if (st->seated || !st->cut || !st->smooth) return -1;

	int sock = socketForPart(ghostPart);
	if (sock < 0) return -1;

	C3D_Mtx mvp;
	Mtx_Multiply(&mvp, &pickProjection, modelView);

	float pad[3] = { GHOST_PAD, GHOST_PAD, GHOST_PAD };
	const meshSocket* s = &meshSockets()[sock];
	float minX, minY, maxX, maxY, nearW;

	if (!projectBox(&mvp, s->min, s->max, noOffset, pad,
			&minX, &minY, &maxX, &maxY, &nearW))
		return -1;

	if (tx < minX || tx > maxX || ty < minY || ty > maxY) return -1;
	return sock;
}
