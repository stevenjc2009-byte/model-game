#include <stdio.h>
#include <citro2d.h>

#include "title.h"
#include "settings.h"
#include "controls.h"
#include "memory_status.h"

// ---------------------------------------------------------------------------
// Layout
//
// Bottom screen, 320x240. Every button is at least 48 px tall because that is
// the smallest thing a thumb-held stylus hits reliably on a 3DS.

typedef struct { float x, y, w, h; } rect;

#define SCREEN_W 320.0f
#define SCREEN_H 240.0f

// The plaques each page's name sits on. These are labels lying on the mat
// rather than bands drawn across it, so the mat stays the surface everything
// else is resting on.
static const rect rPlaqueTitle = {  12.0f,   8.0f, 236.0f, 46.0f };
static const rect rPlaquePage  = {   8.0f,   6.0f, 190.0f, 28.0f };

// Title page
static const rect rPlay    = {  40.0f,  68.0f, 240.0f, 48.0f };
static const rect rOptions = {  40.0f, 124.0f, 240.0f, 48.0f };
static const rect rQuit    = {  40.0f, 180.0f, 240.0f, 48.0f };

// Options page
static const rect rMinus    = {  12.0f,  62.0f,  48.0f, 48.0f };
static const rect rPlus     = { 260.0f,  62.0f,  48.0f, 48.0f };
static const rect rControls = {  40.0f, 126.0f, 240.0f, 48.0f };
static const rect rOptBack  = {  40.0f, 184.0f, 168.0f, 48.0f };
static const rect rRam      = { 216.0f, 184.0f,  92.0f, 48.0f };

// Controls page
static const rect rCtlBack  = {  40.0f, 188.0f, 240.0f, 48.0f };

// Level select page
//
// Ten levels to a page in a 5x2 grid. 54 px tiles on a 62 px pitch is the
// widest grid that still leaves an 8 px gutter between neighbours, so a tap
// that lands between two tiles picks neither rather than the wrong one - and
// every tile clears the 48 px a stylus needs with room to spare. Two pages
// cover the twenty levels.
#define LEVEL_COUNT      20
#define LEVELS_PER_PAGE  10
#define LEVEL_PAGES      (LEVEL_COUNT / LEVELS_PER_PAGE)
#define LEVEL_COLS        5

static const rect rLevel[LEVELS_PER_PAGE] =
{
	{   8.0f,  52.0f, 54.0f, 52.0f }, {  70.0f,  52.0f, 54.0f, 52.0f },
	{ 132.0f,  52.0f, 54.0f, 52.0f }, { 194.0f,  52.0f, 54.0f, 52.0f },
	{ 256.0f,  52.0f, 54.0f, 52.0f },
	{   8.0f, 112.0f, 54.0f, 52.0f }, {  70.0f, 112.0f, 54.0f, 52.0f },
	{ 132.0f, 112.0f, 54.0f, 52.0f }, { 194.0f, 112.0f, 54.0f, 52.0f },
	{ 256.0f, 112.0f, 54.0f, 52.0f },
};

static const rect rPrev    = {   8.0f, 178.0f,  54.0f, 50.0f };
static const rect rLevBack = {  70.0f, 178.0f, 180.0f, 50.0f };
static const rect rNext    = { 256.0f, 178.0f,  54.0f, 50.0f };

// The volume bar sits between the minus and plus buttons.
#define BAR_X 68.0f
#define BAR_Y 74.0f
#define BAR_W 184.0f
#define BAR_H 24.0f

// The front end is the workbench seen from directly above: a green cutting mat
// with its ruled grid, and everything on it moulded out of the same white
// styrene the kit is. Nothing here is a menu colour - every tone is a material.
#define CLR_BACK     C2D_Color32(0x4A, 0x75, 0x39, 0xFF)  // the mat itself
#define CLR_MAT_GRID C2D_Color32(0x40, 0x67, 0x31, 0xFF)  // its printed rule
#define CLR_MAT_EDGE C2D_Color32(0x2E, 0x4A, 0x25, 0xFF)  // the mat's border
#define CLR_STYRENE  C2D_Color32(0xE8, 0xE6, 0xDF, 0xFF)  // unpainted plastic
#define CLR_STY_EDGE C2D_Color32(0xA8, 0xA4, 0x99, 0xFF)  // its moulded edge
#define CLR_STY_DOWN C2D_Color32(0xC6, 0xC2, 0xB6, 0xFF)  // pressed into the mat
#define CLR_RUNNER   C2D_Color32(0x9A, 0x94, 0x88, 0xFF)  // the sprue frame
#define CLR_RUN_LIP  C2D_Color32(0xB4, 0xAE, 0xA0, 0xFF)
#define CLR_INK      C2D_Color32(0x23, 0x20, 0x1C, 0xFF)  // text on plastic
#define CLR_INK_DIM  C2D_Color32(0x6A, 0x66, 0x5E, 0xFF)
#define CLR_TEXT     C2D_Color32(0xEC, 0xF3, 0xE2, 0xFF)  // text on the mat
#define CLR_DIM      C2D_Color32(0xCB, 0xDE, 0xB6, 0xFF)
#define CLR_TAG      C2D_Color32(0xCB, 0xDE, 0xB6, 0xFF)
#define CLR_BAR_ON   C2D_Color32(0x8F, 0xC0, 0x5A, 0xFF)
#define CLR_BAR_OFF  C2D_Color32(0xC6, 0xC2, 0xB6, 0xFF)

// Accent colours. Green is the thing that starts the game, amber is a setting,
// muted red is anything that leaves. The level tiles reuse the same three as a
// size scale, so a colour means the same thing wherever it turns up.
#define CLR_ACC_GO  C2D_Color32(0x8F, 0xC0, 0x5A, 0xFF)
#define CLR_ACC_SET C2D_Color32(0xC8, 0x9B, 0x3C, 0xFF)
#define CLR_ACC_OUT C2D_Color32(0xB0, 0x4A, 0x3A, 0xFF)

// ---------------------------------------------------------------------------
// State

typedef enum { PAGE_TITLE, PAGE_LEVELS, PAGE_OPTIONS, PAGE_CONTROLS } titlePageId;

// What a tap on a given page does. Hit testing returns one of these rather than
// an index, so the same switch reads the same on every page.
typedef enum
{
	HIT_NONE = -1,
	HIT_PLAY,
	HIT_OPTIONS,
	HIT_QUIT,
	HIT_MINUS,
	HIT_PLUS,
	HIT_CONTROLS,
	HIT_BACK,
	HIT_PREV,
	HIT_NEXT,
	// The ten grid cells are HIT_CELL0 + 0 .. + 9. Kept last so "is this a level
	// button" is one comparison rather than a range, and so adding a plain button
	// above never renumbers them.
	HIT_CELL0,
} titleHit;

typedef struct { titleHit id; const rect* r; } hitZone;

static const hitZone zonesTitle[] =
{
	{ HIT_PLAY,     &rPlay    },
	{ HIT_OPTIONS,  &rOptions },
	{ HIT_QUIT,     &rQuit    },
};

static const hitZone zonesOptions[] =
{
	{ HIT_MINUS,    &rMinus    },
	{ HIT_PLUS,     &rPlus     },
	{ HIT_CONTROLS, &rControls },
	{ HIT_BACK,     &rOptBack  },
};

static const hitZone zonesControls[] =
{
	{ HIT_BACK,     &rCtlBack  },
};

static const hitZone zonesLevels[] =
{
	{ HIT_CELL0 + 0, &rLevel[0] }, { HIT_CELL0 + 1, &rLevel[1] },
	{ HIT_CELL0 + 2, &rLevel[2] }, { HIT_CELL0 + 3, &rLevel[3] },
	{ HIT_CELL0 + 4, &rLevel[4] }, { HIT_CELL0 + 5, &rLevel[5] },
	{ HIT_CELL0 + 6, &rLevel[6] }, { HIT_CELL0 + 7, &rLevel[7] },
	{ HIT_CELL0 + 8, &rLevel[8] }, { HIT_CELL0 + 9, &rLevel[9] },
	{ HIT_PREV,      &rPrev     },
	{ HIT_NEXT,      &rNext     },
	{ HIT_BACK,      &rLevBack  },
};

// How many parts each level's kit is meant to hold. The curve doubles every
// three or four levels and sits still for one after each jump, so a new size
// always gets a level to itself before the next one arrives. It never steps
// backwards: a level that asked for fewer parts than the one before it would
// read as a bug rather than a breather. Level 20's sixty parts is what the box
// budget in mesh.h was sized for.
static const unsigned char levelParts[LEVEL_COUNT] =
{
	 1,  2,  3,  4,  5,
	 7,  8, 10, 12, 15,
	15, 18, 20, 20, 25,
	30, 35, 40, 50, 60,
};

static titlePageId page   = PAGE_TITLE;
static bool        active = true;      // the game boots into the front end
static titleHit    held   = HIT_NONE;  // what the stylus went down on, if anything

static int levelPage   = 0;   // which half of the twenty the grid is showing
static int chosenLevel = 1;   // the level the player tapped, 1-based

// Frames the front end sits deaf for when it comes up.
//
// This went in chasing a boot that appeared to start the game on its own. It
// did not: instrumenting the touch reported one real press, thirteen seconds
// after launch, squarely inside Play. So this guard has never actually caught
// anything, and the bug it was written for does not exist.
//
// It stays because the risk it covers is real even if it has not fired here. A
// console handing control back from the HOME menu can report the stylus already
// down on the first frames, which libctru reads as a fresh press - that would
// land on Play and start the game before the player had touched anything. Half
// a second of deafness at launch costs nothing, and no genuine tap can happen
// that fast after a cold start.
#define TITLE_SETTLE_FRAMES 30

static int  settle = TITLE_SETTLE_FRAMES;
static bool seenUp = false;   // the glass has been seen clear at least once

// What the top-screen console was last printed for. The console is a
// per-character blit, so it is only redrawn when one of these has moved.
static int printedPage      = -1;
static int printedVolume    = -1;
static int printedLevelPage = -1;

// Static strings, parsed once. The percentage is the only thing that changes,
// so it gets a buffer of its own that is cleared every frame.
static C2D_TextBuf staticBuf;
static C2D_TextBuf dynBuf;

static C2D_Text txtTitle, txtTagline, txtPlay, txtOptions, txtQuit;
static C2D_Text txtOptHdr, txtVolume, txtMinus, txtPlus, txtControls, txtBack;
static C2D_Text txtNoSound;
static C2D_Text txtCtlHdr;
static C2D_Text txtKey[8], txtAction[8];
static C2D_Text txtLevHdr, txtPrev, txtNext;
static C2D_Text txtNum[LEVEL_COUNT];

// ---------------------------------------------------------------------------

static void parse(C2D_Text* t, const char* s)
{
	C2D_TextParse(t, staticBuf, s);
	C2D_TextOptimize(t);
}

void titleInit(void)
{
	staticBuf = C2D_TextBufNew(1024);
	dynBuf    = C2D_TextBufNew(32);

	parse(&txtTitle,    "MODEL KIT");
	parse(&txtTagline,  "snip  -  file  -  click it together");
	parse(&txtPlay,     "Play");
	parse(&txtOptions,  "Options");
	parse(&txtQuit,     "Quit");
	parse(&txtOptHdr,   "Options");
	parse(&txtVolume,   "Master volume");
	parse(&txtMinus,    "-");
	parse(&txtPlus,     "+");
	parse(&txtControls, "Controls");
	parse(&txtBack,     "Back");
	// One line, not two: the gap between the volume row and the Controls button
	// is a single line of 0.4-scale text tall, and a second one lands on the
	// button. At roughly 5 px a character this is 260 of the 320 px across.
	parse(&txtNoSound,  "No sound yet. This sets the level for when there is.");
	parse(&txtCtlHdr,   "Controls");
	parse(&txtLevHdr,   "Choose a level");
	parse(&txtPrev,     "<");
	parse(&txtNext,     ">");

	// The twenty level numbers are fixed text, so they are parsed once here
	// rather than rebuilt into the dynamic buffer every frame.
	for (int i = 0; i < LEVEL_COUNT; i++)
	{
		char n[4];
		snprintf(n, sizeof(n), "%d", i + 1);
		parse(&txtNum[i], n);
	}

	// The control list is capped at eight lines, which is what txtKey holds.
	const controlLine* lines = controlList();
	int count = controlCount();
	if (count > 8) count = 8;
	for (int i = 0; i < count; i++)
	{
		parse(&txtKey[i],    lines[i].key);
		parse(&txtAction[i], lines[i].action);
	}
}

void titleExit(void)
{
	C2D_TextBufDelete(dynBuf);
	C2D_TextBufDelete(staticBuf);
}

bool titleActive(void)
{
	return active;
}

void titleReturnToLevels(void)
{
	page   = PAGE_LEVELS;
	active = true;
	held   = HIT_NONE;

	// The grid comes back showing the half of the twenty the level came from,
	// so leaving level 14 puts the player back looking at level 14.
	levelPage = (chosenLevel - 1) / LEVELS_PER_PAGE;

	// The same deafness a cold boot gets. What leaves a level is a button, not
	// the stylus - but the stylus may well be resting on the glass at the time,
	// and it must not arm a tile on the frame the grid appears.
	settle = TITLE_SETTLE_FRAMES;
	seenUp = false;

	// The console belongs to the workbench right now, so the front end has to be
	// told its last print is stale or it will leave the bench readout up there.
	printedPage = -1;
}

int titleLevel(void)
{
	return chosenLevel;
}

int titleLevelParts(void)
{
	return levelParts[chosenLevel - 1];
}

void titleCaptureStartLevel(int level)
{
	if (level < 1) level = 1;
	if (level > LEVEL_COUNT) level = LEVEL_COUNT;
	chosenLevel = level;
	active = false;
}

// ---------------------------------------------------------------------------
// Input

static const hitZone* zonesFor(int* count)
{
	switch (page)
	{
		case PAGE_LEVELS:
			*count = (int)(sizeof(zonesLevels) / sizeof(zonesLevels[0]));
			return zonesLevels;
		case PAGE_OPTIONS:
			*count = (int)(sizeof(zonesOptions) / sizeof(zonesOptions[0]));
			return zonesOptions;
		case PAGE_CONTROLS:
			*count = (int)(sizeof(zonesControls) / sizeof(zonesControls[0]));
			return zonesControls;
		default:
			*count = (int)(sizeof(zonesTitle) / sizeof(zonesTitle[0]));
			return zonesTitle;
	}
}

static bool inside(const rect* r, float x, float y)
{
	return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static titleHit hitTest(float x, float y)
{
	int count;
	const hitZone* zones = zonesFor(&count);
	for (int i = 0; i < count; i++)
		if (inside(zones[i].r, x, y)) return zones[i].id;
	return HIT_NONE;
}

static const rect* rectFor(titleHit id)
{
	int count;
	const hitZone* zones = zonesFor(&count);
	for (int i = 0; i < count; i++)
		if (zones[i].id == id) return zones[i].r;
	return NULL;
}

titleAction titleInput(u32 kDown, u32 kHeld, u32 kUp)
{
	if (!active) return TITLE_STAY;

	// Two things have to happen before the front end will look at the stylus at
	// all: the settle window has to run out, and the glass has to have been seen
	// clear on one frame. A touch that was already down when the game started
	// can satisfy neither, so it cannot arm a button, let alone fire one.
	if (settle > 0)
	{
		settle--;
		held = HIT_NONE;
		return TITLE_STAY;
	}

	if (!seenUp)
	{
		if (kHeld & KEY_TOUCH) return TITLE_STAY;
		seenUp = true;
	}

	touchPosition touch;

	// A button arms on touch-down and fires on release, so a stylus that lands
	// on the wrong thing can be slid off it and let go harmlessly.
	if (kDown & KEY_TOUCH)
	{
		hidTouchRead(&touch);
		held = hitTest((float)touch.px, (float)touch.py);
		return TITLE_STAY;
	}

	if (kHeld & KEY_TOUCH)
	{
		if (held != HIT_NONE)
		{
			hidTouchRead(&touch);
			const rect* r = rectFor(held);
			if (r && !inside(r, (float)touch.px, (float)touch.py)) held = HIT_NONE;
		}
		return TITLE_STAY;
	}

	if (!(kUp & KEY_TOUCH)) return TITLE_STAY;

	// Release. The coordinates reported on the release frame are unreliable on
	// hardware, so the armed button is what counts, not where the stylus is now.
	titleHit fired = held;
	held = HIT_NONE;

	// A grid cell is the only thing that actually starts the game. Which level it
	// is depends on the page the grid is showing, so it is worked out here rather
	// than baked into the zone table.
	if (fired >= HIT_CELL0)
	{
		chosenLevel = levelPage * LEVELS_PER_PAGE + (fired - HIT_CELL0) + 1;
		active = false;
		return TITLE_PLAY;
	}

	switch (fired)
	{
		case HIT_PLAY:     page = PAGE_LEVELS;   break;

		case HIT_QUIT:
			return TITLE_QUIT;

		case HIT_OPTIONS:  page = PAGE_OPTIONS;  break;
		case HIT_CONTROLS: page = PAGE_CONTROLS; break;

		case HIT_PREV:
			if (levelPage > 0) levelPage--;
			break;

		case HIT_NEXT:
			if (levelPage < LEVEL_PAGES - 1) levelPage++;
			break;

		case HIT_BACK:
			page = (page == PAGE_CONTROLS) ? PAGE_OPTIONS : PAGE_TITLE;
			break;

		case HIT_MINUS: settingsVolumeStep(-VOLUME_STEP); break;
		case HIT_PLUS:  settingsVolumeStep( VOLUME_STEP); break;

		default: break;
	}

	return TITLE_STAY;
}

// ---------------------------------------------------------------------------
// The top screen, while the front end is up
//
// The console belongs to whichever section is on the glass. While the menu is
// up it is the menu's, and it says what the open page is for and nothing else -
// no manual pages, no part names, nothing at all from the bench. The workbench
// takes it over when Play is tapped, and not a frame before.

void titlePrintTop(bool isNew3DS)
{
	if (!active) return;
	if ((int)page == printedPage && settingsVolume() == printedVolume
		&& levelPage == printedLevelPage) return;
	printedPage      = (int)page;
	printedVolume    = settingsVolume();
	printedLevelPage = levelPage;

	// The rule line is exactly the console's 50 columns and so needs no newline
	// of its own - it wraps onto the next row by itself.
	printf("\x1b[2J\x1b[1;1H");

	switch (page)
	{
		case PAGE_LEVELS:
		{
			const int first = levelPage * LEVELS_PER_PAGE;
			printf("Model Kit  -  levels %d to %d\n",
				first + 1, first + LEVELS_PER_PAGE);
			printf("--------------------------------------------------");
			for (int i = 0; i < LEVELS_PER_PAGE; i++)
				printf("  Level %2d   %2d parts\n",
					first + i + 1, levelParts[first + i]);
			printf("\n");
			printf("Tap a number to start it. Every level opens the\n");
			printf("same ten-part kit for now - the part counts above\n");
			printf("are what each one is being built towards.\n");
			printf("\n");
			printf("< and > page through the twenty, Back returns.\n");
			break;
		}

		case PAGE_OPTIONS:
			printf("Model Kit  -  options\n");
			printf("--------------------------------------------------");
			printf("Master volume : %d%%\n", settingsVolume());
			printf("\n");
			printf("There is no sound in the game yet. This sets the\n");
			printf("level for when there is.\n");
			printf("\n");
			printf("Tap - and + to change it, Controls for the button\n");
			printf("list, or Back to return to the menu.\n");
			break;

		case PAGE_CONTROLS:
		{
			printf("Model Kit  -  controls\n");
			printf("--------------------------------------------------");
			const controlLine* lines = controlList();
			const int count = controlCount();
			for (int i = 0; i < count; i++)
				printf("  %-18s %s\n", lines[i].key, lines[i].action);
			printf("\n");
			printf("These work on the workbench, once you tap Play.\n");
			break;
		}

		default:
			printf("Model Kit\n");
			printf("--------------------------------------------------");
			printf("A model kit, a cutting mat and a pair of nippers.\n");
			printf("Snip the parts off the runner, file the nubs\n");
			printf("smooth, and click them together on the stand.\n");
			printf("\n");
			printf("Hardware : %s\n",
				isNew3DS ? "New 3DS / New 2DS XL" : "Original 3DS / 2DS");
			printf("\n");
			printf("Use the stylus on the bottom screen.\n");
			printf("\n");
			printf("  Play     choose a level and start building\n");
			printf("  Options  master volume, and the controls\n");
			printf("  Quit     close the game\n");
			break;
	}
}

// ---------------------------------------------------------------------------
// Drawing

static void drawTextAt(const C2D_Text* t, float x, float y, float scale, u32 clr)
{
	C2D_DrawText(t, C2D_WithColor, x, y, 0.5f, scale, scale, clr);
}

static void drawTextCentred(const C2D_Text* t, const rect* r, float scale, u32 clr)
{
	float w, h;
	C2D_TextGetDimensions(t, scale, scale, &w, &h);
	drawTextAt(t, r->x + (r->w - w) * 0.5f, r->y + (r->h - h) * 0.5f, scale, clr);
}

// Everything on this screen is a moulded part seen from directly above, so it
// is drawn the way one looks: a flat face of styrene with a slightly darker
// edge all the way round where the mould line runs. Pressing a part sinks it
// into the mat - the face darkens and a shadow appears along the inside of its
// top edge, which is what a pressed key looks like from overhead.
#define PART_EDGE  2.0f
#define PART_TAB   8.0f
#define GATE_W    16.0f
#define GATE_H    10.0f
#define RAIL_X    12.0f
#define RAIL_W    12.0f
#define MAT_GRID  20.0f
#define CHIP      10.0f

static void drawFrameRect(const rect* r, float t, u32 clr)
{
	C2D_DrawRectSolid(r->x,            r->y,            0.0f, r->w, t,    clr);
	C2D_DrawRectSolid(r->x,            r->y + r->h - t, 0.0f, r->w, t,    clr);
	C2D_DrawRectSolid(r->x,            r->y,            0.0f, t,    r->h, clr);
	C2D_DrawRectSolid(r->x + r->w - t, r->y,            0.0f, t,    r->h, clr);
}

// The mat, and the rule printed on it. The grid is the thing that makes the
// background read as a cutting mat rather than as a green screen, and it is the
// only thing here that is not a piece of the kit.
static void drawMat(void)
{
	for (float x = MAT_GRID; x < SCREEN_W; x += MAT_GRID)
		C2D_DrawRectSolid(x, 0.0f, 0.0f, 1.0f, SCREEN_H, CLR_MAT_GRID);
	for (float y = MAT_GRID; y < SCREEN_H; y += MAT_GRID)
		C2D_DrawRectSolid(0.0f, y, 0.0f, SCREEN_W, 1.0f, CLR_MAT_GRID);

	const rect edge = { 0.0f, 0.0f, SCREEN_W, SCREEN_H };
	drawFrameRect(&edge, 3.0f, CLR_MAT_EDGE);
}

static bool pressedOn(const rect* r)
{
	return held != HIT_NONE && rectFor(held) == r;
}

static void drawStyrene(const rect* r, bool pressed)
{
	C2D_DrawRectSolid(r->x, r->y, 0.0f, r->w, r->h,
		pressed ? CLR_STY_DOWN : CLR_STYRENE);
	drawFrameRect(r, PART_EDGE, CLR_STY_EDGE);
	if (pressed)
		C2D_DrawRectSolid(r->x + PART_EDGE, r->y + PART_EDGE, 0.0f,
			r->w - PART_EDGE * 2.0f, 3.0f, CLR_STY_EDGE);
}

// A wide part with its label struck into it. The tab down the left edge is the
// colour code: green starts something, amber sets something, red leaves. The
// label is left-aligned off that tab rather than centred, because a column of
// labels that all begin in the same place is quicker to read than three that
// each begin somewhere else.
//
// A tab of 0 means no code - a part that is only ever itself.
static void drawPartButton(const rect* r, const C2D_Text* label, float scale, u32 tab)
{
	drawStyrene(r, pressedOn(r));

	float textX = r->x + 14.0f;
	if (tab)
	{
		C2D_DrawRectSolid(r->x + PART_EDGE, r->y + PART_EDGE, 0.0f,
			PART_TAB, r->h - PART_EDGE * 2.0f, tab);
		textX = r->x + PART_EDGE + PART_TAB + 12.0f;
	}

	float w, h;
	C2D_TextGetDimensions(label, scale, scale, &w, &h);
	drawTextAt(label, textX, r->y + (r->h - h) * 0.5f, scale, CLR_INK);
}

// A small square part - the pagers, and the volume's minus and plus. There is
// no room for a tab, so the label is centred. One that cannot go anywhere is
// left as bare runner: still there, visibly not a part yet.
static void drawSquareButton(const rect* r, const C2D_Text* label, float scale, bool live)
{
	if (!live)
	{
		C2D_DrawRectSolid(r->x, r->y, 0.0f, r->w, r->h, CLR_RUNNER);
		drawFrameRect(r, PART_EDGE, CLR_STY_EDGE);
		drawTextCentred(label, r, scale, CLR_INK_DIM);
		return;
	}
	drawStyrene(r, pressedOn(r));
	drawTextCentred(label, r, scale, CLR_INK);
}

static void drawPlaque(const rect* r)
{
	C2D_DrawRectSolid(r->x, r->y, 0.0f, r->w, r->h, CLR_STYRENE);
	drawFrameRect(r, PART_EDGE, CLR_STY_EDGE);
}

static void drawPlaqueLabel(const rect* r, const C2D_Text* label, float scale)
{
	drawPlaque(r);
	float w, h;
	C2D_TextGetDimensions(label, scale, scale, &w, &h);
	drawTextAt(label, r->x + 10.0f, r->y + (r->h - h) * 0.5f, scale, CLR_INK);
}

// A level is a part still attached to the runner: the same styrene, the number
// on its face, and a chip of the tier colour in the corner the way a real part
// carries its number stamped beside it.
static void drawLevelPart(const rect* r, const C2D_Text* num, u32 chip)
{
	drawStyrene(r, pressedOn(r));
	C2D_DrawRectSolid(r->x + PART_EDGE, r->y + PART_EDGE, 0.0f, CHIP, CHIP, chip);
	drawTextCentred(num, r, 1.0f, CLR_INK);
}

// The sprue those ten parts are still on. Every bar exactly fills a gutter, so
// each part touches the frame on at least two sides and the grid reads as one
// moulding rather than as ten loose tiles. Drawn before the parts, which sit on
// top of it and hide the overlap.
static void drawLevelRunner(void)
{
	static const float barX[6] = { 0.0f, 62.0f, 124.0f, 186.0f, 248.0f, 310.0f };
	static const float barY[3] = { 44.0f, 104.0f, 164.0f };

	for (int i = 0; i < 6; i++)
		C2D_DrawRectSolid(barX[i], barY[0], 0.0f, 10.0f, 128.0f, CLR_RUNNER);

	for (int i = 0; i < 3; i++)
	{
		C2D_DrawRectSolid(0.0f, barY[i], 0.0f, SCREEN_W, 8.0f, CLR_RUNNER);
		C2D_DrawRectSolid(0.0f, barY[i], 0.0f, SCREEN_W, 2.0f, CLR_RUN_LIP);
	}
}

// The tile strip doubles as a size scale, on the same three colours the buttons
// use: green for the kits a beginner can finish, amber for the middle of the
// twenty, red for the ones that fill the bench.
static u32 tierColour(int parts)
{
	if (parts <=  5) return CLR_ACC_GO;
	if (parts <= 15) return CLR_ACC_SET;
	return CLR_ACC_OUT;
}

static void drawTitlePage(void)
{
	drawMat();

	// The name and the tagline are set as one block and centred in the plaque
	// together, off the measured height of each, so the pair stays put whatever
	// the system font turns out to be.
	drawPlaque(&rPlaqueTitle);
	float nw, nh, gw, gh;
	C2D_TextGetDimensions(&txtTitle,   0.85f, 0.85f, &nw, &nh);
	C2D_TextGetDimensions(&txtTagline, 0.38f, 0.38f, &gw, &gh);
	const float top = rPlaqueTitle.y + (rPlaqueTitle.h - (nh + 3.0f + gh)) * 0.5f;
	drawTextAt(&txtTitle,   rPlaqueTitle.x + 12.0f, top,             0.85f, CLR_INK);
	drawTextAt(&txtTagline, rPlaqueTitle.x + 13.0f, top + nh + 3.0f, 0.38f, CLR_INK_DIM);

	// The rail the three buttons are still moulded onto, with a gate running out
	// to each one. It fills the empty strip left of the buttons, so the menu
	// arrives on a runner the same way the kit does.
	C2D_DrawRectSolid(RAIL_X, 62.0f, 0.0f, RAIL_W, 174.0f, CLR_RUNNER);
	C2D_DrawRectSolid(RAIL_X, 62.0f, 0.0f, 3.0f,   174.0f, CLR_RUN_LIP);

	static const rect* const hangers[3] = { &rPlay, &rOptions, &rQuit };
	for (int i = 0; i < 3; i++)
		C2D_DrawRectSolid(RAIL_X + RAIL_W,
			hangers[i]->y + (hangers[i]->h - GATE_H) * 0.5f,
			0.0f, GATE_W, GATE_H, CLR_RUNNER);

	drawPartButton(&rPlay,    &txtPlay,    0.85f, CLR_ACC_GO);
	drawPartButton(&rOptions, &txtOptions, 0.85f, CLR_ACC_SET);
	drawPartButton(&rQuit,    &txtQuit,    0.85f, CLR_ACC_OUT);
}

static void drawLevelsPage(void)
{
	const int first = levelPage * LEVELS_PER_PAGE;

	drawMat();
	drawPlaqueLabel(&rPlaquePage, &txtLevHdr, 0.55f);

	// Which ten are on screen, printed on the mat to the right of the plaque. It
	// is rebuilt every frame, so it goes in the dynamic buffer alongside the
	// volume percentage.
	C2D_TextBufClear(dynBuf);
	// Sized for the widest an int pair can print, not for the two strings this
	// actually produces, so the compiler can see it cannot truncate.
	char range[32];
	snprintf(range, sizeof(range), "%d - %d", first + 1, first + LEVELS_PER_PAGE);
	C2D_Text txtRange;
	C2D_TextParse(&txtRange, dynBuf, range);
	float rw, rh;
	C2D_TextGetDimensions(&txtRange, 0.45f, 0.45f, &rw, &rh);
	drawTextAt(&txtRange, SCREEN_W - 14.0f - rw,
		rPlaquePage.y + (rPlaquePage.h - rh) * 0.5f, 0.45f, CLR_TAG);

	drawLevelRunner();
	for (int i = 0; i < LEVELS_PER_PAGE; i++)
		drawLevelPart(&rLevel[i], &txtNum[first + i], tierColour(levelParts[first + i]));

	drawSquareButton(&rPrev, &txtPrev, 0.9f, levelPage > 0);
	drawPartButton(&rLevBack, &txtBack, 0.8f, CLR_ACC_OUT);
	drawSquareButton(&rNext, &txtNext, 0.9f, levelPage < LEVEL_PAGES - 1);
}

static void drawOptionsPage(void)
{
	drawMat();
	drawPlaqueLabel(&rPlaquePage, &txtOptHdr, 0.55f);

	drawTextAt(&txtVolume, 14.0f, 44.0f, 0.5f, CLR_TEXT);

	// The number is redrawn every frame, so it lives in its own buffer.
	C2D_TextBufClear(dynBuf);
	char pct[8];
	snprintf(pct, sizeof(pct), "%3d%%", settingsVolume());
	C2D_Text txtPct;
	C2D_TextParse(&txtPct, dynBuf, pct);
	drawTextAt(&txtPct, 244.0f, 44.0f, 0.5f, CLR_TEXT);

	drawSquareButton(&rMinus, &txtMinus, 0.9f, true);
	drawSquareButton(&rPlus,  &txtPlus,  0.9f, true);

	// The bar is cut into the mat rather than laid on top of it, so the lit
	// cells read as filling a channel instead of floating in mid air.
	C2D_DrawRectSolid(BAR_X - 3.0f, BAR_Y - 3.0f, 0.0f,
		BAR_W + 6.0f, BAR_H + 6.0f, CLR_MAT_EDGE);

	const float cell = BAR_W / VOLUME_CELLS;
	const int   lit  = settingsVolume() / VOLUME_STEP;
	for (int i = 0; i < VOLUME_CELLS; i++)
		C2D_DrawRectSolid(BAR_X + i * cell + 1.0f, BAR_Y, 0.0f, cell - 2.0f, BAR_H,
			i < lit ? CLR_BAR_ON : CLR_BAR_OFF);

	drawTextAt(&txtNoSound, 14.0f, 110.0f, 0.4f, CLR_DIM);

	drawPartButton(&rControls, &txtControls, 0.8f, CLR_ACC_SET);
	drawPartButton(&rOptBack,  &txtBack,     0.8f, CLR_ACC_OUT);

	// This is a live allocator reading against the retail Old 3DS app budget,
	// not an emulator system-memory number. Keep it in its own plaque so it is
	// visible in the bottom-right without competing with the Options controls.
	drawPlaque(&rRam);
	C2D_TextBufClear(dynBuf);
	char ram[16];
	snprintf(ram, sizeof(ram), "RAM %u%%", memoryStatusAppPercent());
	C2D_Text txtRam;
	C2D_TextParse(&txtRam, dynBuf, ram);
	float rw, rh;
	C2D_TextGetDimensions(&txtRam, 0.36f, 0.36f, &rw, &rh);
	drawTextAt(&txtRam, rRam.x + (rRam.w - rw) * 0.5f,
		rRam.y + (rRam.h - rh) * 0.5f, 0.36f, CLR_INK);
}

static void drawControlsPage(void)
{
	drawMat();
	drawPlaqueLabel(&rPlaquePage, &txtCtlHdr, 0.55f);

	// The list is laid on a sheet of styrene, the way the instruction leaflet
	// sits on the bench: text this small needs a flat light ground to stay
	// legible against the mat's grid.
	static const rect sheet = { 8.0f, 40.0f, 304.0f, 140.0f };
	drawPlaque(&sheet);

	int count = controlCount();
	if (count > 8) count = 8;
	for (int i = 0; i < count; i++)
	{
		const float y = 48.0f + i * 16.0f;
		drawTextAt(&txtKey[i],     20.0f, y, 0.4f, CLR_INK_DIM);
		drawTextAt(&txtAction[i], 118.0f, y, 0.4f, CLR_INK);
	}

	drawPartButton(&rCtlBack, &txtBack, 0.8f, CLR_ACC_OUT);
}

void titleDraw(C3D_RenderTarget* target)
{
	C2D_TargetClear(target, CLR_BACK);
	C2D_Prepare();
	C2D_SceneBegin(target);

	switch (page)
	{
		case PAGE_LEVELS:   drawLevelsPage();   break;
		case PAGE_OPTIONS:  drawOptionsPage();  break;
		case PAGE_CONTROLS: drawControlsPage(); break;
		default:            drawTitlePage();    break;
	}

	C2D_Flush();
}
