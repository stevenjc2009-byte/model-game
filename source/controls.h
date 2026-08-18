// The button list, in one place.
//
// Two screens print it: the top-screen console on the workbench, and the touch
// screen's Controls page inside Options. Held as data rather than as two blocks
// of printf, so the two can never drift apart.

#pragma once

#include <3ds.h>

#include "settings.h"
#include "strings.h"

typedef struct
{
	stringId key;     ///< What you press. Kept short enough for the touch screen's left column.
	stringId action;  ///< What it does.
} controlLine;

const controlLine* controlList(void);

// Which row's key label belongs on display row `row` of an 8-row list -
// main.c's own in-game Controls display (main.c:2496), which is not in this
// file and cannot be edited from here.
//
// This used to also serve the 12-row canonical table (this file's own
// touch-screen Controls page), but the widened remap outgrew it - see
// controlsKeyLabelId() for what replaced that half. It still exists, and
// still only knows the 8-row shape, purely because main.c's page calls it
// and cannot be changed to call anything else.
//
// Only three of that page's 8 rows (its own hardcoded "A", "SELECT",
// "START") were ever able to move - see controls.c's rowForKey8. Now that
// reset-view, pause-menu and close-game can each land on any of twelve
// keys, not just those three, a row here can go stale: if the action that
// row displays has been moved onto a key this 8-row page has no text for at
// all (B, X, Y, L, R, any D-Pad direction), this falls back to `row`
// unchanged - main.c keeps showing its original text rather than reading
// out of bounds. See the completion report for what that means for that
// page and the fix it needs on the main.c side. Any `count` other than 8
// also returns `row` unchanged, same as before.
int controlKeyRow(int row, int count);

// Twelve, and both screens are sized off it rather than guessing at it.
//
// On the console the block starts at row 6, so twelve lines end at row 17 and
// they now sit directly under the live readout at row 18 with no blank row
// between them - the row math is arithmetic, not a fixed margin, so it moves
// with the count, and this is the last line that fits. A thirteenth would
// overwrite the readout. On the touch screen the Controls page lays them on a
// styrene sheet 140 tall: twelve rows need an 11px pitch to fit (12*11=132,
// under the 140), which is where that pitch comes from - the 12px pitch
// eleven rows used would run the twelfth row past the sheet. The sheet cannot
// simply grow instead - the Back button under it starts at y 188.
//
// It is a compile-time constant because the Controls page has to size its
// parsed-text arrays before it has run anything. That page used to keep its own
// 8 and clamp to it, which meant a ninth line here would simply not appear on it
// - no error, no missing-row marker, just a control the player is never told
// about. controls.c static-asserts that the two agree, so adding a line without
// dealing with both screens is a build failure rather than a silent omission.
#define CONTROL_LINE_COUNT 12

int controlCount(void);

// ---------------------------------------------------------------------------
// Item 37 - remapping.
//
// Translates a raw hidKeysDown()/Held()/Up() bitmask into the bitmask the
// rest of the game should act on, by applying the current remap table from
// settings.h. main.c calls this once, at the top of the frame, exactly where
// it used to call applyStartSelectSwap - see the report for the one-line
// change that hooks it in. Everything downstream (pausing, closing, the
// workbench's own view reset, every menu's B/SELECT back-out, zoom, pose,
// photo mode, paging the manual) keeps checking the same KEY_A / KEY_B /
// KEY_X / KEY_Y / KEY_L / KEY_R / KEY_D* / KEY_SELECT / KEY_START bits it
// always did and does not have to know remapping exists.
//
// Widened (coordinator direction, same day as the original landing) from
// just A/SELECT/START to every one of the game's single-button-press
// actions - see settings.h's settingsRemapAction/settingsRemapKey for the
// full lists. Every bit any of those twelve keys can produce is touched;
// everything else (the circle pad, C-stick, stylus) passes through
// unchanged.
u32 controlsTranslate(u32 keys);

// Puts the Controls page's row for `action` into "listening" state: the very
// next physical press of one of the twelve remappable keys becomes that
// action's new binding, Minecraft-style, with no confirm step. B always
// cancels rather than binding, regardless of what B is itself currently
// assigned to - see controls.c's controlsPollListen() for the
// never-strandable guarantee this is the enforcement of - so a player who
// opens listening by mistake is never stuck with no way out of it.
void controlsBeginListen(settingsRemapAction action);
void controlsCancelListen(void);
bool controlsIsListening(void);
settingsRemapAction controlsListeningAction(void);

// Call once per frame, after hidScanInput(), while the Controls page is on
// screen. No-op if nothing is listening. Reads hidKeysDown()/hidKeysUp()
// directly rather than whatever kDown/kHeld/kUp the caller already has,
// because a rebind has to capture the next PHYSICAL press - the very value
// controlsTranslate() exists to remap away - not whatever that press
// currently means under the old binding.
void controlsPollListen(void);

// True if `action` currently shares a button with any other of the twelve
// remappable actions. The Controls page draws that row's key in red rather
// than refusing the assignment - conflicts are allowed, the same way
// Minecraft's own control screen allows them; the row just says so.
bool controlsRemapConflict(settingsRemapAction action);

// Which action row `row` of the 12-row canonical table holds - the
// touch-screen Controls page's own row numbering, not main.c's separate
// 8-row one. Three rows (D-Pad L/R, L/R, Y/X) now carry two independently
// bindable actions rather than one - `rightHalf` selects which; every other
// row ignores it and returns the same action either way. -1 if that row is
// not remappable at all (the three stylus rows). title.c uses this both on
// a tap (see controlsRowIsSplit for how it picks left vs right) to know
// what to hand controlsBeginListen(), and while drawing the page.
int controlsRowAction(int row, bool rightHalf);

// True if row `row` holds two independently bindable actions rather than
// one - i.e. controlsRowAction(row, false) and (row, true) differ. title.c
// uses this to decide whether to split that row's key column into two
// tappable/drawable halves.
bool controlsRowIsSplit(int row);

// The short display name (see strings.h's STR_KEY_* block) for the button
// `key` itself, independent of which action it happens to be assigned to
// right now. This is what the Controls page actually draws in a
// remappable row's key column - it no longer borrows another row's own
// key text the way the three-action version did, because the widened key
// set has entries (B, X, Y, L, R, each D-Pad direction) that never had a
// row of their own to borrow from in the first place.
stringId controlsKeyLabelId(settingsRemapKey key);
