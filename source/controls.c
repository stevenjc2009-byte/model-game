#include "controls.h"

#include <stddef.h>  // size_t, used by controlsPollListen's capture table

// ---------------------------------------------------------------------------
// The table
//
// Text lives behind strings.h now (item 50), keyed by id rather than typed
// here directly, so a second language can supply its own "reset the view"
// without this file changing at all.

static const controlLine lines[] =
{
	{ STR_CTL_TAP_KEY,    STR_CTL_TAP_ACT    },
	{ STR_CTL_RUB_KEY,    STR_CTL_RUB_ACT    },
	{ STR_CTL_DRAG_KEY,   STR_CTL_DRAG_ACT   },
	{ STR_CTL_DPADLR_KEY, STR_CTL_DPADLR_ACT },
	{ STR_CTL_B_KEY,      STR_CTL_B_ACT      },
	{ STR_CTL_LR_KEY,     STR_CTL_LR_ACT     },
	{ STR_CTL_A_KEY,      STR_CTL_A_ACT      },
	{ STR_CTL_YX_KEY,     STR_CTL_YX_ACT     },
	{ STR_CTL_DDOWN_KEY,  STR_CTL_DDOWN_ACT  },
	{ STR_CTL_DUP_KEY,    STR_CTL_DUP_ACT    },
	{ STR_CTL_SELECT_KEY, STR_CTL_SELECT_ACT },
	{ STR_CTL_START_KEY,  STR_CTL_START_ACT  },
};

_Static_assert(sizeof(lines) / sizeof(lines[0]) == CONTROL_LINE_COUNT,
	"CONTROL_LINE_COUNT must match this table: the Controls page sizes its text "
	"arrays from it, and the workbench console has room for exactly this many "
	"rows before it reaches the live readout at row 18.");

const controlLine* controlList(void)
{
	return lines;
}

int controlCount(void)
{
	return (int)(sizeof(lines) / sizeof(lines[0]));
}

// ---------------------------------------------------------------------------
// Item 37 - remapping
//
// Widened (coordinator direction, same day as the original landing) from
// three actions (reset-view, pause-menu, close-game) bindable only among
// A/SELECT/START, to all twelve of the game's single-button-press actions,
// each bindable to any of the game's twelve discrete buttons - see
// settings.h's settingsRemapAction/settingsRemapKey for both full lists,
// confirmed against main.c's own key handling (main.c:3117-3319, 3029,
// 3071) before this landed, not assumed from this table alone. The three
// stylus rows (tap/rub/drag) are analog or gesture-based, not single-button
// presses, and stay fixed - they were never in scope.
//
// This file's own 12-row table, and the touch-screen Controls page built
// from it (title.c), can say anything about any of the twelve - they are
// drawn by code in this project's own files, using controlsKeyLabelId()
// rather than borrowing another row's fixed text the way the three-action
// version did.
//
// main.c's in-game Controls display (main.c:2496) is a different story: it
// keeps its OWN 8-row hardcoded key strings ("TAP","RUB","DRAG","D-PAD",
// "L / R","A","SELECT","START") and its own call to controlKeyRow(i, 8).
// That file is not owned here and cannot be edited. Its array still only
// has text for A, SELECT and START among the single-button rows - nothing
// for B, X, Y, L, R or a lone D-Pad direction - so a player who moves
// reset-view, pause-menu or close-game onto any of those now leaves that
// page showing stale text (controlKeyRow falls back to the row's original
// string rather than guessing - see rowForKey8 below), and back/zoom/photo
// mode/pose/screenshot/paging were never represented on that page at all,
// same as before. This is a known, disclosed consequence of the widen, not
// something fixed here - see the completion report for what main.c's page
// would need to stay accurate.
static const u32 physicalBitOf[REMAP_KEY_COUNT] =
{
	[REMAP_KEY_A]      = KEY_A,
	[REMAP_KEY_B]      = KEY_B,
	[REMAP_KEY_X]      = KEY_X,
	[REMAP_KEY_Y]      = KEY_Y,
	[REMAP_KEY_L]      = KEY_L,
	[REMAP_KEY_R]      = KEY_R,
	[REMAP_KEY_DUP]    = KEY_DUP,
	[REMAP_KEY_DDOWN]  = KEY_DDOWN,
	[REMAP_KEY_DLEFT]  = KEY_DLEFT,
	[REMAP_KEY_DRIGHT] = KEY_DRIGHT,
	[REMAP_KEY_SELECT] = KEY_SELECT,
	[REMAP_KEY_START]  = KEY_START,
};

// What each action drives downstream - the bit main.c already checks for
// it natively (reset at main.c:3117, back at :3296, zoom out/in at :3283-4,
// photo mode/next angle at :3303/3310, pose/screenshot at :3134/3126, page
// left/right at :3317-9, pause/close at :3071/3029) and cannot be changed
// to check instead, since main.c is not edited here. Identical to
// physicalBitOf's own shape today because the default assignment is the
// identity, but kept as its own table since the two mean different things:
// one is "what button", the other is "what bit the rest of the game
// already checks for this action".
static const u32 logicalBitOf[REMAP_ACTION_COUNT] =
{
	[REMAP_ACTION_RESET]      = KEY_A,
	[REMAP_ACTION_BACK]       = KEY_B,
	[REMAP_ACTION_PHOTO_MODE] = KEY_Y,
	[REMAP_ACTION_NEXT_ANGLE] = KEY_X,
	[REMAP_ACTION_ZOOM_OUT]   = KEY_L,
	[REMAP_ACTION_ZOOM_IN]    = KEY_R,
	[REMAP_ACTION_SCREENSHOT] = KEY_DUP,
	[REMAP_ACTION_POSE]       = KEY_DDOWN,
	[REMAP_ACTION_PAGE_LEFT]  = KEY_DLEFT,
	[REMAP_ACTION_PAGE_RIGHT] = KEY_DRIGHT,
	[REMAP_ACTION_PAUSE]      = KEY_SELECT,
	[REMAP_ACTION_CLOSE]      = KEY_START,
};

u32 controlsTranslate(u32 keys)
{
	const u32 raw = keys;
	keys &= ~(KEY_A | KEY_B | KEY_X | KEY_Y | KEY_L | KEY_R |
	          KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT |
	          KEY_SELECT | KEY_START);

	// A press of the button currently assigned to an action sets that
	// action's logical bit. Two actions sharing a button (a conflict, shown
	// red on the Controls page rather than refused - see controlsRemapConflict)
	// both fire from the one press; that is deliberate, not a bug, and
	// matches how Minecraft's own control screen treats a clash.
	//
	// This also means main.c's own downstream uses of these bits for
	// things that are not "the" remappable action - the in-game pause
	// menu's cursor navigation at main.c:2581-2622 reads the same
	// translated KEY_A/B/SELECT/DUP/DDOWN/DLEFT/DRIGHT bits gameplay does -
	// move in lockstep with whatever the player has remapped onto them.
	// That is inherent to this one-hook design and was already true for
	// A/SELECT/START before the widen; it is not new, and not something
	// fixed here - flagged in the completion report for awareness.
	for (int action = 0; action < REMAP_ACTION_COUNT; action++)
	{
		settingsRemapKey key = settingsRemapButton((settingsRemapAction)action);
		if (raw & physicalBitOf[key]) keys |= logicalBitOf[action];
	}
	return keys;
}

static bool listening = false;
static settingsRemapAction listenAction = REMAP_ACTION_RESET;

void controlsBeginListen(settingsRemapAction action)
{
	listening = true;
	listenAction = action;
}

void controlsCancelListen(void)
{
	listening = false;
}

bool controlsIsListening(void)
{
	return listening;
}

settingsRemapAction controlsListeningAction(void)
{
	return listenAction;
}

// ---------------------------------------------------------------------------
// The never-strandable guarantee, restated for the widened set.
//
// The old guarantee leaned on B sitting permanently outside the assignable
// key set at all - it simply was not one of REMAP_KEY_COUNT's three values,
// so it could never be a bind target and was safe to hardcode as "cancel".
// That rail is gone now that B is REMAP_KEY_B, a real, assignable key with
// BACK bound to it by default.
//
// The replacement rule, chosen and enforced here: B is permanently reserved
// as the Controls page's cancel button - a raw physical press of it always
// exits listening state, checked first, unconditionally, before anything
// else in this function runs and without consulting the remap table at
// all. This holds no matter what action B is currently assigned to, how
// many actions currently conflict on it, or what the player has done to
// every other key - a press of physical B always gets a player out of
// listening. The one thing it costs: B itself can never be *assigned* to
// an action by listening (it is simply never in the capture table below),
// only ever loaded as a value - the shipped default, a save file, or
// settingsResetRemap(). That is a deliberate trade, not an oversight.
//
// Separately, and independently of the above: settingsSetRemapButton()
// (settings.c) only ever accepts an in-range key and there is no operation
// anywhere in this system that clears a slot to "unbound" - every action
// always has exactly one, always-valid key, from a fresh install through
// any amount of remapping. So no action - pause, close, or any other - can
// ever end up reachable from nowhere; the worst that can happen is a
// conflict, which the Minecraft-style model treats as valid and simply
// colours red (see controlsRemapConflict), not as broken. Reset-to-defaults
// (settingsResetRemap(), the Controls page's own Reset row) is also always
// available regardless, as a second, independent way back to a known-good
// layout.
void controlsPollListen(void)
{
	if (!listening) return;

	// hidKeysDown(), not the kDown title.c was handed - see controls.h. This
	// has to be the actual physical press, not whatever it currently means.
	const u32 raw = hidKeysDown();

	if (raw & KEY_B) { listening = false; return; }

	static const struct { u32 bit; settingsRemapKey key; } capture[] =
	{
		{ KEY_A,      REMAP_KEY_A      },
		{ KEY_X,      REMAP_KEY_X      },
		{ KEY_Y,      REMAP_KEY_Y      },
		{ KEY_L,      REMAP_KEY_L      },
		{ KEY_R,      REMAP_KEY_R      },
		{ KEY_DUP,    REMAP_KEY_DUP    },
		{ KEY_DDOWN,  REMAP_KEY_DDOWN  },
		{ KEY_DLEFT,  REMAP_KEY_DLEFT  },
		{ KEY_DRIGHT, REMAP_KEY_DRIGHT },
		{ KEY_SELECT, REMAP_KEY_SELECT },
		{ KEY_START,  REMAP_KEY_START  },
	};
	for (size_t i = 0; i < sizeof(capture) / sizeof(capture[0]); i++)
	{
		if (raw & capture[i].bit)
		{
			settingsSetRemapButton(listenAction, capture[i].key);
			listening = false;
			return;
		}
	}

	// Anything else (the circle pad, C-stick, stylus...) is simply not a
	// valid target - so it is ignored and listening carries on rather than
	// binding something this project cannot then display honestly.
}

bool controlsRemapConflict(settingsRemapAction action)
{
	settingsRemapKey key = settingsRemapButton(action);
	for (int i = 0; i < REMAP_ACTION_COUNT; i++)
		if (i != (int)action && settingsRemapButton((settingsRemapAction)i) == key)
			return true;
	return false;
}

// Which action (or, for the three joint rows, which of two actions) sits at
// each row of the 12-row canonical table. Three rows now hold a pair rather
// than a single action - row 3 (D-Pad L/R: page manual left/right), row 5
// (L/R: zoom out/in) and row 7 (Y/X: photo mode/next angle) - so there are
// two parallel tables, left and right; every other remappable row has the
// same action in both, and the three stylus rows (0, 1, 2) are -1 in both,
// meaning "not remappable at all".
static const int actionForRowL12[CONTROL_LINE_COUNT] =
{
	-1, -1, -1,
	REMAP_ACTION_PAGE_LEFT,
	REMAP_ACTION_BACK,
	REMAP_ACTION_ZOOM_OUT,
	REMAP_ACTION_RESET,
	REMAP_ACTION_PHOTO_MODE,
	REMAP_ACTION_POSE,
	REMAP_ACTION_SCREENSHOT,
	REMAP_ACTION_PAUSE,
	REMAP_ACTION_CLOSE,
};
static const int actionForRowR12[CONTROL_LINE_COUNT] =
{
	-1, -1, -1,
	REMAP_ACTION_PAGE_RIGHT,
	REMAP_ACTION_BACK,
	REMAP_ACTION_ZOOM_IN,
	REMAP_ACTION_RESET,
	REMAP_ACTION_NEXT_ANGLE,
	REMAP_ACTION_POSE,
	REMAP_ACTION_SCREENSHOT,
	REMAP_ACTION_PAUSE,
	REMAP_ACTION_CLOSE,
};

int controlsRowAction(int row, bool rightHalf)
{
	if (row < 0 || row >= CONTROL_LINE_COUNT) return -1;
	return rightHalf ? actionForRowR12[row] : actionForRowL12[row];
}

bool controlsRowIsSplit(int row)
{
	if (row < 0 || row >= CONTROL_LINE_COUNT) return false;
	return actionForRowL12[row] >= 0 && actionForRowL12[row] != actionForRowR12[row];
}

// The 12-row canonical table no longer needs a row-borrowing scheme of its
// own - see controlsKeyLabelId() below, which title.c now uses directly.
// This table only exists to describe main.c's own separate 8-row Controls
// display from the outside - see the file header comment. If that page's
// row order ever changes there is nothing here that would notice; it would
// need updating by hand to match.
static const int actionForRow8[8] =
{
	-1, -1, -1, -1, -1,
	REMAP_ACTION_RESET, REMAP_ACTION_PAUSE, REMAP_ACTION_CLOSE,
};

// Only A, SELECT and START have a row on that page at all - every other key
// this system can now assign reset-view/pause-menu/close-game to has no
// honest row to point at, so it maps to -1 and controlKeyRow falls back to
// the row's own original text instead of reading out of bounds. See the
// file header comment and the completion report.
static const int rowForKey8[REMAP_KEY_COUNT] =
{
	[REMAP_KEY_A]      =  5,
	[REMAP_KEY_B]      = -1,
	[REMAP_KEY_X]      = -1,
	[REMAP_KEY_Y]      = -1,
	[REMAP_KEY_L]      = -1,
	[REMAP_KEY_R]      = -1,
	[REMAP_KEY_DUP]    = -1,
	[REMAP_KEY_DDOWN]  = -1,
	[REMAP_KEY_DLEFT]  = -1,
	[REMAP_KEY_DRIGHT] = -1,
	[REMAP_KEY_SELECT] =  6,
	[REMAP_KEY_START]  =  7,
};

int controlKeyRow(int row, int count)
{
	if (count != 8) return row;
	if (row < 0 || row >= 8 || actionForRow8[row] < 0) return row;

	settingsRemapAction action = (settingsRemapAction)actionForRow8[row];
	settingsRemapKey    key    = settingsRemapButton(action);
	int mapped = rowForKey8[key];
	return (mapped >= 0) ? mapped : row;
}

// See controls.h. Indexed straight off settingsRemapKey - a key added there
// needs a row added here (and in strings.h/strings.c's STR_KEY_* block) in
// the same slot.
static const stringId keyLabelId[REMAP_KEY_COUNT] =
{
	[REMAP_KEY_A]      = STR_KEY_A,
	[REMAP_KEY_B]      = STR_KEY_B,
	[REMAP_KEY_X]      = STR_KEY_X,
	[REMAP_KEY_Y]      = STR_KEY_Y,
	[REMAP_KEY_L]      = STR_KEY_L,
	[REMAP_KEY_R]      = STR_KEY_R,
	[REMAP_KEY_DUP]    = STR_KEY_DUP,
	[REMAP_KEY_DDOWN]  = STR_KEY_DDOWN,
	[REMAP_KEY_DLEFT]  = STR_KEY_DLEFT,
	[REMAP_KEY_DRIGHT] = STR_KEY_DRIGHT,
	[REMAP_KEY_SELECT] = STR_KEY_SELECT,
	[REMAP_KEY_START]  = STR_KEY_START,
};

stringId controlsKeyLabelId(settingsRemapKey key)
{
	if (key < 0 || key >= REMAP_KEY_COUNT) return STR_KEY_A;
	return keyLabelId[key];
}
