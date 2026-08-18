#include "settings.h"
#include "save.h"

#include <string.h>

// Layout id for the settings file. This must change whenever the SettingsBlob
// struct changes - a size or layout mismatch will refuse a stale file and start
// fresh with defaults rather than reading garbage. Do not bump unless the struct
// itself is edited; adding a new function does not require it.
//
// Bumped to 2 for item 37 (the remap table) and item 50 (language) landing
// together - a save from layout 1 does not have either field, so rather than
// read three bytes of whatever used to be past the end of the old struct, a
// stale file is treated as missing and every new field starts at its
// documented default, same as a fresh install.
//
// Bumped again to 3 when item 37's remap widened from 3 actions/3 keys to
// 12/12 (coordinator direction, same day) - remapButton[] grew from
// REMAP_ACTION_COUNT==3 to ==12, so a layout-2 file is the wrong size and
// would either be rejected outright or read nine bytes of garbage past its
// old end. Layout 3 makes that file "missing" instead, same reasoning as
// the bump above: every action starts at its documented shipped default.
//
// Bumped again to 4 for tutorialRetired - the flag recording that the tutorial
// has already switched itself off once. It has to be stored, not derived: "has
// level 1 ever been finished" is the whole question, and a layout-3 file is one
// byte short of answering it. A player upgrading from layout 3 is treated as a
// fresh install here, which means the tutorial comes back on for one more run
// of level 1 and then retires itself - the safe direction to be wrong in.
#define SETTINGS_LAYOUT 4u

typedef struct
{
	int volume;
	bool showTutorial;
	u8   remapButton[REMAP_ACTION_COUNT];
	int  language;
	bool tutorialRetired;
} SettingsBlob;

static int masterVolume = 100;
static bool showTutorial = true;

// Dead weight kept deliberately. It recorded that the tutorial had switched
// itself off after level 1; nothing sets it and nothing reads it any more, and
// the two functions that did are gone. The field stays in SettingsBlob, and this
// variable stays to round-trip it, only so the file layout does not change:
// bumping SETTINGS_LAYOUT to drop one byte would make every existing settings
// file the wrong size and hand the player back a default volume, default
// language and default controls to save nothing.
static bool tutorialRetired = false;

// Default assignment: each action on the button it has always fired on -
// a straight identity mapping onto main.c's own real key checks (reset =
// KEY_A at main.c:3117, back = KEY_B at :3296, and so on), so a fresh
// install plays exactly as it always has. settingsResetRemap() and a
// fresh install both start here.
static u8 remapButton[REMAP_ACTION_COUNT] =
{
	[REMAP_ACTION_RESET]      = REMAP_KEY_A,
	[REMAP_ACTION_BACK]       = REMAP_KEY_B,
	[REMAP_ACTION_PHOTO_MODE] = REMAP_KEY_Y,
	[REMAP_ACTION_NEXT_ANGLE] = REMAP_KEY_X,
	[REMAP_ACTION_ZOOM_OUT]   = REMAP_KEY_L,
	[REMAP_ACTION_ZOOM_IN]    = REMAP_KEY_R,
	[REMAP_ACTION_SCREENSHOT] = REMAP_KEY_DUP,
	[REMAP_ACTION_POSE]       = REMAP_KEY_DDOWN,
	[REMAP_ACTION_PAGE_LEFT]  = REMAP_KEY_DLEFT,
	[REMAP_ACTION_PAGE_RIGHT] = REMAP_KEY_DRIGHT,
	[REMAP_ACTION_PAUSE]      = REMAP_KEY_SELECT,
	[REMAP_ACTION_CLOSE]      = REMAP_KEY_START,
};

// -1 - not yet chosen. See settings.h: strings.c reads this as "ask the
// console", not as "English", and only does so once.
static int language = -1;

// Set by the setters, cleared by settingsFlush. Starts false: the defaults above
// are not worth a write, and a first run that never opens Options should leave
// the card alone.
static bool pending = false;
static bool saveFailed = false;

int settingsVolume(void)
{
	return masterVolume;
}

void settingsVolumeStep(int delta)
{
	int before = masterVolume;
	masterVolume += delta;
	if (masterVolume < 0)   masterVolume = 0;
	if (masterVolume > 100) masterVolume = 100;
	// Only a real move counts. Winding into the end stop repeats the same value,
	// and marking that dirty would write the card on every press of a control
	// that is doing nothing.
	if (masterVolume != before) pending = true;
}

bool settingsShowTutorial(void)
{
	return showTutorial;
}

void settingsSetShowTutorial(bool on)
{
	if (showTutorial != on) pending = true;
	showTutorial = on;
}

// Derived from the remap table rather than its own field (see settings.h):
// "swapped" means pause-menu is currently on START instead of its default
// SELECT. This stays well-defined no matter what the Controls page has done
// to reset-view, because it only ever looks at the pause/close pair.
bool settingsSwapStartSelect(void)
{
	return remapButton[REMAP_ACTION_PAUSE] == REMAP_KEY_START;
}

// A two-state preset: forces pause-menu and close-game onto the requested
// pair of buttons and leaves reset-view exactly where it was. Whatever the
// Controls page has done since the last time this ran is overwritten for
// just these two actions - presets are allowed to do that; it is what
// choosing one means.
void settingsSetSwapStartSelect(bool on)
{
	settingsRemapKey newPause = on ? REMAP_KEY_START : REMAP_KEY_SELECT;
	settingsRemapKey newClose = on ? REMAP_KEY_SELECT : REMAP_KEY_START;
	settingsSetRemapButton(REMAP_ACTION_PAUSE, newPause);
	settingsSetRemapButton(REMAP_ACTION_CLOSE, newClose);
}

settingsRemapKey settingsRemapButton(settingsRemapAction action)
{
	if (action < 0 || action >= REMAP_ACTION_COUNT) return REMAP_KEY_A;
	return (settingsRemapKey)remapButton[action];
}

void settingsSetRemapButton(settingsRemapAction action, settingsRemapKey key)
{
	if (action < 0 || action >= REMAP_ACTION_COUNT) return;
	if (key < 0 || key >= REMAP_KEY_COUNT) return;
	if (remapButton[action] != (u8)key) pending = true;
	remapButton[action] = (u8)key;
}

void settingsResetRemap(void)
{
	static const u8 defaults[REMAP_ACTION_COUNT] =
	{
		[REMAP_ACTION_RESET]      = REMAP_KEY_A,
		[REMAP_ACTION_BACK]       = REMAP_KEY_B,
		[REMAP_ACTION_PHOTO_MODE] = REMAP_KEY_Y,
		[REMAP_ACTION_NEXT_ANGLE] = REMAP_KEY_X,
		[REMAP_ACTION_ZOOM_OUT]   = REMAP_KEY_L,
		[REMAP_ACTION_ZOOM_IN]    = REMAP_KEY_R,
		[REMAP_ACTION_SCREENSHOT] = REMAP_KEY_DUP,
		[REMAP_ACTION_POSE]       = REMAP_KEY_DDOWN,
		[REMAP_ACTION_PAGE_LEFT]  = REMAP_KEY_DLEFT,
		[REMAP_ACTION_PAGE_RIGHT] = REMAP_KEY_DRIGHT,
		[REMAP_ACTION_PAUSE]      = REMAP_KEY_SELECT,
		[REMAP_ACTION_CLOSE]      = REMAP_KEY_START,
	};
	if (memcmp(remapButton, defaults, sizeof(defaults)) != 0) pending = true;
	memcpy(remapButton, defaults, sizeof(defaults));
}

int settingsLanguage(void)
{
	return language;
}

void settingsSetLanguage(int lang)
{
	if (language != lang) pending = true;
	language = lang;
}

void settingsLoad(void)
{
	SettingsBlob blob;
	if (!saveReadSettingsBlob(&blob, sizeof(blob), SETTINGS_LAYOUT)) return;

	// Clamped on the way in as well as on the way out. The checksum makes a
	// corrupt file unlikely, but a file hand-edited on a PC would pass it, and a
	// volume of -400 would light the Options bar backwards rather than be
	// noticed. Everything here is authored by this file, so nothing is trusted
	// from it that has not been checked.
	masterVolume = blob.volume;
	if (masterVolume < 0)   masterVolume = 0;
	if (masterVolume > 100) masterVolume = 100;
	showTutorial = blob.showTutorial;
	tutorialRetired = blob.tutorialRetired;

	// Each slot clamped independently rather than rejecting the whole blob:
	// a file hand-edited to put one action out of range should not also
	// throw away the other two, or the language and volume alongside them.
	// Conflicts (two actions on the same button) are left alone - those are
	// a valid, if unusual, state here same as from the Controls page.
	for (int i = 0; i < REMAP_ACTION_COUNT; i++)
	{
		u8 v = blob.remapButton[i];
		remapButton[i] = (v < REMAP_KEY_COUNT) ? v : (u8)remapButton[i];
	}

	language = blob.language;

	// Loading is not a change. Marking it dirty here would write the file back
	// on the first frame of every boot for no reason.
	pending = false;

	// A save carrying the tutorial switched off is loaded switched off, whatever
	// switched it off. There was briefly a one-time undo here that turned it back
	// on for anyone whose save had the old automatic switch-off in it; it is gone
	// because turning a player's setting back on for them is not something a
	// patch gets to do, however good the reason. The Options page is the only
	// thing that moves this switch now, in either direction.
}

bool settingsSave(void)
{
	SettingsBlob blob;
	blob.volume = masterVolume;
	blob.showTutorial = showTutorial;
	memcpy(blob.remapButton, remapButton, sizeof(remapButton));
	blob.language = language;
	blob.tutorialRetired = tutorialRetired;
	bool ok = saveWriteSettingsBlob(&blob, sizeof(blob), SETTINGS_LAYOUT);
	if (!ok) saveFailed = true;
	return ok;
}

bool settingsFlush(void)
{
	if (!pending) return true;
	pending = false;
	return settingsSave();
}

bool settingsSaveFailed(void)
{
	return saveFailed;
}
