// One lookup for every user-facing English string in the files this table
// covers (title.c, controls.c), keyed by an enum instead of being typed
// inline wherever it is used.
//
// Why: a string typed into a printf or a C2D_TextParse call cannot be found
// by anything that is not reading that exact line of source. A second
// language means either hunting every one of those lines by eye - which
// misses the one that got missed - or having exactly one place that knows
// what the game says, in every language it says it in. This is that place.
//
// A language is a plain array of const char*, one slot per stringId, and
// STR(id) reads the caller's currently active language and falls back to
// English for a slot a partial translation left empty (NULL or ""), so a
// half-finished language degrades to English sentence by sentence rather
// than leaving a blank plaque on screen.
//
// main.c's own strings are NOT in this table - this file cannot be edited
// there, so those are listed for the record in the completion report
// instead of being wired up.

#pragma once

// Every language this build ships, in the order STRINGS_TABLE lists their
// columns. Adding a language means: add it here, add its column to every
// row in strings.c, and add it to languageName()/CFGU mapping. Nothing else
// has to change - every caller asks STR() for the active one.
typedef enum
{
	LANG_ENGLISH = 0,
	LANG_FRENCH,
	LANG_COUNT,
} gameLanguage;

typedef enum
{
	// Title page
	STR_TITLE,
	STR_TAGLINE,
	STR_PLAY,
	STR_OPTIONS,
	STR_QUIT,

	// The level select's top-screen preview - the kit the highlighted tile
	// holds, turning on the spot above the grid it was picked from.
	STR_PRE_LOCKED,      // shown on the top screen over an unbuilt kit's outline
	STR_PRE_PARTS,       // "%d parts"
	STR_PRE_RUNNERS,     // "%d runners"
	STR_PRE_RUNNER_1,    // the singular of the above - most kits are on one runner

	// Options page (front end)
	STR_VOLUME,
	STR_MINUS,
	STR_PLUS,
	STR_CONTROLS,
	STR_BACK,
	STR_VOL_NOTE,
	STR_LANGUAGE,
	STR_RESET,

	// Update page
	STR_UPD_LINE1,
	STR_UPD_LINE2,
	STR_UPD_CHECK,
	STR_UPD_GET,
	STR_UPD_RESTART,
	STR_UPD_OFF,
	STR_UPD_SHORT_CHECKING,
	STR_UPD_SHORT_UP_TO_DATE,
	STR_UPD_SHORT_AVAILABLE,
	STR_UPD_SHORT_DOWNLOADING,
	STR_UPD_SHORT_INSTALLING,
	STR_UPD_SHORT_DONE,
	STR_UPD_SHORT_FAILED,
	STR_UPD_SHORT_READY,
	STR_UPD_INSTALLED_ROW,  // "Installed  %s" - touch screen, not the console's STR_C_INSTALLED_LINE
	STR_UPD_NEWEST_ROW,     // "Newest     %s"

	// Level select
	STR_PREV,
	STR_NEXT,
	STR_BUILT_TAG,

	// Controls page - remap prompts
	STR_LISTENING,
	STR_CANCEL_HINT,

	// Controls table (button, action) - see controls.c
	STR_CTL_TAP_KEY,     STR_CTL_TAP_ACT,
	STR_CTL_RUB_KEY,     STR_CTL_RUB_ACT,
	STR_CTL_DRAG_KEY,    STR_CTL_DRAG_ACT,
	STR_CTL_DPADLR_KEY,  STR_CTL_DPADLR_ACT,
	STR_CTL_B_KEY,       STR_CTL_B_ACT,
	STR_CTL_LR_KEY,      STR_CTL_LR_ACT,
	STR_CTL_A_KEY,       STR_CTL_A_ACT,
	STR_CTL_YX_KEY,      STR_CTL_YX_ACT,
	STR_CTL_DDOWN_KEY,   STR_CTL_DDOWN_ACT,
	STR_CTL_DUP_KEY,     STR_CTL_DUP_ACT,
	STR_CTL_SELECT_KEY,  STR_CTL_SELECT_ACT,
	STR_CTL_START_KEY,   STR_CTL_START_ACT,

	// Per-key short labels, one per settingsRemapKey value (controls.c,
	// settings.h) - item 37's widened remap. Distinct from the STR_CTL_*_KEY
	// set above: those are this table's own row text (some of them joint,
	// e.g. "L / R"); these are always a single physical button's own name,
	// used to redraw whichever key an action is *currently* bound to,
	// wherever on the Controls page that needs showing. Order matches
	// settingsRemapKey exactly - controls.c indexes this array straight off
	// the enum, so a key added there needs a row added here in the same slot.
	STR_KEY_A, STR_KEY_B, STR_KEY_X, STR_KEY_Y, STR_KEY_L, STR_KEY_R,
	STR_KEY_DUP, STR_KEY_DDOWN, STR_KEY_DLEFT, STR_KEY_DRIGHT,
	STR_KEY_SELECT, STR_KEY_START,

	// Console (top screen), by page. %-specifiers are part of the string and
	// must stay identical - same count, same order, same conversion - across
	// every language's column in strings.c, because the call site fills them
	// in after STR() has already chosen which sentence they land in.
	STR_C_LEVELS_HDR,        // "Model Kit  -  levels %d to %d\n"
	STR_C_LEVEL_ROW,         // "  Level %2d   %2d parts   %s\n"
	STR_C_BUILT_TOTAL,       // "  Built %d of %d.\n"
	STR_C_LEVELS_HELP1,
	STR_C_LEVELS_HELP2,

	STR_C_OPTIONS_HDR,
	STR_C_VOLUME_LINE,       // "Master volume : %d%%\n"
	STR_C_OPTIONS_HELP1,
	STR_C_OPTIONS_HELP2,
	STR_C_OPTIONS_HELP3,

	STR_C_UPDATE_HDR,
	STR_C_INSTALLED_LINE,    // "Installed version : %s\n"
	STR_C_NEWEST_LINE,       // "Newest release    : %s\n"
	STR_C_UPD_UNAVAILABLE1,
	STR_C_UPD_UNAVAILABLE2,
	STR_C_UPD_UNAVAILABLE3,
	STR_C_UPD_PROGRESS,      // "  %d%%\n\n"
	STR_C_UPD_HELP_AVAILABLE1,
	STR_C_UPD_HELP_AVAILABLE2,
	STR_C_UPD_HELP_BUSY1,
	STR_C_UPD_HELP_BUSY2,
	STR_C_UPD_HELP_DONE,
	STR_C_UPD_HELP_DEFAULT,
	STR_C_UPD_NOT_SET,

	STR_C_CONTROLS_HDR,
	STR_C_CONTROLS_ROW,      // "  %-18s %s\n"
	STR_C_CONTROLS_HELP,

	STR_C_TITLE_HDR,
	STR_C_TITLE_HELP1,
	STR_C_TITLE_HELP2,
	STR_C_TITLE_HELP3,
	STR_C_HARDWARE_LINE,     // "Hardware : %s\n"
	STR_C_HARDWARE_NEW,
	STR_C_HARDWARE_OLD,
	STR_C_TITLE_HELP4,
	STR_C_TITLE_ROW_PLAY,
	STR_C_TITLE_ROW_OPTIONS,
	STR_C_TITLE_ROW_QUIT,

	// main.c's remaining hardcoded strings (item 50), added by the
	// main.c-owning agent together with their call sites. Same STR_C_*
	// console convention as above: STR_C_BEGINNER_* for the beginner
	// tutorial sheet (top screen, level 1 only), STR_C_SEAT_*/STR_C_STATUS_*
	// for the workbench seat/status line (seatMsg, printed into the "Fits"
	// row of the console's live block), STR_C_PHOTO_* for the photo-mode
	// console block, STR_C_MANUAL_* for the per-level manual header/guide
	// line.
	STR_C_BEGINNER_HEADER,       // "FIRST BUILD - ONE STEP AT A TIME"
	STR_C_BEGINNER_STEP,         // "STEP %d OF 4"
	STR_C_BEGINNER_DONE,         // "DONE"
	STR_C_BEGINNER_TITLE_OPEN,   // "OPEN THE KIT BOX"
	STR_C_BEGINNER_TITLE_JOIN,   // "TAP THE SMALL JOIN"
	STR_C_BEGINNER_TITLE_RUB,    // "RUB THE LOOSE PART"
	STR_C_BEGINNER_TITLE_TAP,    // "TAP THE AMBER SHAPE"
	STR_C_BEGINNER_SUB_OPEN,     // "Tap the red mark on the lid to lift it off."
	STR_C_BEGINNER_SUB_JOIN,     // "Tap the red dot on the small gate."
	STR_C_BEGINNER_SUB_RUB,      // "Keep rubbing until the nub is smooth."
	STR_C_BEGINNER_SUB_TAP,      // "The part clicks into its final place."
	STR_C_BEGINNER_KIT_CAPTION,  // "%s  -  BOX / RUNNER %d OF %d"
	STR_C_BEGINNER_STYLUS_HINT,  // "Use the stylus on the bottom screen."
	STR_C_BEGINNER_COMPLETE,     // "COMPLETE THIS STEP TO CONTINUE"

	STR_C_SEAT_NONE,             // "- - -"
	STR_C_SEAT_ON_RUNNER,        // "still on the runner"
	STR_C_SEAT_FITTED_TAP_REMOVE,// "fitted - tap to remove"
	STR_C_SEAT_FILE_FIRST,       // "%s - file the nub first"
	STR_C_SEAT_TAP_SOCKET,       // "%s - tap the socket"
	STR_C_SEAT_ALREADY_FITTED,   // "%s is already fitted"
	STR_C_SEAT_TAKES,            // "%s takes %s"
	STR_C_SEAT_NEEDS_FIRST,      // "%s needs %s first"
	STR_C_SEAT_SNIP_FIRST,       // "%s - snip it off first"
	STR_C_SEAT_FITTED,           // "fitted - %s"
	STR_C_SEAT_HOLDS,            // "%s holds %s"
	STR_C_SEAT_TAKEN_OFF,        // "taken off - %s"
	STR_C_STATUS_POSE_HINT,      // "%s - L/R to pose"
	STR_C_STATUS_TAP_MOVES,      // "tap a part that moves"
	STR_C_STATUS_RAM,            // "RAM %u%%"

	// How far into the kit the player is. It labels the step bar at the top of
	// the top screen now - see tutorialDrawStepBar in tutorial_art.c - having
	// started life on a paper tag in the corner of the workbench. The counts
	// string that used to sit under it on that tag is gone: the tutorial sheet
	// has always shown snipped, filed and fitted against their totals on the
	// right of pages 2, 3 and 4, so it was a second copy of a figure already on
	// the same screen. Kept as a format string so the French words are the same
	// length problem as any other translation rather than a special case.
	STR_C_BENCH_STEP,            // "STEP %d/%d"

	// One-step undo. The label sits on the bench button; the other two are
	// status-line answers, and they go through the same seatMsg line every
	// other thing a tap does reports on, so undo reads as one more thing the
	// bench can tell you rather than as a system message.
	STR_C_UNDO_LABEL,            // "UNDO"
	STR_C_UNDO_DONE,             // "undone"
	STR_C_UNDO_NOTHING,          // "nothing to undo"

	STR_C_PHOTO_HEADER,          // "\n  Photo mode\n\n"
	STR_C_PHOTO_NEXT,            // "    X   next angle\n"
	STR_C_PHOTO_BACK,            // "    Y   back to the build\n"

	STR_C_MANUAL_GUIDE,          // "  Snip the stub, file the nub, tap the ghost.\n"
	STR_C_MANUAL_HEADER,         // "Model Kit  -  build manual       level %2d\n"

	// Tutorial art sheet (top screen, four-step illustrated draw-call
	// tutorial) - tutorial_art.c. STR_T_* to keep this block distinct from
	// the STR_C_BEGINNER_* console block above, which is a different render
	// path (console text vs citro2d shapes) covering the same four steps.
	//
	// The sheet's own "STEP n" / "OF 4" pair used to live here, labelling the
	// row of four pips that counted the four tutorial pages. That row counts
	// build steps now and is labelled with STR_C_BENCH_STEP above, so both are
	// gone rather than left translated and unreachable.
	STR_T_OPEN_TITLE,     // "OPEN THE BOX"
	STR_T_OPEN_SUB,       // "Touch the red dot on the lid."
	STR_T_OPEN_LIFTS,     // "it lifts off"
	STR_T_SNIP_COUNT,     // "%d of %d done"
	STR_T_SNIP_TITLE,     // "SNIP EVERY PART OFF"
	STR_T_SNIP_SUB,       // "Touch the red dot on the little bridge."
	STR_T_SNIP_BRIDGE,    // "this bridge holds it on"
	STR_T_SNIP_FREE,      // "it comes free"
	STR_T_SNIP_ALL_TEN,   // "DO ALL TEN:"
	STR_T_FILE_PCT,       // "this part %d%%"
	STR_T_FILE_TITLE,     // "RUB THE BUMP FLAT"
	STR_T_FILE_SUB,       // "Slide the stylus side to side across the bump."
	STR_T_FILE_BUMP,      // "the bump"
	STR_T_FILE_SMOOTH,    // "flat and smooth"
	STR_T_FILE_KEEP,      // "keep rubbing"
	STR_T_FIT_COUNT,      // "%d of %d built"
	STR_T_FIT_TITLE,      // "PUT IT WHERE IT GLOWS"
	STR_T_FIT_SUB,        // "Touch the orange see-through shape."
	STR_T_FIT_HOLDING,    // "you are holding"

	STR_COUNT,
} stringId;

// Which language is active. Backed by settings.c so it survives a reboot;
// read this rather than settingsLanguage() directly so a caller never has
// to know the setting exists.
gameLanguage languageCurrent(void);

// Switches the active language and marks it for saving. Does not reparse any
// C2D_Text already on screen - the caller (title.c) has to do that, because
// this file does not know a C2D_TextBuf exists.
void languageSet(gameLanguage lang);

// A language's own name, always written in that language ("English",
// "Francais") rather than in whatever language is currently active, so a
// player who cannot read the active language can still find their own in
// the list.
const char* languageName(gameLanguage lang);

// Asks the console what language it is set to and returns the closest one
// this game ships, or LANG_ENGLISH if the console's language is not one of
// them. Safe to call once at boot before settings has decided anything -
// it opens and closes the cfg service itself.
gameLanguage languageDetectSystem(void);

// The string for id, in the currently active language. Falls back to the
// English column for any slot a language leaves empty, and falls back to a
// visible marker (never NULL) if id itself is out of range, so a bad id is a
// garbled line on screen rather than a crash.
const char* STR(stringId id);
