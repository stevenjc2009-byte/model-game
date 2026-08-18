// Settings that outlive the screen that sets them.
//
// Master volume lives here rather than inside either menu because the title
// screen and the pause menu both set the same one. Two copies would let a
// player change the level in one place and have the other contradict it.
//
// All settings here are persisted to the SD card by settingsSave() and restored
// by settingsLoad(), so they survive a reboot.

#pragma once

#include <3ds.h>
#include <stdbool.h>

// The bar is ten cells wide and moves one cell a press, so the level can never
// change without the player seeing it change.
#define VOLUME_CELLS 10
#define VOLUME_STEP  10

// 0 to 100. audioUpdateVolume() reads it every frame and hands it to the DSP as
// the master level, so this is a live control, not a placeholder. 0 is the mute:
// there is no separate mute flag because a master level that reaches zero is
// already one.
//
// It will still be silent under an emulator with no DSP firmware dumped - the
// whole audio layer stands down there - so a level that appears to do nothing on
// a PC is the missing firmware, not this.
int settingsVolume(void);

// Moves the level by delta and clamps to 0..100. It clamps rather than wraps: a
// player winding the volume down to silence should not have the next press drop
// them back to full.
void settingsVolumeStep(int delta);

// Whether the illustrated tutorial sheet takes the top screen. On by default,
// because it is the only thing explaining how the game is played, and it is what
// makes a brand new install show the sheet on level 1 without the player having
// to go and find a setting first.
//
// The switch governs every level, not just level 1: with it off the top screen
// carries the placeholder name-plate instead, and with it on the sheet follows
// the player to whatever they are building.
//
// The Options page is the only thing in the game that moves it, in either
// direction. Finishing level 1 used to switch it off automatically, which is
// what made the breathing dot look like a level-1-only feature - the dot is
// drawn only while the sheet is up. That is gone, and so is the one-time undo
// that briefly turned the sheet back on for saves carrying the old flag: a
// setting a player is holding is theirs, and a patch does not get to move it.
bool settingsShowTutorial(void);
void settingsSetShowTutorial(bool on);

// Swaps what START and SELECT do. Off by default, which is SELECT for the pause
// menu and START to close the game. Players reach for START expecting the menu,
// so this lets them have it that way round.
//
// Superseded by the remap table below (item 37) but kept as a real, working
// entry point: main.c's in-game Options page still calls these two directly
// and cannot be edited to call anything else. Internally this is now a
// two-state PRESET of the remap table - "swap the pause and close bindings,
// leave reset-view where it is" - rather than a second place button
// assignments are stored, so the two systems cannot disagree about which
// button currently does what. See settingsRemapButton() for the general
// case; this pair is the one slice of it the old UI can still reach.
bool settingsSwapStartSelect(void);
void settingsSetSwapStartSelect(bool on);

// Item 37 - control remapping. Twelve actions (see settingsRemapAction),
// each fired by one of twelve buttons (see settingsRemapKey) - every
// single-button-press binding the game has (the D-Pad/L/R/Y/X pairs split
// into their own independent halves; the three stylus rows are analog or
// gesture-based and stay fixed, never entered into this table at all).
//
// A button can be assigned to more than one action at once. That is not a
// bug to guard against: Minecraft's own control screen allows it and just
// marks the clash, rather than refusing the press, and controls.c does the
// same - conflicting rows are shown in red, not blocked. What is guarded is
// that every action always has some button, never none: the table is
// initialised to a default assignment and every setter writes a valid slot,
// so no action can ever end up with nothing bound to it at all.
//
// B is the one button permanently reserved outside this guarantee's normal
// path: controlsPollListen() (controls.c) treats a physical B press as an
// unconditional cancel of the Controls page's listening state, regardless
// of what B itself is currently assigned to. That is what keeps a player
// who opens listening by mistake - or has ended up with a heavily
// conflicted layout - always able to get back out. The trade-off is that B
// can never be *assigned* to an action through listening (only loaded as a
// value, e.g. from a save file or Reset-to-defaults) - see controls.c.
typedef enum
{
	REMAP_ACTION_RESET = 0,   ///< normally A - reset the workbench view
	REMAP_ACTION_BACK,        ///< normally B - back to the current step
	REMAP_ACTION_PHOTO_MODE,  ///< normally Y - toggle photo mode
	REMAP_ACTION_NEXT_ANGLE,  ///< normally X - next photo-mode angle
	REMAP_ACTION_ZOOM_OUT,    ///< normally L - zoom the camera out
	REMAP_ACTION_ZOOM_IN,     ///< normally R - zoom the camera in
	REMAP_ACTION_SCREENSHOT,  ///< normally D-Pad Up - save a screenshot
	REMAP_ACTION_POSE,        ///< normally D-Pad Down - pose the model
	REMAP_ACTION_PAGE_LEFT,   ///< normally D-Pad Left - page the manual back
	REMAP_ACTION_PAGE_RIGHT,  ///< normally D-Pad Right - page the manual forward
	REMAP_ACTION_PAUSE,       ///< normally SELECT - open the pause menu
	REMAP_ACTION_CLOSE,       ///< normally START - close the game
	REMAP_ACTION_COUNT,
} settingsRemapAction;

typedef enum
{
	REMAP_KEY_A = 0,
	REMAP_KEY_B,
	REMAP_KEY_X,
	REMAP_KEY_Y,
	REMAP_KEY_L,
	REMAP_KEY_R,
	REMAP_KEY_DUP,
	REMAP_KEY_DDOWN,
	REMAP_KEY_DLEFT,
	REMAP_KEY_DRIGHT,
	REMAP_KEY_SELECT,
	REMAP_KEY_START,
	REMAP_KEY_COUNT,
} settingsRemapKey;

// Which physical button currently fires the given action. Always returns a
// value in 0..REMAP_KEY_COUNT-1, even straight after a fresh install or a
// corrupt save - settingsLoad() clamps this the same way it clamps volume.
settingsRemapKey settingsRemapButton(settingsRemapAction action);
void settingsSetRemapButton(settingsRemapAction action, settingsRemapKey key);

// Puts all twelve actions back on their shipped buttons. The Controls
// page's own Reset row calls this rather than twelve separate
// settingsSetRemapButton calls, so "reset" can never leave some actions
// changed and others not.
void settingsResetRemap(void);

// Item 50 - localisation. Persists which of strings.h's gameLanguage values
// is active. Stored as a plain int here rather than pulling in strings.h's
// enum, so settings.c does not have to know that header exists; strings.c
// is what interprets the value and clamps it against LANG_COUNT.
//
// -1 means no language has ever been chosen - a fresh install, or a save
// written before this setting existed. strings.c reads that as "ask the
// console what it is set to", not as "English"; settingsLoad() leaves it at
// -1 rather than guessing, so that one-time detection still runs on a
// player's first boot after this update.
int settingsLanguage(void);
void settingsSetLanguage(int lang);

// Reads the settings file. Safe to call before anything is set; a missing or
// stale file just leaves the defaults in place.
void settingsLoad(void);

// Writes the settings file. Returns false if the card could not be written.
bool settingsSave(void);

// Writes only if something has actually changed since the last write, so the
// caller can put it in the frame loop without hitting the card every frame. The
// setters are what mark the change, which means a new setting added later cannot
// forget to be saved - it is saved by virtue of having a setter.
//
// Returns false only for a write that was attempted and failed. A failure does
// not queue a retry: the pending flag is cleared either way, because a card that
// refuses one write will refuse the next sixty this second too, and the caller
// is told once rather than sixty times. settingsSaveFailed() remembers it for
// the report on the way out.
bool settingsFlush(void);

// Whether any settingsFlush or settingsSave this session was refused by the
// card. Latched, so a failure early on is still reported at exit.
bool settingsSaveFailed(void);
