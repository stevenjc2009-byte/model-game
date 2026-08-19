// Every TEST_CAPTURE_*/TEST_*_AUDIT compile flag in one place, instead of
// scattered through the top of main.c.
//
// Every flag defaults to 0 - a shipped build never has to know this header
// exists. Enabling one temporarily patches game boot to serve one
// verification pass rather than play normally; enabling more than one at
// once is not supported and behaviour is undefined.
//
// Each is wrapped in #ifndef/#define/#endif rather than a plain #define so
// a build can override one from the Makefile's CFLAGS (-DTEST_CAPTURE_LEVEL=1)
// without a macro-redefinition error.
//
// tools/regress.py reads this file to find the flags it can run, by exact
// text - "#define NAME VALUE" on its own line, nothing reformatted - so it
// never has to parse C. Keep that form if you touch this file: same names,
// same default values, same one-flag-per-line layout. It switches a flag on
// for one build with make HARNESS=-DNAME=1 rather than editing anything here.

#pragma once

// Boots straight into level TEST_CAPTURE_LEVEL (1-based) instead of the menu.
// Leave at 0 for the shipped build; a capture pass sets it to skip menu
// navigation the emulator's key mapping can't drive.
#ifndef TEST_CAPTURE_LEVEL
#define TEST_CAPTURE_LEVEL 0
#endif
// Opens the in-bench options menu (PAGE_OPTIONS) on boot, for capturing a
// menu screen without navigating there by hand.
#ifndef TEST_CAPTURE_MENU
#define TEST_CAPTURE_MENU 0
#endif
// Skips straight to every part cut, filed, and seated - the finished model -
// rather than the kit's starting state.
#ifndef TEST_CAPTURE_ASSEMBLED
#define TEST_CAPTURE_ASSEMBLED 0
#endif
// Starts with the kit box already open, runner on the bench.
#ifndef TEST_CAPTURE_OPEN
#define TEST_CAPTURE_OPEN 0
#endif
// Starts with exactly one part already cut free, to capture the loose-part
// and gate-picking states without cutting through the whole runner by hand.
#ifndef TEST_CAPTURE_CUT_ONE
#define TEST_CAPTURE_CUT_ONE 0
#endif
// Which runner (1-based) TEST_CAPTURE_CUT_ONE and TEST_CAPTURE_ASSEMBLED
// treat as already reached; earlier runners are marked fully cut.
#ifndef TEST_CAPTURE_RUNNER
#define TEST_CAPTURE_RUNNER 0
#endif
// Hides the on-screen manual/guide text for a clean capture of the model
// alone - see photoMode in camera.h for the same idea at play time.
#ifndef TEST_CAPTURE_HIDE_GUIDE
#define TEST_CAPTURE_HIDE_GUIDE 0
#endif
// Selects one of the authored room-tour camera presets (see main.c) instead
// of the game's own framing, for auditing the bedroom shell rather than the
// bench.
#ifndef TEST_CAPTURE_VIEW
#define TEST_CAPTURE_VIEW 0
#endif
// Runs meshValidateKits() against every kit in the game rather than just the
// one being booted into, printing a pass/fail per kit.
#ifndef TEST_AUDIT_ALL_KITS
#define TEST_AUDIT_ALL_KITS 0
#endif
// Runs runCameraIdleAudit(): checks the camera settles and stops drifting in
// each of the closed/runner/assembly framings.
#ifndef TEST_CAMERA_IDLE_AUDIT
#define TEST_CAMERA_IDLE_AUDIT 0
#endif
// Runs runLevel1WorkspaceAudit(): checks every part and socket in level 1
// stays within the bench's playable footprint through cutting, filing and
// seating.
#ifndef TEST_LEVEL1_WORKSPACE_AUDIT
#define TEST_LEVEL1_WORKSPACE_AUDIT 0
#endif
// Runs runCameraPanAudit(): checks the Circle Pad slide moves the view the
// right way and clamps at the ends of the bench.
#ifndef TEST_CAMERA_PAN_AUDIT
#define TEST_CAMERA_PAN_AUDIT 0
#endif
// Runs runCeilingAudit(): checks the room ceiling hides correctly for both a
// normal camera pose and the highest legal manual orbit.
#ifndef TEST_CEILING_AUDIT
#define TEST_CEILING_AUDIT 0
#endif
// Runs meshCollisionAuditAllKits() (mesh.c): checks every kit's authored
// geometry for overlaps that would leave two parts occupying the same
// space.
#ifndef TEST_COLLISION_AUDIT
#define TEST_COLLISION_AUDIT 0
#endif
// Item 35: forces pose mode and sets one part's pose angle directly, to
// capture a posed joint at a chosen angle without driving L/R interactively
// (Azahar's key mapping can't reach it - see mesh.c's jointDefs). 1-based
// like TEST_CAPTURE_RUNNER; 0 = off, so TEST_POSE_DEG is inert with it.
#ifndef TEST_POSE_PART
#define TEST_POSE_PART 0
#endif
// Degrees to set TEST_POSE_PART's pose angle to; meaningless while
// TEST_POSE_PART is 0.
#ifndef TEST_POSE_DEG
#define TEST_POSE_DEG 0
#endif
// Item 34: paints the whole texture atlas with a smooth two-axis colour
// gradient (red rising left-to-right, green rising top-to-bottom) instead of
// the real WHITE/GRAIN/TREAD/PANEL regions, and maps that whole gradient
// across the kit-box art panel - the largest flat on-screen surface - rather
// than just its normal GRAIN sub-rectangle. A correct PICA200 8x8-tile
// swizzle shows a perfectly smooth diagonal gradient on that panel; a wrong
// one shows visible tearing/blockiness at 8-texel boundaries. For verifying
// the swizzle by eye, not by reasoning, before trusting it with real content.
#ifndef TEST_ATLAS_GRADIENT
#define TEST_ATLAS_GRADIENT 0
#endif
// Runs runHintAudit(): checks the tutorial's breathing dot lands on the piece
// each step is actually talking about, by feeding the dot's own screen position
// back into the pick function a stylus tap goes through. Console output only.
#ifndef TEST_HINT_AUDIT
#define TEST_HINT_AUDIT 0
#endif
// Walks all twenty kits through all four tutorial steps and writes the bottom
// screen to sdmc:/modelkit/shot_NNN.bmp at each one - eighty shots, in level
// order, four per level, in step order. The visual counterpart to
// TEST_HINT_AUDIT: that one measures where the dot lands, this one shows it.
// The game quits itself once the last shot is written.
#ifndef TEST_HINT_TOUR
#define TEST_HINT_TOUR 0
#endif
// Item 32: exercises the paint-override state machine (cycle order, palette
// mapping, save round-trip, and the pre-item-32 legacy-save migration)
// directly against the real gameplay functions, the same way
// TEST_LEVEL1_WORKSPACE_AUDIT exercises cutting/filing/seating. Console
// output only - no 3D pass involved, so it is unaffected by Azahar's
// completion-update quirk that TEST_CAPTURE_ASSEMBLED works around.
#ifndef TEST_PAINT_AUDIT
#define TEST_PAINT_AUDIT 0
#endif
// Walks all twenty levels through the pause menu's own Save and Load rows -
// menuSaveNow()/menuLoadNow(), the exact functions the rows call - doing real
// work on the bench first, wiping RAM in between, and comparing what comes back
// byte for byte. Also covers the two paths a hand test cannot reach: loading
// with nothing on the card, and a save the card refuses. Console output only.
#ifndef TEST_SAVELOAD_AUDIT
#define TEST_SAVELOAD_AUDIT 0
#endif
// The visual counterpart to TEST_SAVELOAD_AUDIT, in the same relationship
// TEST_HINT_TOUR has to TEST_HINT_AUDIT: that one proves the Save and Load rows
// work on all twenty kits, this one shows what the player is looking at while
// they do. Walks the twenty levels with the pause menu open on the Load row and
// a real "LOADED - <kit name>" note under it - the longest line that note ever
// carries - holding each level two seconds so a capture can catch it. The pause
// menu renders to the TOP screen, which screenshotCapture does not reach, so the
// shots are taken of the emulator window from outside rather than written to the
// card.
#ifndef TEST_SAVELOAD_TOUR
#define TEST_SAVELOAD_TOUR 0
#endif
// Drives the one-step undo against the four actions that take a snapshot -
// cutPart, fileStroke, seatPart, unseatPart - on every kit in the game, and
// checks the state that comes back matches what was there before the action
// byte for byte. Also covers the three answers a hand test at the stylus cannot
// reach reliably: a second undo in a row must refuse, an action the bench
// REFUSED must not have spent the slot, and leaving the level must clear it.
// Console output only, and it never touches the card - undo is entirely in RAM.
#ifndef TEST_UNDO_AUDIT
#define TEST_UNDO_AUDIT 0
#endif
// Checks that the front-end console never writes the single-buffered top screen
// while the level select's 3D preview owns it, in either direction of the
// transition - arriving at the level select, and leaving it.
//
// This one exists because the fault it covers cannot be seen here at all. The
// top screen has one framebuffer and two writers; when they collide the result
// is half console, half 3D, and it stays that way for the session. Real hardware
// has an asynchronous transfer engine so it happens there, and an emulator
// finishes the transfer inside the same step so it never does. Screenshots
// therefore prove nothing either way.
//
// So the audit does not look at the screen. It counts console writes - see
// titleConsoleWrites() - and asserts they are zero across the frames where the
// preview owns that memory. Console output only; nothing is drawn and the card
// is not touched.
#ifndef TEST_TOPSCREEN_AUDIT
#define TEST_TOPSCREEN_AUDIT 0
#endif
// Runs runWorkZoomAudit(): checks the camera leans in on a small part while it
// is being filed, gives the distance back exactly when the filing is done, and
// leaves everything else - a large part, photo mode - alone. Also prints the
// largest and smallest part of all twenty kits, which is what the rule for
// "small" is judged against.
#ifndef TEST_WORKZOOM_AUDIT
#define TEST_WORKZOOM_AUDIT 0
#endif

// True when any flag above is on - which is to say, when this build is a
// verification pass rather than the game.
//
// Every one of them fabricates game state: a kit already cut, a part already
// filed, twenty levels walked through in eighty frames. main.c folds the open
// build back into levelBuilds every frame and writes it to the card on the way
// out, so a verification build that shares an SD card with a real save hands
// the player back kits somebody else already snipped apart. That happened on
// 2026-08-18 - a tour of all twenty kits left every one of them showing its
// parts cut - and nothing inside the game can tell a save written by a test
// from one written by playing.
//
// main.c uses this to leave the card alone entirely while any flag is on,
// progress and settings both. A verification pass has nothing worth keeping in
// it by definition: it exists to be looked at once and thrown away.
#define TEST_ANY_HARNESS ( \
	TEST_CAPTURE_LEVEL || TEST_CAPTURE_MENU || TEST_CAPTURE_ASSEMBLED || \
	TEST_CAPTURE_OPEN || TEST_CAPTURE_CUT_ONE || TEST_CAPTURE_RUNNER || \
	TEST_CAPTURE_HIDE_GUIDE || TEST_CAPTURE_VIEW || TEST_AUDIT_ALL_KITS || \
	TEST_CAMERA_IDLE_AUDIT || TEST_LEVEL1_WORKSPACE_AUDIT || \
	TEST_CAMERA_PAN_AUDIT || TEST_CEILING_AUDIT || TEST_COLLISION_AUDIT || \
	TEST_POSE_PART || TEST_ATLAS_GRADIENT || TEST_HINT_AUDIT || \
	TEST_HINT_TOUR || TEST_PAINT_AUDIT || TEST_SAVELOAD_AUDIT || \
	TEST_SAVELOAD_TOUR || TEST_UNDO_AUDIT || TEST_TOPSCREEN_AUDIT || \
	TEST_WORKZOOM_AUDIT)

// The subset of the flags above that answer in words rather than in pictures.
//
// Each one runs a check and prints a verdict line, so a machine can read the
// result. The rest - the TEST_CAPTURE_* flags, the two tours, TEST_POSE_PART,
// TEST_ATLAS_GRADIENT - exist to put something on screen for a person to look
// at, and no amount of parsing turns a screenshot into a pass or a fail. They
// are deliberately not in here.
//
// tools/regress.py builds and runs exactly this list. When a new audit is added
// it goes in here too, or the harness will not know it exists.
#define TEST_ANY_AUDIT ( \
	TEST_AUDIT_ALL_KITS || TEST_CAMERA_IDLE_AUDIT || \
	TEST_LEVEL1_WORKSPACE_AUDIT || TEST_CAMERA_PAN_AUDIT || \
	TEST_CEILING_AUDIT || TEST_COLLISION_AUDIT || TEST_HINT_AUDIT || \
	TEST_PAINT_AUDIT || TEST_SAVELOAD_AUDIT || TEST_UNDO_AUDIT || \
	TEST_TOPSCREEN_AUDIT || TEST_WORKZOOM_AUDIT)
