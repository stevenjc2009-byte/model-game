#include <stdio.h>
#include <citro2d.h>

#include "title.h"
#include "audio.h"
#include "mesh.h"
#include "settings.h"
#include "controls.h"
#include "strings.h"
#include "memory_status.h"
#include "updater.h"

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
//
// Controls used to have the middle row to itself at the full 240. Update needed
// somewhere to live and the page had no fourth row's worth of height left, so
// the two share that row instead: 116 each with an 8 px gutter, which is the
// same gutter the level grid uses and still clears the 48 px a stylus needs.
//
// Item 50 needed a fourth control (Language) and there was no row left to
// give it, so this reflows upward instead of touching rOptBack/rRam - those
// two keep the exact position they always had, since that is the
// best-tested part of this page. The volume buttons and the Controls/Update
// row each give up a few px of height (48->44, 48->40) to make room for a
// slim Language row between them and Back/Ram; both are still well clear of
// a stylus tap, just no longer the full 48 the rest of this file uses.
static const rect rMinus    = {  12.0f,  56.0f,  48.0f, 44.0f };
static const rect rPlus     = { 260.0f,  56.0f,  48.0f, 44.0f };
static const rect rControls = {  40.0f, 108.0f, 116.0f, 40.0f };
static const rect rUpdate   = { 164.0f, 108.0f, 116.0f, 40.0f };
static const rect rLanguage = {  40.0f, 152.0f, 240.0f, 28.0f };
static const rect rOptBack  = {  40.0f, 184.0f, 168.0f, 48.0f };
static const rect rRam      = { 216.0f, 184.0f,  92.0f, 48.0f };

// Update page
//
// One wide button that changes what it says with the state of the update, and a
// Back beside it. While a download is running neither is offered - there is no
// safe way to leave a half-written title behind - so the row draws as a single
// status plaque instead.
static const rect rUpdAction = {  40.0f, 184.0f, 168.0f, 48.0f };
static const rect rUpdBack   = { 216.0f, 184.0f,  92.0f, 48.0f };

// The progress channel, cut into the mat like the volume bar above it.
#define UPD_BAR_X  20.0f
#define UPD_BAR_Y 150.0f
#define UPD_BAR_W 280.0f
#define UPD_BAR_H  14.0f

// The memory plaque carries two rows in those 48 px. This is as large as the
// wider row ("VRAM 100%") can go and still clear the plaque's side margins.
#define MEM_TEXT_SCALE 0.5f
#define MEM_ROW_GAP    2.0f

// Controls page
//
// Item 37 turned this into the live remap screen (steve's own instruction:
// make it work like Minecraft's control list, not a separate menu bolted on
// beside it), which needed a Reset-to-defaults control the read-only page
// never did. Split the same way the Update page already splits its action
// button from Back: rCtlBack narrows from the full 240 and rCtlReset takes
// the space that opens up on the right.
static const rect rCtlBack  = {  40.0f, 188.0f, 168.0f, 48.0f };
static const rect rCtlReset = { 216.0f, 188.0f,  92.0f, 48.0f };

// Level select page
//
// Ten levels to a page in a 5x2 grid. 54 px tiles on a 62 px pitch is the
// widest grid that still leaves an 8 px gutter between neighbours, so a tap
// that lands between two tiles picks neither rather than the wrong one - and
// every tile clears the 48 px a stylus needs with room to spare. Two pages
// cover the twenty levels.
// The grid shows every level the kit tables define; mesh.h owns that number.
#define LEVEL_COUNT      MESH_KIT_LEVELS
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

typedef enum { PAGE_TITLE, PAGE_LEVELS, PAGE_OPTIONS, PAGE_CONTROLS, PAGE_UPDATE } titlePageId;

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
	HIT_UPDATE,
	HIT_LANGUAGE,
	HIT_UPD_ACTION,
	HIT_BACK,
	HIT_RESET,
	HIT_PREV,
	HIT_NEXT,
	// A tap on one of the Controls page's twelve rows - see zonesControls and
	// titleInput. HIT_CTL_ROW0 + row for row 0..CONTROL_LINE_COUNT-1, the
	// same shape as HIT_CELL0 below, because which of the twelve is
	// remappable is a row NUMBER (6, 10, 11 - controls.c's actionForRow12),
	// not a fixed rect the way every other button here is.
	HIT_CTL_ROW0,
	// The ten grid cells are HIT_CELL0 + 0 .. + 9. Explicitly placed after
	// HIT_CTL_ROW0's own twelve slots rather than left to auto-increment, so
	// the two ranges cannot overlap.
	HIT_CELL0 = HIT_CTL_ROW0 + CONTROL_LINE_COUNT,
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
	{ HIT_UPDATE,   &rUpdate   },
	{ HIT_LANGUAGE, &rLanguage },
	{ HIT_BACK,     &rOptBack  },
};

static const hitZone zonesUpdate[] =
{
	{ HIT_UPD_ACTION, &rUpdAction },
	{ HIT_BACK,       &rUpdBack   },
};

// One rect per row of the Controls page, filled in by initControlRowRects()
// (called once from titleInit - the geometry is fixed, but a static const
// array cannot be built with a loop, and it has to match drawControlsPage's
// own y = 44 + i*11 exactly or a tap would land a row away from what it
// looks like it hit). Only rows 6, 10 and 11 do anything when tapped - see
// titleInput - the rest are here so the whole sheet still hit-tests
// correctly rather than only the three live rows.
static rect rCtlRow[CONTROL_LINE_COUNT];

#define CTLROW(n) { (titleHit)(HIT_CTL_ROW0 + (n)), &rCtlRow[n] }
static const hitZone zonesControls[] =
{
	CTLROW(0),  CTLROW(1),  CTLROW(2),  CTLROW(3),
	CTLROW(4),  CTLROW(5),  CTLROW(6),  CTLROW(7),
	CTLROW(8),  CTLROW(9),  CTLROW(10), CTLROW(11),
	{ HIT_BACK,  &rCtlBack  },
	{ HIT_RESET, &rCtlReset },
};
#undef CTLROW

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

// How many parts a level's kit holds now comes from meshKitPartCount(), so this
// file no longer has an opinion about it at all.
//
// It used to be a twenty-entry table running 1, 2, 3 ... 50, 60 - the sizes the
// kits were being built towards. Nothing kept it in step with the kits, so
// nineteen of the twenty levels advertised a count they did not have. Replacing
// it with a local constant stopped the lying but moved the same drift risk one
// step away: the constant and the kit tables were still two places holding one
// number. Asking mesh.c leaves only one.

static titlePageId page   = PAGE_TITLE;
static bool        active = true;      // the game boots into the front end
static titleHit    held   = HIT_NONE;  // what the stylus went down on, if anything
// Where on the glass `held` was armed. Only meaningful together with held !=
// HIT_NONE. Item 37's widened remap uses this to tell which half of a split
// Controls-page row (D-Pad L/R, L/R, Y/X - see controlsRowIsSplit) was
// tapped; nothing else reads it. Captured at arm time, not release - see
// titleInput's own comment on why release coordinates are not trusted.
static float        heldX  = 0.0f;

static int levelPage   = 0;   // which half of the twenty the grid is showing
static int chosenLevel = 1;   // the level the player tapped, 1-based

// Which kits have been finished. Pushed in by main.c off the save file - see
// titleSetBuilt - and read by both the grid and the console, so the tick on a
// tile and the count above it can never disagree.
static bool levelBuilt[LEVEL_COUNT];

void titleSetBuilt(int level, bool built)
{
	if (level < 1 || level > LEVEL_COUNT) return;
	levelBuilt[level - 1] = built;
}

static int builtTotal(void)
{
	int n = 0;
	for (int i = 0; i < LEVEL_COUNT; i++) if (levelBuilt[i]) n++;
	return n;
}

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
// Finishing a kit changes the levels list without changing the page, so the
// count has to be one of the things the console watches or the list would go on
// showing the tally from before the build was finished.
static int printedBuilt     = -1;

// The update page is the one page that changes on its own, without anything
// being tapped, so the reprint test has to watch the worker as well. Progress is
// compared in whole percent, which is what the console prints - a finer test
// would clear and redraw the screen dozens of times a second for no visible
// difference and a lot of flicker.
static int printedUpdState    = -1;
static int printedUpdProgress = -2;
// Item 50 - a language change redraws the console even though none of the
// state above has moved, or switching languages on the Options page would
// leave the console showing the old one until something else changed too.
static int printedLanguage = -1;

// Static strings, parsed once. The percentage is the only thing that changes,
// so it gets a buffer of its own that is cleared every frame.
static C2D_TextBuf staticBuf;
static C2D_TextBuf dynBuf;

static C2D_Text txtTitle, txtTagline, txtPlay, txtOptions, txtQuit;
static C2D_Text txtOptHdr, txtVolume, txtMinus, txtPlus, txtControls, txtBack;
static C2D_Text txtVolNote;
static C2D_Text txtCtlHdr;
static C2D_Text txtLanguage, txtReset;

// Update page. The button on the Options row is set in two lines because
// "Check for Update" will not fit across 116 px at any size worth reading.
static C2D_Text txtUpd1, txtUpd2, txtUpdHdr;
static C2D_Text txtUpdCheck, txtUpdGet, txtUpdRestart, txtUpdOff;
// The Controls page holds this many rows: 140 px of sheet at 16 px a line. The
// list in controls.c is exactly this long today, so a ninth entry added there
// would be parsed by neither loop below and simply not appear on the page.
// Taken from controls.h rather than written out again here - the two disagreeing
// is exactly what let a ninth control vanish off this page.
#define CONTROL_ROWS CONTROL_LINE_COUNT
static C2D_Text txtKey[CONTROL_ROWS], txtAction[CONTROL_ROWS];
// One per settingsRemapKey value (item 37's widened remap) - the button's
// own short name, independent of which row or action currently shows it.
// See controls.c's controlsKeyLabelId() and drawControlsPage() below.
static C2D_Text txtKeyLabel[REMAP_KEY_COUNT];
static C2D_Text txtLevHdr, txtPrev, txtNext;
static C2D_Text txtNum[LEVEL_COUNT];

// ---------------------------------------------------------------------------

static void parse(C2D_Text* t, const char* s)
{
	C2D_TextParse(t, staticBuf, s);
	C2D_TextOptimize(t);
}

static void parseId(C2D_Text* t, stringId id)
{
	parse(t, STR(id));
}

// Fills every static (parsed-once) C2D_Text this file draws, from the
// currently active language. Called once from titleInit, and again by
// titleSetLanguage() whenever the player changes language on the Options
// page - a C2D_TextBuf accumulates glyph data on every parse, so this clears
// it first rather than leaking a copy of the old language into it each time.
static void refreshStaticText(void)
{
	C2D_TextBufClear(staticBuf);

	parseId(&txtTitle,    STR_TITLE);
	parseId(&txtTagline,  STR_TAGLINE);
	parseId(&txtPlay,     STR_PLAY);
	parseId(&txtOptions,  STR_OPTIONS);
	parseId(&txtQuit,     STR_QUIT);
	parseId(&txtOptHdr,   STR_OPTIONS);
	parseId(&txtVolume,   STR_VOLUME);
	parseId(&txtMinus,    STR_MINUS);
	parseId(&txtPlus,     STR_PLUS);
	parseId(&txtControls, STR_CONTROLS);
	parseId(&txtBack,     STR_BACK);
	// One line, not two: the gap between the volume row and the Controls button
	// is a single line of 0.4-scale text tall, and a second one lands on the
	// button. At roughly 5 px a character this is 260 of the 320 px across.
	parseId(&txtVolNote,  STR_VOL_NOTE);
	parseId(&txtCtlHdr,   STR_CONTROLS);
	parseId(&txtLanguage, STR_LANGUAGE);
	parseId(&txtReset,    STR_RESET);
	parseId(&txtUpd1,     STR_UPD_LINE1);
	parseId(&txtUpd2,     STR_UPD_LINE2);
	parseId(&txtUpdHdr,   STR_UPD_LINE2);
	parseId(&txtUpdCheck, STR_UPD_CHECK);
	parseId(&txtUpdGet,   STR_UPD_GET);
	parseId(&txtUpdRestart, STR_UPD_RESTART);
	parseId(&txtUpdOff,   STR_UPD_OFF);
	parseId(&txtLevHdr,   STR_LEVELS_HDR);
	parseId(&txtPrev,     STR_PREV);
	parseId(&txtNext,     STR_NEXT);

	// The twenty level numbers are fixed text, so they are parsed once here
	// rather than rebuilt into the dynamic buffer every frame.
	for (int i = 0; i < LEVEL_COUNT; i++)
	{
		char n[4];
		snprintf(n, sizeof(n), "%d", i + 1);
		parse(&txtNum[i], n);
	}

	const controlLine* lines = controlList();
	int count = controlCount();
	if (count > CONTROL_ROWS) count = CONTROL_ROWS;
	for (int i = 0; i < count; i++)
	{
		parseId(&txtKey[i],    lines[i].key);
		parseId(&txtAction[i], lines[i].action);
	}

	for (int k = 0; k < REMAP_KEY_COUNT; k++)
		parseId(&txtKeyLabel[k], controlsKeyLabelId((settingsRemapKey)k));
}

// Row rects for the Controls page's 12-row list, matching drawControlsPage's
// own y = 44 + i*11 exactly - see rCtlRow's declaration above for why this
// has to be code rather than a second static table someone could edit only
// one copy of. Row width/x/height mirror the sheet drawControlsPage lays
// down; only y actually varies per row.
static void initControlRowRects(void)
{
	for (int i = 0; i < CONTROL_LINE_COUNT; i++)
	{
		rCtlRow[i].x = 16.0f;
		rCtlRow[i].y = 44.0f + i * 11.0f;
		rCtlRow[i].w = 288.0f;
		rCtlRow[i].h = 11.0f;
	}
}

void titleInit(void)
{
	staticBuf = C2D_TextBufNew(1024);
	// 32 was enough before items 37/50: the Controls page now also parses a
	// "B cancels" hint and a listening prompt into this buffer on the same
	// frame with no clear between them (drawControlsPage), which in French
	// runs close to 32 glyphs on its own, on top of what the Update and
	// Options pages already put through it. C2D_TextParse degrades safely
	// (it just stops adding glyphs - see c2d/text.h) rather than crashing,
	// but "safely" here means a silently truncated word, so this is sized
	// with real headroom instead of exactly to today's longest string.
	dynBuf    = C2D_TextBufNew(96);

	// languageCurrent() does the console-language detection (and, on a fresh
	// install, persists the result) the first time anything asks it for the
	// active language - which this is, so a first boot after this update
	// settles on a language right here rather than defaulting to English for
	// one frame and then jumping.
	languageCurrent();

	refreshStaticText();
	initControlRowRects();
}

// Item 50 - the Options page's Language row (titleInput's HIT_LANGUAGE case)
// calls this to cycle to the next language. Persists the new choice, then
// reparses every static string this file owns so the page in front of the
// player updates immediately rather than on the next visit - same reasoning
// as refreshStaticText's own header comment. Not in title.h: nothing outside
// this file needs to call it.
static void titleCycleLanguage(void)
{
	gameLanguage next = (gameLanguage)((languageCurrent() + 1) % LANG_COUNT);
	languageSet(next);
	refreshStaticText();
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
	return meshKitPartCount(chosenLevel);
}

void titleCaptureStartLevel(int level)
{
	if (level < 1) level = 1;
	if (level > LEVEL_COUNT) level = LEVEL_COUNT;
	chosenLevel = level;
	active = false;

	// Jumping straight into a level skips the grid that would normally have set
	// these. Nothing reads them while the front end is inactive, and the only way
	// back in - titleReturnToLevels - sets them itself, so this is belt and
	// braces: it keeps the two ways of entering a level leaving the same state.
	page      = PAGE_LEVELS;
	levelPage = (chosenLevel - 1) / LEVELS_PER_PAGE;
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
		case PAGE_UPDATE:
			// While the worker is mid-flight the page offers nothing to press.
			// Taking the zones away is how that is enforced, so there is one
			// answer to "can this be tapped" rather than a check in the handler
			// and another in the drawing code that can disagree.
			if (updaterBusy()) { *count = 0; return NULL; }
			*count = (int)(sizeof(zonesUpdate) / sizeof(zonesUpdate[0]));
			return zonesUpdate;
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

	// Item 37 - while a Controls-page row is listening, this captures the very
	// next physical A / SELECT / START (or B to cancel). It has to read the
	// raw press directly - see controls.h - not kDown/kHeld/kUp above, which
	// have already been through controlsTranslate() by the time they reach
	// here. Gated to the Controls page only, so a stray press elsewhere can
	// never be mistaken for a rebind.
	if (page == PAGE_CONTROLS) controlsPollListen();

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
		held  = hitTest((float)touch.px, (float)touch.py);
		heldX = (float)touch.px;
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

	// One click per button that actually fired. Placed here rather than on each
	// case below so a new button cannot be added without a sound, and so a
	// release that landed on nothing stays silent.
	if (fired != HIT_NONE) audioPlay(SND_UI);

	// A grid cell is the only thing that actually starts the game. Which level it
	// is depends on the page the grid is showing, so it is worked out here rather
	// than baked into the zone table.
	if (fired >= HIT_CELL0)
	{
		chosenLevel = levelPage * LEVELS_PER_PAGE + (fired - HIT_CELL0) + 1;
		active = false;
		return TITLE_PLAY;
	}

	// One of the Controls page's twelve rows. Nine of them (everything but
	// the three stylus rows) are now remappable - three of those nine (the
	// D-Pad L/R, L/R and Y/X rows) hold two independent actions rather than
	// one, so which half of the row the tap landed on (heldX against the
	// row's own midpoint) picks which - see controlsRowIsSplit. A tap on a
	// non-remappable row falls through and does nothing, the same as it
	// always has.
	if (fired >= HIT_CTL_ROW0 && fired < HIT_CELL0)
	{
		int row = fired - HIT_CTL_ROW0;
		bool rightHalf = controlsRowIsSplit(row) &&
			heldX >= (rCtlRow[row].x + rCtlRow[row].w * 0.5f);
		int action = controlsRowAction(row, rightHalf);
		if (action >= 0)
		{
			// Tapping the row that is already listening cancels it - the touch
			// equivalent of pressing B - rather than rebinding it to whatever
			// button the tap itself would translate to.
			if (controlsIsListening() && controlsListeningAction() == (settingsRemapAction)action)
				controlsCancelListen();
			else
				controlsBeginListen((settingsRemapAction)action);
		}
		return TITLE_STAY;
	}

	switch (fired)
	{
		case HIT_PLAY:     page = PAGE_LEVELS;   break;

		case HIT_QUIT:
			return TITLE_QUIT;

		case HIT_OPTIONS:  page = PAGE_OPTIONS;  break;
		case HIT_CONTROLS: page = PAGE_CONTROLS; break;

		case HIT_LANGUAGE:
			titleCycleLanguage();
			break;

		case HIT_RESET:
			settingsResetRemap();
			break;

		case HIT_UPDATE:
			page = PAGE_UPDATE;
			// Opening the page starts the check. The player asked the question by
			// tapping the button; making them tap a second one to actually ask it
			// would be a step that exists only because the code is in two parts.
			updaterStartCheck();
			break;

		case HIT_UPD_ACTION:
			switch (updaterState())
			{
				case UPDATE_AVAILABLE:
					updaterStartInstall();
					break;

				case UPDATE_DONE:
					// The new title is on the card; the only thing left is to go
					// and run it. main.c ends the loop, and the chainloader set
					// below brings the game straight back up.
					updaterRelaunch();
					active = false;
					return TITLE_RELAUNCH;

				default:
					// Idle, settled or failed - all of which mean "ask again".
					updaterStartCheck();
					break;
			}
			break;

		case HIT_PREV:
			if (levelPage > 0) levelPage--;
			break;

		case HIT_NEXT:
			if (levelPage < LEVEL_PAGES - 1) levelPage++;
			break;

		case HIT_BACK:
			// Leaving the Controls page mid-listen (a stylus tap on Back is a
			// perfectly valid way to do that) drops the listening state with
			// it - see controlsPollListen's own B handling for the other way
			// out. Otherwise a stale "listening" flag would sit around after
			// the row it belongs to is off screen, ready to catch the next
			// A/SELECT/START press anywhere else in the front end.
			if (page == PAGE_CONTROLS) controlsCancelListen();
			page = (page == PAGE_CONTROLS || page == PAGE_UPDATE)
			     ? PAGE_OPTIONS : PAGE_TITLE;
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
		&& levelPage == printedLevelPage && builtTotal() == printedBuilt
		&& (int)updaterState() == printedUpdState
		&& updaterProgress() == printedUpdProgress
		&& (int)languageCurrent() == printedLanguage) return;
	printedPage        = (int)page;
	printedVolume      = settingsVolume();
	printedLevelPage   = levelPage;
	printedBuilt       = builtTotal();
	printedUpdState    = (int)updaterState();
	printedUpdProgress = updaterProgress();
	printedLanguage    = (int)languageCurrent();

	// The rule line is exactly the console's 50 columns and so needs no newline
	// of its own - it wraps onto the next row by itself.
	printf("\x1b[2J\x1b[1;1H");

	switch (page)
	{
		case PAGE_LEVELS:
		{
			const int first = levelPage * LEVELS_PER_PAGE;
			printf(STR(STR_C_LEVELS_HDR), first + 1, first + LEVELS_PER_PAGE);
			printf("--------------------------------------------------");
			// The shelf: which of these ten are already on it, and the
			// running total underneath. A finished kit is called out in
			// words here rather than by a symbol, because the console has
			// the width for it and words survive being read at arm's
			// length on a 2DS better than a glyph does.
			for (int i = 0; i < LEVELS_PER_PAGE; i++)
				printf(STR(STR_C_LEVEL_ROW), first + i + 1,
					meshKitPartCount(first + i + 1),
					levelBuilt[first + i] ? STR(STR_BUILT_TAG) : "");
			printf("\n");
			printf(STR(STR_C_BUILT_TOTAL), builtTotal(), LEVEL_COUNT);
			printf("\n");
			printf("%s", STR(STR_C_LEVELS_HELP1));
			printf("\n");
			printf("%s", STR(STR_C_LEVELS_HELP2));
			break;
		}

		case PAGE_OPTIONS:
			printf("%s", STR(STR_C_OPTIONS_HDR));
			printf("--------------------------------------------------");
			printf(STR(STR_C_VOLUME_LINE), settingsVolume());
			printf("\n");
			printf("%s", STR(STR_C_OPTIONS_HELP1));
			printf("\n");
			printf("%s", STR(STR_C_OPTIONS_HELP2));
			printf("\n");
			printf("%s", STR(STR_C_OPTIONS_HELP3));
			break;

		case PAGE_UPDATE:
			printf("%s", STR(STR_C_UPDATE_HDR));
			printf("--------------------------------------------------");
			printf(STR(STR_C_INSTALLED_LINE),
				MODELKIT_VERSION_SET ? MODELKIT_VERSION : STR(STR_C_UPD_NOT_SET));
			if (updaterLatestVersion()[0] != '\0')
				printf(STR(STR_C_NEWEST_LINE), updaterLatestVersion());
			printf("\n");

			if (!updaterAvailable())
			{
				printf("%s", STR(STR_C_UPD_UNAVAILABLE1));
				printf("\n");
				printf("%s", STR(STR_C_UPD_UNAVAILABLE2));
				printf("%s", STR(STR_C_UPD_UNAVAILABLE3));
				break;
			}

			printf("%s\n", updaterMessage());
			printf("\n");

			if (updaterProgress() >= 0)
				printf(STR(STR_C_UPD_PROGRESS), updaterProgress());

			switch (updaterState())
			{
				case UPDATE_AVAILABLE:
					printf("%s", STR(STR_C_UPD_HELP_AVAILABLE1));
					printf("%s", STR(STR_C_UPD_HELP_AVAILABLE2));
					break;
				case UPDATE_DOWNLOADING:
				case UPDATE_INSTALLING:
					printf("%s", STR(STR_C_UPD_HELP_BUSY1));
					printf("%s", STR(STR_C_UPD_HELP_BUSY2));
					break;
				case UPDATE_DONE:
					printf("%s", STR(STR_C_UPD_HELP_DONE));
					break;
				case UPDATE_CHECKING:
					break;
				default:
					printf("%s", STR(STR_C_UPD_HELP_DEFAULT));
					break;
			}
			break;

		case PAGE_CONTROLS:
		{
			printf("%s", STR(STR_C_CONTROLS_HDR));
			printf("--------------------------------------------------");
			const controlLine* lines = controlList();
			const int count = controlCount();
			// Item 37's widened remap: this console mirror used to lean on
			// controlKeyRow's 12-row branch to reorder which row's fixed key
			// text printed where. That branch only serves main.c's separate
			// 8-row page now (see controls.c/controlKeyRow) - this file's
			// own console print asks controlsRowAction/controlsKeyLabelId
			// directly instead, the same source drawControlsPage uses.
			for (int i = 0; i < count; i++)
			{
				int leftAction  = controlsRowAction(i, false);
				int rightAction = controlsRowAction(i, true);
				if (leftAction < 0)
				{
					printf(STR(STR_C_CONTROLS_ROW), STR(lines[i].key), STR(lines[i].action));
				}
				else if (leftAction == rightAction)
				{
					settingsRemapKey key = settingsRemapButton((settingsRemapAction)leftAction);
					printf(STR(STR_C_CONTROLS_ROW), STR(controlsKeyLabelId(key)), STR(lines[i].action));
				}
				else
				{
					char buf[24];
					settingsRemapKey lKey = settingsRemapButton((settingsRemapAction)leftAction);
					settingsRemapKey rKey = settingsRemapButton((settingsRemapAction)rightAction);
					snprintf(buf, sizeof(buf), "%s / %s",
						STR(controlsKeyLabelId(lKey)), STR(controlsKeyLabelId(rKey)));
					printf(STR(STR_C_CONTROLS_ROW), buf, STR(lines[i].action));
				}
			}
			printf("\n");
			printf("%s", STR(STR_C_CONTROLS_HELP));
			break;
		}

		default:
			printf("%s", STR(STR_C_TITLE_HDR));
			printf("--------------------------------------------------");
			printf("%s", STR(STR_C_TITLE_HELP1));
			printf("%s", STR(STR_C_TITLE_HELP2));
			printf("%s", STR(STR_C_TITLE_HELP3));
			printf("\n");
			printf(STR(STR_C_HARDWARE_LINE),
				isNew3DS ? STR(STR_C_HARDWARE_NEW) : STR(STR_C_HARDWARE_OLD));
			printf("\n");
			printf("%s", STR(STR_C_TITLE_HELP4));
			printf("\n");
			printf("%s", STR(STR_C_TITLE_ROW_PLAY));
			printf("%s", STR(STR_C_TITLE_ROW_OPTIONS));
			printf("%s", STR(STR_C_TITLE_ROW_QUIT));
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
// The same part, with its label set over two lines.
//
// Sharing the Controls row left 116 px, and "Check for Update" does not fit on
// one line across that at a size worth reading. The pair is measured and centred
// as one block for the same reason the title and its tagline are: whatever the
// system font turns out to be, the two lines stay put relative to each other.
static void drawPartButtonTwoLine(const rect* r, const C2D_Text* top,
                                  const C2D_Text* bottom, float scale, u32 tab)
{
	drawStyrene(r, pressedOn(r));

	float textX = r->x + 14.0f;
	if (tab)
	{
		C2D_DrawRectSolid(r->x + PART_EDGE, r->y + PART_EDGE, 0.0f,
			PART_TAB, r->h - PART_EDGE * 2.0f, tab);
		textX = r->x + PART_EDGE + PART_TAB + 10.0f;
	}

	float tw, th, bw, bh;
	C2D_TextGetDimensions(top,    scale, scale, &tw, &th);
	C2D_TextGetDimensions(bottom, scale, scale, &bw, &bh);
	(void)tw; (void)bw;

	const float blockTop = r->y + (r->h - (th + 2.0f + bh)) * 0.5f;
	drawTextAt(top,    textX, blockTop,                 scale, CLR_INK);
	drawTextAt(bottom, textX, blockTop + th + 2.0f,     scale, CLR_INK);
}

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

// The mark a finished kit leaves on its tile.
//
// It is a shape, not a colour. The tier chip is already using the accent
// colours in the opposite corner, and the palette work in B8 settled that this
// game does not ask anyone to tell two hues apart to read its state - so
// "built" has to be legible with the colour taken away entirely.
//
// Drawn as a staircase of small blocks because citro2d has no rotated rect, and
// at twenty pixels across a diagonal resolves to a staircase anyway. The two
// strokes are a short one down and a long one back up: a tick.
#define TICK_BLK 2.0f
static void drawTick(float x, float y, u32 colour)
{
	static const signed char stroke[9][2] =
	{
		{0,3},{1,4},{2,5},
		{3,4},{4,3},{5,2},{6,1},{7,0},{8,0},
	};
	for (int i = 0; i < 9; i++)
		C2D_DrawRectSolid(x + stroke[i][0]*TICK_BLK, y + stroke[i][1]*TICK_BLK,
			0.0f, TICK_BLK*2.0f, TICK_BLK*2.0f, colour);
}

// A level is a part still attached to the runner: the same styrene, the number
// on its face, and a chip of the tier colour in the corner the way a real part
// carries its number stamped beside it. A kit that has been finished also
// carries a tick in the far corner, which is the only thing on this screen that
// says what the player has already done.
static void drawLevelPart(const rect* r, const C2D_Text* num, u32 chip, bool built)
{
	drawStyrene(r, pressedOn(r));
	C2D_DrawRectSolid(r->x + PART_EDGE, r->y + PART_EDGE, 0.0f, CHIP, CHIP, chip);
	drawTextCentred(num, r, 1.0f, CLR_INK);
	if (built)
		drawTick(r->x + r->w - PART_EDGE - 22.0f,
		         r->y + r->h - PART_EDGE - 16.0f, CLR_INK);
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

// The chip on each tile says whether that kit is finished: green for built, red
// for still to do.
//
// It used to band the chip by part count - green under six, amber to fifteen,
// red above - which was a scale that could not move, because every kit in the
// game is exactly ten parts. Twenty identical amber chips look like they encode
// something and do not. Completion is the one thing that does differ tile to
// tile, so the chip now carries that.
//
// Colour is the second channel here, not the only one: the tick drawn in the
// corner says the same thing by shape, so nothing on this page needs hue to be
// told apart.
static u32 builtColour(bool built)
{
	return built ? CLR_ACC_GO : CLR_ACC_OUT;
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

	// Which ten are on screen and how many of the twenty are finished, printed on
	// the mat to the right of the plaque. It is rebuilt every frame, so it goes
	// in the dynamic buffer alongside the volume percentage.
	//
	// The tally shares the line rather than getting one of its own because there
	// is no second line to have: the plaque is 28 tall at y 6 and the top row of
	// tiles starts at y 52.
	C2D_TextBufClear(dynBuf);
	// Sized for the widest an int pair can print, not for the two strings this
	// actually produces, so the compiler can see it cannot truncate.
	char range[48];
	snprintf(range, sizeof(range), "%d-%d   %d/%d built",
		first + 1, first + LEVELS_PER_PAGE, builtTotal(), LEVEL_COUNT);
	C2D_Text txtRange;
	C2D_TextParse(&txtRange, dynBuf, range);
	float rw, rh;
	C2D_TextGetDimensions(&txtRange, 0.45f, 0.45f, &rw, &rh);
	drawTextAt(&txtRange, SCREEN_W - 14.0f - rw,
		rPlaquePage.y + (rPlaquePage.h - rh) * 0.5f, 0.45f, CLR_TAG);

	drawLevelRunner();
	for (int i = 0; i < LEVELS_PER_PAGE; i++)
		drawLevelPart(&rLevel[i], &txtNum[first + i],
			builtColour(levelBuilt[first + i]), levelBuilt[first + i]);

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

	drawTextAt(&txtVolNote, 14.0f, 110.0f, 0.4f, CLR_DIM);

	drawPartButton(&rControls, &txtControls, 0.8f, CLR_ACC_SET);
	drawPartButtonTwoLine(&rUpdate, &txtUpd1, &txtUpd2, 0.5f, CLR_ACC_SET);

	// Item 50 - the language row. A plaque, not a button of its own colour,
	// because a tap here does not go anywhere - it cycles in place, the same
	// as - and shown right beside - +/- do for volume above it.
	drawPlaque(&rLanguage);
	drawTextAt(&txtLanguage, rLanguage.x + 10.0f,
		rLanguage.y + (rLanguage.h - 16.0f) * 0.5f, 0.4f, CLR_INK);
	{
		C2D_TextBufClear(dynBuf);
		C2D_Text txtLangName;
		C2D_TextParse(&txtLangName, dynBuf, languageName(languageCurrent()));
		C2D_TextOptimize(&txtLangName);
		float lw, lh;
		C2D_TextGetDimensions(&txtLangName, 0.4f, 0.4f, &lw, &lh);
		drawTextAt(&txtLangName, rLanguage.x + rLanguage.w - 10.0f - lw,
			rLanguage.y + (rLanguage.h - lh) * 0.5f, 0.4f, CLR_INK_DIM);
	}

	drawPartButton(&rOptBack,  &txtBack,     0.8f, CLR_ACC_OUT);

	// Two live readings against fixed retail Old 3DS ceilings, not emulator
	// system-memory numbers: the 64 MB of FCRAM the game occupies, then the
	// separate 6 MB VRAM bank. They keep their own plaque so they are visible
	// in the bottom-right without competing with the Options controls.
	drawPlaque(&rRam);
	C2D_TextBufClear(dynBuf);

	char ram[16];
	char vram[16];
	snprintf(ram,  sizeof(ram),  "RAM %u%%",  memoryStatusAppPercent());
	snprintf(vram, sizeof(vram), "VRAM %u%%", memoryStatusVramPercent());

	C2D_Text txtRam, txtVram;
	C2D_TextParse(&txtRam,  dynBuf, ram);
	C2D_TextParse(&txtVram, dynBuf, vram);

	// Large enough to read without leaning into the screen - the old 0.36 was
	// legible only because you knew what it said. The pair is centred as one
	// block so the rows sit level in the plaque instead of hugging its edges.
	float rw, rh, vw, vh;
	C2D_TextGetDimensions(&txtRam,  MEM_TEXT_SCALE, MEM_TEXT_SCALE, &rw, &rh);
	C2D_TextGetDimensions(&txtVram, MEM_TEXT_SCALE, MEM_TEXT_SCALE, &vw, &vh);

	const float blockTop = rRam.y + (rRam.h - (rh + MEM_ROW_GAP + vh)) * 0.5f;

	drawTextAt(&txtRam,  rRam.x + (rRam.w - rw) * 0.5f,
		blockTop, MEM_TEXT_SCALE, CLR_INK);
	drawTextAt(&txtVram, rRam.x + (rRam.w - vw) * 0.5f,
		blockTop + rh + MEM_ROW_GAP, MEM_TEXT_SCALE, CLR_INK);
}

static void drawControlsPage(void)
{
	drawMat();
	drawPlaqueLabel(&rPlaquePage, &txtCtlHdr, 0.55f);

	// Item 37 - while a row is listening, "B cancels" sits where the levels
	// page puts its own page tag: to the right of the header plaque, so the
	// one time it matters it is exactly where the eye already goes to check
	// what a page is doing.
	C2D_TextBufClear(dynBuf);
	if (controlsIsListening())
	{
		C2D_Text txtHint;
		C2D_TextParse(&txtHint, dynBuf, STR(STR_CANCEL_HINT));
		C2D_TextOptimize(&txtHint);
		float hw, hh;
		C2D_TextGetDimensions(&txtHint, 0.4f, 0.4f, &hw, &hh);
		drawTextAt(&txtHint, SCREEN_W - 14.0f - hw,
			rPlaquePage.y + (rPlaquePage.h - hh) * 0.5f, 0.4f, CLR_TAG);
	}

	// The list is laid on a sheet of styrene, the way the instruction leaflet
	// sits on the bench: text this small needs a flat light ground to stay
	// legible against the mat's grid.
	static const rect sheet = { 8.0f, 40.0f, 304.0f, 140.0f };
	drawPlaque(&sheet);

	int count = controlCount();
	if (count > CONTROL_ROWS) count = CONTROL_ROWS;
	// 11 rather than 12: the sheet is 140 tall from y 40, and the twelfth row
	// (item 39's screenshot control took the count to twelve) has to land
	// inside it. At the 12 pitch eleven rows used, the twelfth would print its
	// text past y 180, off the bottom of the styrene - the same way 13 failed
	// when the count went to eleven. 12*11=132 leaves an 8px margin above the
	// sheet's own bottom edge instead.
	for (int i = 0; i < count; i++)
	{
		const float y = 44.0f + i * 11.0f;

		// Nine of the twelve rows are remappable now - every row but the
		// three stylus ones (tap/rub/drag). Three of those nine (D-Pad L/R,
		// L/R, Y/X) hold two independent actions, split left/right within
		// the row's own key column - see controlsRowIsSplit.
		int leftAction  = controlsRowAction(i, false);
		int rightAction = controlsRowAction(i, true);

		int listeningHere = -1;
		if (controlsIsListening())
		{
			settingsRemapAction la = controlsListeningAction();
			if (leftAction  >= 0 && la == (settingsRemapAction)leftAction)  listeningHere = leftAction;
			if (rightAction >= 0 && la == (settingsRemapAction)rightAction) listeningHere = rightAction;
		}

		if (listeningHere >= 0)
		{
			// Minecraft-style: the row being rebound shows a bracketed
			// prompt in place of its key(s) - no separate menu, no confirm
			// step - and the very next press one of the twelve remappable
			// keys or B (see controlsPollListen()) ends it, back to a
			// plain key label. Spans the row's full key column even for a
			// split row - only one half can be listening at a time, and
			// showing the prompt at full width keeps this exactly the
			// proven layout the single-action rows already used.
			char buf[32];
			snprintf(buf, sizeof(buf), "> %s <", STR(STR_LISTENING));
			C2D_Text txtListen;
			C2D_TextParse(&txtListen, dynBuf, buf);
			C2D_TextOptimize(&txtListen);
			drawTextAt(&txtListen, 20.0f, y, 0.4f, CLR_ACC_SET);
		}
		else if (leftAction < 0)
		{
			// Not remappable at all (the three stylus rows) - unchanged
			// from before item 37 widened anything: always this row's own
			// fixed key text.
			drawTextAt(&txtKey[i], 20.0f, y, 0.4f, CLR_INK_DIM);
		}
		else if (leftAction == rightAction)
		{
			// One action, one key - the common case. A conflict (this
			// action sharing a button with any other) is shown, not
			// blocked - Minecraft does the same - so the only difference
			// here is colour: the key prints in the same muted red Quit
			// uses, everywhere else on this sheet.
			u32 keyClr = CLR_INK_DIM;
			if (controlsRemapConflict((settingsRemapAction)leftAction)) keyClr = CLR_ACC_OUT;
			settingsRemapKey key = settingsRemapButton((settingsRemapAction)leftAction);
			drawTextAt(&txtKeyLabel[key], 20.0f, y, 0.4f, keyClr);
		}
		else
		{
			// A split row: each half gets its own key label and its own
			// conflict colour, independently of the other half.
			u32 lClr = CLR_INK_DIM, rClr = CLR_INK_DIM;
			if (controlsRemapConflict((settingsRemapAction)leftAction))  lClr = CLR_ACC_OUT;
			if (controlsRemapConflict((settingsRemapAction)rightAction)) rClr = CLR_ACC_OUT;
			settingsRemapKey lKey = settingsRemapButton((settingsRemapAction)leftAction);
			settingsRemapKey rKey = settingsRemapButton((settingsRemapAction)rightAction);
			drawTextAt(&txtKeyLabel[lKey], 20.0f, y, 0.4f, lClr);
			drawTextAt(&txtKeyLabel[rKey], 70.0f, y, 0.4f, rClr);
		}
		drawTextAt(&txtAction[i], 118.0f, y, 0.4f, CLR_INK);
	}

	drawPartButton(&rCtlBack,  &txtBack,  0.8f, CLR_ACC_OUT);
	drawPartButton(&rCtlReset, &txtReset, 0.7f, CLR_ACC_SET);
}

// A few words for the glass. The full sentence - including the reason a check
// failed - goes on the console up top, which has the width for it; this page
// only has to say which of the six things is happening.
static const char* updateShortLabel(void)
{
	switch (updaterState())
	{
		case UPDATE_CHECKING:    return STR(STR_UPD_SHORT_CHECKING);
		case UPDATE_UP_TO_DATE:  return STR(STR_UPD_SHORT_UP_TO_DATE);
		case UPDATE_AVAILABLE:   return STR(STR_UPD_SHORT_AVAILABLE);
		case UPDATE_DOWNLOADING: return STR(STR_UPD_SHORT_DOWNLOADING);
		case UPDATE_INSTALLING:  return STR(STR_UPD_SHORT_INSTALLING);
		case UPDATE_DONE:        return STR(STR_UPD_SHORT_DONE);
		case UPDATE_FAILED:      return STR(STR_UPD_SHORT_FAILED);
		default:                 return STR(STR_UPD_SHORT_READY);
	}
}

static void drawUpdatePage(void)
{
	drawMat();
	drawPlaqueLabel(&rPlaquePage, &txtUpdHdr, 0.55f);

	C2D_TextBufClear(dynBuf);

	// Which version is on the card, and which one GitHub has. The second line is
	// only drawn once there is an answer - an empty "Newest :" reads as a bug.
	char installed[48];
	snprintf(installed, sizeof(installed), STR(STR_UPD_INSTALLED_ROW),
		MODELKIT_VERSION_SET ? MODELKIT_VERSION : STR(STR_C_UPD_NOT_SET));
	C2D_Text txtInstalled;
	C2D_TextParse(&txtInstalled, dynBuf, installed);
	drawTextAt(&txtInstalled, 14.0f, 44.0f, 0.45f, CLR_TEXT);

	if (updaterLatestVersion()[0] != '\0')
	{
		char newest[48];
		snprintf(newest, sizeof(newest), STR(STR_UPD_NEWEST_ROW), updaterLatestVersion());
		C2D_Text txtNewest;
		C2D_TextParse(&txtNewest, dynBuf, newest);
		drawTextAt(&txtNewest, 14.0f, 62.0f, 0.45f, CLR_TEXT);
	}

	C2D_Text txtShort;
	C2D_TextParse(&txtShort, dynBuf, updateShortLabel());
	drawTextAt(&txtShort, 14.0f, 100.0f, 0.6f, CLR_TEXT);

	// The bar only appears once there is something to measure, and it is cut
	// into the mat the same way the volume bar is.
	const int progress = updaterProgress();
	if (progress >= 0)
	{
		C2D_DrawRectSolid(UPD_BAR_X - 3.0f, UPD_BAR_Y - 3.0f, 0.0f,
			UPD_BAR_W + 6.0f, UPD_BAR_H + 6.0f, CLR_MAT_EDGE);
		C2D_DrawRectSolid(UPD_BAR_X, UPD_BAR_Y, 0.0f, UPD_BAR_W, UPD_BAR_H, CLR_BAR_OFF);

		float filled = (UPD_BAR_W * (float)progress) / 100.0f;
		if (filled > 0.0f)
			C2D_DrawRectSolid(UPD_BAR_X, UPD_BAR_Y, 0.0f, filled, UPD_BAR_H, CLR_BAR_ON);
	}

	// Nothing to press while the worker holds the job - zonesFor already refuses
	// the taps, and drawing no buttons is what makes that visible rather than
	// looking like the console has stopped responding.
	if (updaterBusy()) return;

	if (!updaterAvailable())
	{
		drawPartButton(&rUpdAction, &txtUpdOff, 0.6f, CLR_ACC_OUT);
		drawPartButton(&rUpdBack,   &txtBack,   0.55f, CLR_ACC_OUT);
		return;
	}

	const C2D_Text* action = &txtUpdCheck;
	u32 tab = CLR_ACC_SET;
	switch (updaterState())
	{
		case UPDATE_AVAILABLE: action = &txtUpdGet;     tab = CLR_ACC_GO; break;
		case UPDATE_DONE:      action = &txtUpdRestart; tab = CLR_ACC_GO; break;
		default: break;
	}

	drawPartButton(&rUpdAction, action,   0.6f,  tab);
	drawPartButton(&rUpdBack,   &txtBack, 0.55f, CLR_ACC_OUT);
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
		case PAGE_UPDATE:   drawUpdatePage();   break;
		default:            drawTitlePage();    break;
	}

	C2D_Flush();
}
