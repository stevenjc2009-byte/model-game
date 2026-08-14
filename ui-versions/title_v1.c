#include <stdio.h>
#include <citro2d.h>

#include "title.h"
#include "settings.h"
#include "controls.h"

// ---------------------------------------------------------------------------
// Layout
//
// Bottom screen, 320x240. Every button is at least 48 px tall because that is
// the smallest thing a thumb-held stylus hits reliably on a 3DS.

typedef struct { float x, y, w, h; } rect;

#define SCREEN_W 320.0f

// The two header bands the pages hang under: a deep one on the title page,
// which carries the name and the tagline under it, and a shallow one on the
// pages that only need a label.
#define HDR_TITLE_H 60.0f
#define HDR_PAGE_H  40.0f

// Title page
static const rect rPlay    = {  40.0f,  68.0f, 240.0f, 48.0f };
static const rect rOptions = {  40.0f, 124.0f, 240.0f, 48.0f };
static const rect rQuit    = {  40.0f, 180.0f, 240.0f, 48.0f };

// Options page
static const rect rMinus    = {  12.0f,  62.0f,  48.0f, 48.0f };
static const rect rPlus     = { 260.0f,  62.0f,  48.0f, 48.0f };
static const rect rControls = {  40.0f, 126.0f, 240.0f, 48.0f };
static const rect rOptBack  = {  40.0f, 184.0f, 240.0f, 48.0f };

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

#define CLR_BACK    C2D_Color32(0x24, 0x21, 0x1E, 0xFF)
#define CLR_SHADOW  C2D_Color32(0x14, 0x12, 0x10, 0xFF)
#define CLR_HEADER  C2D_Color32(0x4F, 0x7C, 0x3D, 0xFF)
#define CLR_HDR_LIP C2D_Color32(0x36, 0x56, 0x29, 0xFF)
#define CLR_BUTTON  C2D_Color32(0x6E, 0x64, 0x58, 0xFF)
#define CLR_BTN_LIP C2D_Color32(0x8A, 0x7F, 0x70, 0xFF)
#define CLR_PRESSED C2D_Color32(0x9B, 0x6F, 0x47, 0xFF)
#define CLR_PRS_LIP C2D_Color32(0xB6, 0x88, 0x5C, 0xFF)
#define CLR_BAR_ON  C2D_Color32(0x4F, 0x7C, 0x3D, 0xFF)
#define CLR_BAR_OFF C2D_Color32(0x45, 0x3F, 0x38, 0xFF)
#define CLR_TEXT    C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF)
#define CLR_DIM     C2D_Color32(0xB8, 0xB0, 0xA4, 0xFF)
#define CLR_TAG     C2D_Color32(0xD8, 0xE6, 0xC8, 0xFF)

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

// Left-aligned, centred down a band of a given height. The font's line height
// is measured rather than assumed, so this sits right whatever the system font
// turns out to be.
static void drawTextLeftMid(const C2D_Text* t, float x, float bandH, float scale, u32 clr)
{
	float w, h;
	C2D_TextGetDimensions(t, scale, scale, &w, &h);
	drawTextAt(t, x, (bandH - h) * 0.5f, scale, clr);
}

// A button is a card: a hard shadow behind it, the body, and a lighter lip
// along the top edge. Pressing it drops the card onto its shadow, and that
// travel is the whole of the press feedback - it reads instantly at 60 fps and
// costs two rectangles.
#define CARD_LIFT   3.0f
#define CARD_LIP    2.0f
#define CARD_ACCENT 4.0f
#define TILE_STRIP  5.0f

static void drawCard(const rect* r, bool pressed, rect* face)
{
	if (!pressed)
		C2D_DrawRectSolid(r->x + CARD_LIFT, r->y + CARD_LIFT, 0.0f, r->w, r->h, CLR_SHADOW);

	face->x = pressed ? r->x + CARD_LIFT : r->x;
	face->y = pressed ? r->y + CARD_LIFT : r->y;
	face->w = r->w;
	face->h = r->h;

	C2D_DrawRectSolid(face->x, face->y, 0.0f, face->w, face->h,
		pressed ? CLR_PRESSED : CLR_BUTTON);
	C2D_DrawRectSolid(face->x, face->y, 0.0f, face->w, CARD_LIP,
		pressed ? CLR_PRS_LIP : CLR_BTN_LIP);
}

// accent of 0 means no bar - the plain buttons on the Options and Controls
// pages, where a colour would be saying something that is not true.
static void drawButton(const rect* r, const C2D_Text* label, float scale, u32 accent)
{
	rect face;
	drawCard(r, held != HIT_NONE && rectFor(held) == r, &face);
	if (accent) C2D_DrawRectSolid(face.x, face.y, 0.0f, CARD_ACCENT, face.h, accent);
	drawTextCentred(label, &face, scale, CLR_TEXT);
}

// A level tile: the same card with a colour strip across the top instead of a
// bar down the side, because a grid of tiles reads as a grid rather than as a
// stack of rows.
static void drawTile(const rect* r, const C2D_Text* num, u32 strip)
{
	rect face;
	drawCard(r, held != HIT_NONE && rectFor(held) == r, &face);
	C2D_DrawRectSolid(face.x, face.y, 0.0f, face.w, TILE_STRIP, strip);

	// Centred under the strip rather than in the whole tile, or the number
	// looks like it has slipped down inside its own box.
	const rect inner = { face.x, face.y + TILE_STRIP, face.w, face.h - TILE_STRIP };
	drawTextCentred(num, &inner, 0.9f, CLR_TEXT);
}

// A pager that cannot go anywhere is drawn flat and unlit, so it reads as part
// of the background rather than as a button that does nothing when tapped.
static void drawPager(const rect* r, const C2D_Text* label, bool live)
{
	if (!live)
	{
		C2D_DrawRectSolid(r->x, r->y, 0.0f, r->w, r->h, CLR_BAR_OFF);
		drawTextCentred(label, r, 0.9f, CLR_DIM);
		return;
	}
	drawButton(r, label, 0.9f, 0);
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

static void drawBand(float height)
{
	C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, SCREEN_W, height, CLR_HEADER);
	C2D_DrawRectSolid(0.0f, height - 3.0f, 0.0f, SCREEN_W, 3.0f, CLR_HDR_LIP);
}

static void drawHeader(const C2D_Text* label, float height, float scale)
{
	drawBand(height);
	drawTextLeftMid(label, 12.0f, height - 3.0f, scale, CLR_TEXT);
}

static void drawTitlePage(void)
{
	drawBand(HDR_TITLE_H);

	// The name and the tagline are set as one block and centred in the band
	// together, off the measured height of each, so the pair stays put whatever
	// the font does.
	float nw, nh, gw, gh;
	C2D_TextGetDimensions(&txtTitle,   1.0f,  1.0f,  &nw, &nh);
	C2D_TextGetDimensions(&txtTagline, 0.42f, 0.42f, &gw, &gh);
	const float top = ((HDR_TITLE_H - 3.0f) - (nh + 2.0f + gh)) * 0.5f;
	drawTextAt(&txtTitle,   14.0f, top,                 1.0f,  CLR_TEXT);
	drawTextAt(&txtTagline, 16.0f, top + nh + 2.0f,     0.42f, CLR_TAG);

	drawButton(&rPlay,    &txtPlay,    0.85f, CLR_ACC_GO);
	drawButton(&rOptions, &txtOptions, 0.85f, CLR_ACC_SET);
	drawButton(&rQuit,    &txtQuit,    0.85f, CLR_ACC_OUT);
}

static void drawLevelsPage(void)
{
	const int first = levelPage * LEVELS_PER_PAGE;

	drawBand(HDR_PAGE_H);
	drawTextLeftMid(&txtLevHdr, 12.0f, HDR_PAGE_H - 3.0f, 0.7f, CLR_TEXT);

	// Which ten are on screen, right-aligned in the band. It is rebuilt every
	// frame, so it goes in the dynamic buffer alongside the volume percentage.
	C2D_TextBufClear(dynBuf);
	// Sized for the widest an int pair can print, not for the two strings this
	// actually produces, so the compiler can see it cannot truncate.
	char range[32];
	snprintf(range, sizeof(range), "%d - %d", first + 1, first + LEVELS_PER_PAGE);
	C2D_Text txtRange;
	C2D_TextParse(&txtRange, dynBuf, range);
	float rw, rh;
	C2D_TextGetDimensions(&txtRange, 0.45f, 0.45f, &rw, &rh);
	drawTextLeftMid(&txtRange, SCREEN_W - 12.0f - rw, HDR_PAGE_H - 3.0f, 0.45f, CLR_TAG);

	for (int i = 0; i < LEVELS_PER_PAGE; i++)
		drawTile(&rLevel[i], &txtNum[first + i], tierColour(levelParts[first + i]));

	drawPager(&rPrev, &txtPrev, levelPage > 0);
	drawButton(&rLevBack, &txtBack, 0.8f, CLR_ACC_OUT);
	drawPager(&rNext, &txtNext, levelPage < LEVEL_PAGES - 1);
}

static void drawOptionsPage(void)
{
	drawHeader(&txtOptHdr, 34.0f, 0.7f);

	drawTextAt(&txtVolume, 12.0f, 40.0f, 0.5f, CLR_TEXT);

	// The number is redrawn every frame, so it lives in its own buffer.
	C2D_TextBufClear(dynBuf);
	char pct[8];
	snprintf(pct, sizeof(pct), "%3d%%", settingsVolume());
	C2D_Text txtPct;
	C2D_TextParse(&txtPct, dynBuf, pct);
	drawTextAt(&txtPct, 244.0f, 40.0f, 0.5f, CLR_TEXT);

	drawButton(&rMinus, &txtMinus, 0.9f, 0);
	drawButton(&rPlus,  &txtPlus,  0.9f, 0);

	const float cell = BAR_W / VOLUME_CELLS;
	const int   lit  = settingsVolume() / VOLUME_STEP;
	for (int i = 0; i < VOLUME_CELLS; i++)
		C2D_DrawRectSolid(BAR_X + i * cell + 1.0f, BAR_Y, 0.0f, cell - 2.0f, BAR_H,
			i < lit ? CLR_BAR_ON : CLR_BAR_OFF);

	drawTextAt(&txtNoSound, 12.0f, 110.0f, 0.4f, CLR_DIM);

	drawButton(&rControls, &txtControls, 0.8f, 0);
	drawButton(&rOptBack,  &txtBack,     0.8f, 0);
}

static void drawControlsPage(void)
{
	drawHeader(&txtCtlHdr, 30.0f, 0.6f);

	int count = controlCount();
	if (count > 8) count = 8;
	for (int i = 0; i < count; i++)
	{
		const float y = 38.0f + i * 19.0f;
		drawTextAt(&txtKey[i],     12.0f, y, 0.4f, CLR_DIM);
		drawTextAt(&txtAction[i], 112.0f, y, 0.4f, CLR_TEXT);
	}

	drawButton(&rCtlBack, &txtBack, 0.8f, 0);
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
