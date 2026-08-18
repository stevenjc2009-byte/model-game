// Transcription of tutorial-idea-A-v2.svg (the approved mockup) into citro2d
// draw calls. Coordinates below are copied straight out of the SVG's four
// panel groups, which are already laid out in native 400x240 screen space -
// nothing here is redesigned or rescaled, only translated call-for-call.

#include <math.h>
#include <stdio.h>

#include "tutorial_art.h"
#include "strings.h"

#define TUT_PI 3.14159265f

// ---------------------------------------------------------------------------
// Colours, lifted straight from the mockup's fill attributes.

#define CLR_PAPER    C2D_Color32(0xFB, 0xFA, 0xF6, 0xFF)
#define CLR_INK      C2D_Color32(0x26, 0x26, 0x24, 0xFF)
#define CLR_MUTED    C2D_Color32(0x55, 0x55, 0x5A, 0xFF)
#define CLR_TAG      C2D_Color32(0x8A, 0x8F, 0x95, 0xFF) // top-right "OF 4" / counters
#define CLR_PIP_OFF  C2D_Color32(0xC7, 0xCB, 0xD0, 0xFF)

#define CLR_BLUE     C2D_Color32(0x2C, 0x5C, 0x8F, 0xFF)
#define CLR_BLUE_LT  C2D_Color32(0x3C, 0x74, 0xAD, 0xFF)
#define CLR_BLUE_DK  C2D_Color32(0x22, 0x49, 0x6F, 0xFF)

#define CLR_RED      C2D_Color32(0xD9, 0x3B, 0x30, 0xFF)

#define CLR_AMBER    C2D_Color32(0xE0, 0x98, 0x2F, 0xFF)
#define CLR_AMBER_LT C2D_Color32(0xF0, 0xB4, 0x5C, 0xFF)
#define CLR_AMBER_DK C2D_Color32(0xC5, 0x7F, 0x1E, 0xFF)

#define CLR_GREEN    C2D_Color32(0x2C, 0x9B, 0x57, 0xFF)
#define CLR_MAT      C2D_Color32(0x4F, 0x7A, 0x4A, 0xFF)
#define CLR_DESK     C2D_Color32(0x9A, 0x7A, 0x4F, 0xFF)

#define CLR_CARD     C2D_Color32(0xE6, 0xDF, 0xCF, 0xFF)
#define CLR_CARD_DK  C2D_Color32(0xCF, 0xC5, 0xB0, 0xFF)
#define CLR_CARD_LT  C2D_Color32(0xF4, 0xEF, 0xE4, 0xFF)
#define CLR_CARD_EDGE C2D_Color32(0xB3, 0xA8, 0x8E, 0xFF) // box-lid edge lines

#define CLR_FRAME    C2D_Color32(0xC9, 0xC4, 0xB8, 0xFF)
#define CLR_FRAME_LT C2D_Color32(0xDC, 0xD7, 0xCC, 0xFF)
#define CLR_FRAME_DK C2D_Color32(0xA8, 0xA2, 0x96, 0xFF)

#define CLR_STY_TIP        C2D_Color32(0xC6, 0xCB, 0xD0, 0xFF)
#define CLR_STY_TIP_LIT    C2D_Color32(0xE4, 0xE8, 0xEB, 0xFF)
#define CLR_STY_FERRULE    C2D_Color32(0x9A, 0xA1, 0xA8, 0xFF)
#define CLR_STY_BARREL     C2D_Color32(0x2E, 0x33, 0x38, 0xFF)
#define CLR_STY_BARREL_LIT C2D_Color32(0x56, 0x5E, 0x66, 0xFF)
#define CLR_STY_GRIP       C2D_Color32(0x4A, 0x52, 0x59, 0xFF)
#define CLR_STY_CAP        C2D_Color32(0x1F, 0x24, 0x29, 0xFF)
#define CLR_STY_MARK       C2D_Color32(0x8C, 0x94, 0x9B, 0xFF)

#define CLR_PANEL    C2D_Color32(0xEA, 0xF4, 0xEE, 0xFF)
#define CLR_TRACK    C2D_Color32(0xDD, 0xE1, 0xE6, 0xFF)

// ---------------------------------------------------------------------------
// Small shape primitives everything else is built from.

static float clampf(float v, float lo, float hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

static void tri1(float x0, float y0, float x1, float y1, float x2, float y2, u32 clr)
{
	C2D_DrawTriangle(x0, y0, clr, x1, y1, clr, x2, y2, clr, 0.0f);
}

static void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, u32 clr)
{
	tri1(x0, y0, x1, y1, x2, y2, clr);
	tri1(x0, y0, x2, y2, x3, y3, clr);
}

// Fan-triangulated 5-point polygon, for the two SVG shapes that aren't plain
// quads (the still-joined part and the freed part in panel 2).
static void poly5(float x0, float y0, float x1, float y1, float x2, float y2,
	float x3, float y3, float x4, float y4, u32 clr)
{
	tri1(x0, y0, x1, y1, x2, y2, clr);
	tri1(x0, y0, x2, y2, x3, y3, clr);
	tri1(x0, y0, x3, y3, x4, y4, clr);
}

// The seven-point arrow glyph the mockup reuses three times ("it comes free",
// "flat and smooth", the small held-part arrow). Local verts are relative to
// the shaft's left edge and the glyph's vertical centre.
static void drawArrowGlyph(float leftX, float centerY, u32 clr)
{
	const float lx[7] = { 0.0f, 22.0f, 22.0f, 38.0f, 22.0f, 22.0f, 0.0f };
	const float ly[7] = { -6.0f, -6.0f, -14.0f, 0.0f, 14.0f, 6.0f, 6.0f };
	float x[7], y[7];
	for (int i = 0; i < 7; i++) { x[i] = leftX + lx[i]; y[i] = centerY + ly[i]; }
	for (int i = 1; i < 6; i++)
		tri1(x[0], y[0], x[i], y[i], x[i + 1], y[i + 1], clr);
}

// Front/right/top faces of an isometric block: front is the axis-aligned rect
// (x0,y0)-(x1,y1); (dx,dy) is the depth vector from a front corner to its
// matching back corner - the same shared shape behind every block the mockup
// draws (steps 1, 3 and 4).
static void drawIsoBox(float x0, float y0, float x1, float y1, float dx, float dy,
	u32 frontClr, u32 rightClr, u32 topClr)
{
	quad(x0, y0, x1, y0, x1, y1, x0, y1, frontClr);
	quad(x1, y0, x1 + dx, y0 + dy, x1 + dx, y1 + dy, x1, y1, rightClr);
	quad(x0, y0, x1, y0, x1 + dx, y0 + dy, x0 + dx, y0 + dy, topClr);
}

// A ring drawn as short line segments rather than a punched donut, so it
// reads correctly over whatever's underneath (paper in panel 1, the amber
// ghost in panel 4) instead of assuming a fixed background colour.
static void drawRingStroke(float cx, float cy, float r, float thickness, u32 clr)
{
	const int segs = 28;
	const float step = 2.0f * TUT_PI / (float)segs;
	float px = cx + r, py = cy;
	for (int i = 1; i <= segs; i++)
	{
		const float a = step * (float)i;
		const float x = cx + r * cosf(a);
		const float y = cy + r * sinf(a);
		C2D_DrawLine(px, py, clr, x, y, clr, thickness, 0.0f);
		px = x; py = y;
	}
}

static void drawTapTarget(float cx, float cy, float r)
{
	const float midR = r * 0.65f;
	const float innerR = r * 0.34f;
	drawRingStroke(cx, cy, r, 2.0f, CLR_RED);
	drawRingStroke(cx, cy, midR, 2.4f, CLR_RED);
	C2D_DrawEllipseSolid(cx - innerR, cy - innerR, 0.0f, innerR * 2.0f, innerR * 2.0f, CLR_RED);
}

// One dashed segment, matching the SVG's stroke-dasharray="5 4" ghost outline.
static void drawDashedSegment(float x0, float y0, float x1, float y1, float thickness, u32 clr)
{
	const float dash = 5.0f, gap = 4.0f;
	const float dx = x1 - x0, dy = y1 - y0;
	const float len = sqrtf(dx * dx + dy * dy);
	if (len < 0.001f) return;
	const float ux = dx / len, uy = dy / len;
	float pos = 0.0f;
	while (pos < len)
	{
		const float end = pos + dash < len ? pos + dash : len;
		C2D_DrawLine(x0 + ux * pos, y0 + uy * pos, clr, x0 + ux * end, y0 + uy * end, clr, thickness, 0.0f);
		pos += dash + gap;
	}
}

// The reusable stylus from the SVG <defs>. Tip sits at (tipX,tipY); the body
// runs out along the given angle. No matrix helper for C2D shapes, so each
// local vertex is scaled, rotated and translated by hand.
static void stylusXY(float tipX, float tipY, float cosR, float sinR, float scl,
	float lx, float ly, float* outX, float* outY)
{
	lx *= scl; ly *= scl;
	*outX = tipX + lx * cosR - ly * sinR;
	*outY = tipY + lx * sinR + ly * cosR;
}

static void stylusTri(float tipX, float tipY, float cosR, float sinR, float scl,
	float x0, float y0, float x1, float y1, float x2, float y2, u32 clr)
{
	float ax, ay, bx, by, cx, cy;
	stylusXY(tipX, tipY, cosR, sinR, scl, x0, y0, &ax, &ay);
	stylusXY(tipX, tipY, cosR, sinR, scl, x1, y1, &bx, &by);
	stylusXY(tipX, tipY, cosR, sinR, scl, x2, y2, &cx, &cy);
	tri1(ax, ay, bx, by, cx, cy, clr);
}

static void stylusQuad(float tipX, float tipY, float cosR, float sinR, float scl,
	float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, u32 clr)
{
	stylusTri(tipX, tipY, cosR, sinR, scl, x0, y0, x1, y1, x2, y2, clr);
	stylusTri(tipX, tipY, cosR, sinR, scl, x0, y0, x2, y2, x3, y3, clr);
}

static void drawStylus(float tipX, float tipY, float degrees, float scale)
{
	const float rad = degrees * (TUT_PI / 180.0f);
	const float cosR = cosf(rad), sinR = sinf(rad);

	// tip cone: full shadow half, then a lit half along the upper edge
	stylusTri(tipX, tipY, cosR, sinR, scale, 0.0f, 0.0f, 15.0f, -4.2f, 15.0f, 4.2f, CLR_STY_TIP);
	stylusTri(tipX, tipY, cosR, sinR, scale, 0.0f, 0.0f, 15.0f, -4.2f, 15.0f, 0.0f, CLR_STY_TIP_LIT);
	// ferrule
	stylusQuad(tipX, tipY, cosR, sinR, scale, 15.0f, -5.0f, 22.0f, -5.0f, 22.0f, 5.0f, 15.0f, 5.0f, CLR_STY_FERRULE);
	// barrel body and its top highlight strip
	stylusQuad(tipX, tipY, cosR, sinR, scale, 22.0f, -5.6f, 78.0f, -7.2f, 78.0f, 7.2f, 22.0f, 5.6f, CLR_STY_BARREL);
	stylusQuad(tipX, tipY, cosR, sinR, scale, 22.0f, -5.6f, 78.0f, -7.2f, 78.0f, -4.4f, 22.0f, -3.2f, CLR_STY_BARREL_LIT);
	// grip band
	stylusQuad(tipX, tipY, cosR, sinR, scale, 34.0f, -6.6f, 49.0f, -6.6f, 49.0f, 6.6f, 34.0f, 6.6f, CLR_STY_GRIP);
	// end cap
	stylusQuad(tipX, tipY, cosR, sinR, scale, 78.0f, -7.2f, 86.0f, -5.6f, 86.0f, 5.6f, 78.0f, 7.2f, CLR_STY_CAP);
	// small highlight mark on the barrel
	stylusQuad(tipX, tipY, cosR, sinR, scale, 60.0f, -7.0f, 63.0f, -7.0f, 63.0f, -1.0f, 60.0f, -1.0f, CLR_STY_MARK);
}

// ---------------------------------------------------------------------------
// Text. Every call parses fresh into the caller's buffer - none of this is
// static across frames, since most of it carries live counters.

static void text(C2D_TextBuf buf, float x, float y, float scale, u32 clr, const char* s)
{
	C2D_Text t;
	C2D_TextParse(&t, buf, s);
	C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, scale, scale, clr);
}

static void textRight(C2D_TextBuf buf, float xRight, float y, float scale, u32 clr, const char* s)
{
	C2D_Text t;
	C2D_TextParse(&t, buf, s);
	C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor | C2D_AlignRight, xRight, y, 0.5f, scale, scale, clr);
}

static void textCenter(C2D_TextBuf buf, float xCenter, float y, float scale, u32 clr, const char* s)
{
	C2D_Text t;
	C2D_TextParse(&t, buf, s);
	C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor | C2D_AlignCenter, xCenter, y, 0.5f, scale, scale, clr);
}

// ---------------------------------------------------------------------------
// The four progress bars and "STEP N" heading shared by every page.

static void drawPips(C2D_TextBuf buf, int step)
{
	const float pipX[4] = { 20.0f, 112.0f, 204.0f, 296.0f };
	for (int i = 0; i < 4; i++)
		C2D_DrawRectSolid(pipX[i], 10.0f, 0.0f, 82.0f, 9.0f, i < step ? CLR_BLUE : CLR_PIP_OFF);

	// Sized in bytes, not letters. "ETAPE 4" is seven and used to fit here with
	// nothing to spare; the French "ETAPE" carries an acute on the E, which is
	// two bytes in UTF-8, and the step number was being truncated away.
	char label[16];
	snprintf(label, sizeof(label), STR(STR_T_STEP_N), step);
	text(buf, 20.0f, 24.0f, 0.42f, CLR_BLUE, label);
}

// ---------------------------------------------------------------------------
// Step 1 - OPEN THE BOX

static void drawStepOpen(C2D_TextBuf buf, const tutorialInfo* info)
{
	(void)info;
	drawPips(buf, 1);
	textRight(buf, 378.0f, 24.0f, 0.42f, CLR_TAG, STR(STR_T_OF_4));
	text(buf, 20.0f, 44.0f, 0.60f, CLR_INK, STR(STR_T_OPEN_TITLE));
	text(buf, 20.0f, 71.0f, 0.42f, CLR_MUTED, STR(STR_T_OPEN_SUB));

	// desk and mat
	quad(40.0f, 208.0f, 300.0f, 208.0f, 352.0f, 176.0f, 92.0f, 176.0f, CLR_DESK);
	quad(72.0f, 200.0f, 262.0f, 200.0f, 302.0f, 176.0f, 112.0f, 176.0f, CLR_MAT);

	// the box, lid already tilted off
	drawIsoBox(118.0f, 152.0f, 232.0f, 192.0f, 42.0f, -26.0f, CLR_CARD, CLR_CARD_DK, CLR_CARD_LT);
	C2D_DrawLine(118.0f, 152.0f, CLR_CARD_EDGE, 232.0f, 152.0f, CLR_CARD_EDGE, 1.5f, 0.0f);
	C2D_DrawLine(232.0f, 152.0f, CLR_CARD_EDGE, 232.0f, 192.0f, CLR_CARD_EDGE, 1.5f, 0.0f);
	C2D_DrawLine(232.0f, 152.0f, CLR_CARD_EDGE, 274.0f, 126.0f, CLR_CARD_EDGE, 1.5f, 0.0f);

	// "it lifts off" up-arrow callout
	C2D_DrawRectSolid(294.0f, 112.0f, 0.0f, 9.0f, 32.0f, CLR_BLUE);
	tri1(282.0f, 114.0f, 315.0f, 114.0f, 298.5f, 92.0f, CLR_BLUE);
	textCenter(buf, 298.0f, 152.0f, 0.38f, CLR_BLUE, STR(STR_T_OPEN_LIFTS));

	drawTapTarget(196.0f, 140.0f, 26.0f);
	drawStylus(200.0f, 136.0f, 38.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Step 2 - SNIP EVERY PART OFF

static void drawStepSnip(C2D_TextBuf buf, const tutorialInfo* info)
{
	drawPips(buf, 2);
	char tag[24];
	snprintf(tag, sizeof(tag), STR(STR_T_SNIP_COUNT), info->cutDone, info->partTotal);
	textRight(buf, 378.0f, 24.0f, 0.42f, CLR_TAG, tag);
	text(buf, 20.0f, 44.0f, 0.60f, CLR_INK, STR(STR_T_SNIP_TITLE));
	text(buf, 20.0f, 71.0f, 0.42f, CLR_MUTED, STR(STR_T_SNIP_SUB));

	// frame, with a shallow 3D edge so it reads as plastic
	quad(34.0f, 196.0f, 240.0f, 196.0f, 250.0f, 188.0f, 44.0f, 188.0f, CLR_FRAME_DK);
	C2D_DrawRectSolid(34.0f, 98.0f, 0.0f, 206.0f, 98.0f, CLR_FRAME);
	C2D_DrawRectSolid(34.0f, 98.0f, 0.0f, 206.0f, 7.0f, CLR_FRAME_LT);
	C2D_DrawRectSolid(48.0f, 112.0f, 0.0f, 178.0f, 70.0f, CLR_PAPER);

	// the part, still joined - it touches the bridge, the bridge touches the rail
	poly5(106.0f, 120.0f, 190.0f, 120.0f, 200.0f, 128.0f, 200.0f, 174.0f, 106.0f, 174.0f, CLR_BLUE);
	quad(106.0f, 120.0f, 190.0f, 120.0f, 200.0f, 128.0f, 116.0f, 128.0f, CLR_BLUE_LT);
	C2D_DrawRectSolid(122.0f, 140.0f, 0.0f, 44.0f, 20.0f, CLR_BLUE_DK);
	// the bridge: spans rail edge to part edge, no gap at either end
	C2D_DrawRectSolid(48.0f, 141.0f, 0.0f, 58.0f, 13.0f, CLR_AMBER);
	C2D_DrawRectSolid(48.0f, 141.0f, 0.0f, 58.0f, 4.0f, CLR_AMBER_LT);
	// call out the joint explicitly
	C2D_DrawLine(77.0f, 141.0f, CLR_MUTED, 77.0f, 116.0f, CLR_MUTED, 1.4f, 0.0f);
	C2D_DrawLine(77.0f, 116.0f, CLR_MUTED, 96.0f, 116.0f, CLR_MUTED, 1.4f, 0.0f);
	text(buf, 100.0f, 106.0f, 0.38f, CLR_MUTED, STR(STR_T_SNIP_BRIDGE));

	drawTapTarget(77.0f, 147.0f, 22.0f);
	drawStylus(81.0f, 143.0f, 38.0f, 0.9f);

	drawArrowGlyph(256.0f, 148.0f, CLR_GREEN);
	C2D_DrawRectSolid(302.0f, 118.0f, 0.0f, 76.0f, 62.0f, CLR_PANEL);
	C2D_DrawRectSolid(302.0f, 118.0f, 0.0f, 76.0f, 2.0f, CLR_GREEN);
	C2D_DrawRectSolid(302.0f, 178.0f, 0.0f, 76.0f, 2.0f, CLR_GREEN);
	C2D_DrawRectSolid(302.0f, 118.0f, 0.0f, 2.0f, 62.0f, CLR_GREEN);
	C2D_DrawRectSolid(376.0f, 118.0f, 0.0f, 2.0f, 62.0f, CLR_GREEN);
	poly5(316.0f, 134.0f, 356.0f, 134.0f, 364.0f, 141.0f, 364.0f, 166.0f, 316.0f, 166.0f, CLR_BLUE);
	textCenter(buf, 340.0f, 184.0f, 0.38f, CLR_GREEN, STR(STR_T_SNIP_FREE));

	// DO ALL TEN: label + ten-cell tally, driven by cutDone
	text(buf, 34.0f, 209.0f, 0.38f, CLR_MUTED, STR(STR_T_SNIP_ALL_TEN));
	for (int i = 0; i < info->partTotal; i++)
		C2D_DrawRectSolid(106.0f + (float)i * 18.0f, 208.0f, 0.0f, 14.0f, 11.0f,
			i < info->cutDone ? CLR_BLUE : CLR_PIP_OFF);
}

// ---------------------------------------------------------------------------
// Step 3 - RUB THE BUMP FLAT

static void drawStepFile(C2D_TextBuf buf, const tutorialInfo* info)
{
	drawPips(buf, 3);
	char tag[24];
	snprintf(tag, sizeof(tag), STR(STR_T_FILE_PCT), (int)(clampf(info->filePct, 0.0f, 1.0f) * 100.0f + 0.5f));
	textRight(buf, 378.0f, 24.0f, 0.42f, CLR_TAG, tag);
	text(buf, 20.0f, 44.0f, 0.60f, CLR_INK, STR(STR_T_FILE_TITLE));
	text(buf, 20.0f, 71.0f, 0.42f, CLR_MUTED, STR(STR_T_FILE_SUB));

	// same isometric world as step 1
	quad(30.0f, 204.0f, 240.0f, 204.0f, 288.0f, 174.0f, 78.0f, 174.0f, CLR_DESK);
	quad(56.0f, 198.0f, 212.0f, 198.0f, 250.0f, 174.0f, 94.0f, 174.0f, CLR_MAT);

	// the loose part as a 3D block, with the bump sitting on its top face
	drawIsoBox(86.0f, 150.0f, 176.0f, 188.0f, 36.0f, -22.0f, CLR_BLUE, CLR_BLUE_DK, CLR_BLUE_LT);
	quad(140.0f, 132.0f, 164.0f, 132.0f, 164.0f, 120.0f, 140.0f, 120.0f, CLR_AMBER);
	quad(164.0f, 132.0f, 176.0f, 125.0f, 176.0f, 113.0f, 164.0f, 120.0f, CLR_AMBER_DK);
	quad(140.0f, 120.0f, 164.0f, 120.0f, 176.0f, 113.0f, 152.0f, 113.0f, CLR_AMBER_LT);
	C2D_DrawLine(158.0f, 113.0f, CLR_AMBER_DK, 176.0f, 98.0f, CLR_AMBER_DK, 1.4f, 0.0f);
	C2D_DrawLine(176.0f, 98.0f, CLR_AMBER_DK, 196.0f, 98.0f, CLR_AMBER_DK, 1.4f, 0.0f);
	text(buf, 200.0f, 93.0f, 0.38f, CLR_AMBER_DK, STR(STR_T_FILE_BUMP));

	// rub motion arrow across the bump
	C2D_DrawRectSolid(104.0f, 94.0f, 0.0f, 70.0f, 7.0f, CLR_RED);
	tri1(104.0f, 87.0f, 104.0f, 108.0f, 88.0f, 97.5f, CLR_RED);
	tri1(174.0f, 87.0f, 174.0f, 108.0f, 190.0f, 97.5f, CLR_RED);
	drawStylus(152.0f, 116.0f, 38.0f, 1.0f);

	// result
	drawArrowGlyph(228.0f, 164.0f, CLR_GREEN);
	drawIsoBox(276.0f, 150.0f, 344.0f, 188.0f, 32.0f, -19.0f, CLR_BLUE, CLR_BLUE_DK, CLR_BLUE_LT);
	textCenter(buf, 326.0f, 118.0f, 0.38f, CLR_GREEN, STR(STR_T_FILE_SMOOTH));

	// filing progress bar, driven by filePct
	C2D_DrawRectSolid(30.0f, 214.0f, 0.0f, 228.0f, 13.0f, CLR_TRACK);
	C2D_DrawRectSolid(30.0f, 214.0f, 0.0f, 228.0f * clampf(info->filePct, 0.0f, 1.0f), 13.0f, CLR_GREEN);
	text(buf, 268.0f, 217.0f, 0.38f, CLR_MUTED, STR(STR_T_FILE_KEEP));
}

// ---------------------------------------------------------------------------
// Step 4 - PUT IT WHERE IT GLOWS

static void drawStepFit(C2D_TextBuf buf, const tutorialInfo* info)
{
	drawPips(buf, 4);
	char tag[24];
	snprintf(tag, sizeof(tag), STR(STR_T_FIT_COUNT), info->builtDone, info->partTotal);
	textRight(buf, 378.0f, 24.0f, 0.42f, CLR_TAG, tag);
	text(buf, 20.0f, 44.0f, 0.60f, CLR_INK, STR(STR_T_FIT_TITLE));
	text(buf, 20.0f, 71.0f, 0.42f, CLR_MUTED, STR(STR_T_FIT_SUB));

	quad(148.0f, 208.0f, 300.0f, 208.0f, 336.0f, 186.0f, 184.0f, 186.0f, CLR_DESK);
	drawIsoBox(196.0f, 158.0f, 254.0f, 190.0f, 28.0f, -16.0f, CLR_BLUE, CLR_BLUE_DK, CLR_BLUE_LT);

	// the ghost target: three pre-blended amber shades stand in for the SVG's
	// translucent fills, since alpha-over-arbitrary-background isn't assumed here
	drawIsoBox(196.0f, 114.0f, 254.0f, 142.0f, 28.0f, -16.0f, CLR_AMBER, CLR_AMBER_LT, CLR_AMBER_DK);
	drawDashedSegment(196.0f, 114.0f, 254.0f, 114.0f, 2.0f, CLR_AMBER);
	drawDashedSegment(254.0f, 114.0f, 282.0f, 98.0f, 2.0f, CLR_AMBER);
	drawDashedSegment(196.0f, 114.0f, 196.0f, 142.0f, 2.0f, CLR_AMBER);
	drawDashedSegment(196.0f, 142.0f, 254.0f, 142.0f, 2.0f, CLR_AMBER);
	drawDashedSegment(254.0f, 142.0f, 254.0f, 114.0f, 2.0f, CLR_AMBER);

	drawTapTarget(228.0f, 120.0f, 22.0f);
	drawStylus(232.0f, 116.0f, 38.0f, 1.0f);

	textCenter(buf, 60.0f, 114.0f, 0.38f, CLR_MUTED, STR(STR_T_FIT_HOLDING));
	drawIsoBox(30.0f, 134.0f, 78.0f, 164.0f, 20.0f, -12.0f, CLR_BLUE, CLR_BLUE_DK, CLR_BLUE_LT);
	drawArrowGlyph(106.0f, 148.0f, C2D_Color32(0x2C, 0x5C, 0x8F, 140));
}

// ---------------------------------------------------------------------------

void tutorialDrawSheet(C2D_TextBuf buf, const tutorialInfo* info)
{
	C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 400.0f, 240.0f, CLR_PAPER);

	switch (info->step)
	{
		case 1:  drawStepOpen(buf, info); break;
		case 2:  drawStepSnip(buf, info); break;
		case 3:  drawStepFile(buf, info); break;
		case 4:  drawStepFit(buf, info);  break;
		default: break;
	}
}
