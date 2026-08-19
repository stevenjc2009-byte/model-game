// The in-app updater behind the Options page's "Check for Update" button.
//
// There is no update channel for homebrew - Nintendo's own only serves signed
// retail titles - so this is the whole mechanism, built from parts: ask GitHub
// what the newest release is, compare it to the version compiled into this
// binary, and if GitHub is ahead, stream the new .cia straight into the AM
// service and relaunch into it.
//
// Two things about that are worth knowing before reading updater.c.
//
// First, the console cannot verify github.com on its own. Its root certificate
// store predates every CA in use today, so a plain HTTPS request fails with a
// TLS error that no amount of retrying will fix. The fix is romfs:/cacert.pem,
// the Mozilla bundle, handed to libcurl explicitly. That file is the reason
// this project has a RomFs at all.
//
// Second, the download runs on its own thread. 3DS wifi is slow enough that a
// synchronous fetch would lock the front end for the better part of a minute,
// which reads as a crash. The main loop polls updaterState() and paints; the
// worker does the waiting.

#pragma once

#include <3ds.h>
#include <stdbool.h>

// The version this binary believes it is.
//
// It is compared numerically against the newest release tag on GitHub, with a
// leading "v" tolerated on either side, so a tag of "v0.2.1" and a value here
// of "0.2.1" are the same version. Bump this in the same commit that cuts the
// release, or the shipped build will offer itself an update forever.
//
// Left blank between releases on purpose. While it is blank the updater refuses
// to compare - it reports that the build has no version rather than treating an
// unset value as 0.0.0, which would make every release on GitHub look newer and
// offer a pointless update on every check. Filling it in is the only thing
// needed to switch the check back on, and it is filled in by the same commit
// that cuts the release so the shipped build cannot lie about itself.
#define MODELKIT_VERSION "0.4.5"

// Whether the line above has been filled in. Everything that prints or compares
// the version goes through this so there is one answer to "do we know".
#define MODELKIT_VERSION_SET (MODELKIT_VERSION[0] != '\0')

// Where to ask. Kept here rather than buried in the .c so that forking the
// repo is a one-line change.
#define MODELKIT_REPO_OWNER "stevenjc2009-byte"
#define MODELKIT_REPO_NAME  "model-game"

// Releases name their asset with the version on the end - modelkit0.2.1.cia and
// so on - so there is no single filename to ask for. The updater does not need
// one: it reads the real download URL out of the API response and takes whatever
// .cia the release actually carries, whatever it is called.
//
// This is only the last-resort fallback, used if the API answers with something
// that cannot be parsed. It resolves only if a release also carries a copy under
// this fixed name, which is the same copy the install QR code points at.
#define MODELKIT_CIA_ASSET  "modelkit.cia"

// Where the updater has got to. The front end draws from this and nothing else.
typedef enum
{
	UPDATE_IDLE,         ///< Nothing has been asked for yet.
	UPDATE_CHECKING,     ///< Talking to GitHub.
	UPDATE_UP_TO_DATE,   ///< Asked, and this build is already the newest.
	UPDATE_AVAILABLE,    ///< A newer release exists; waiting on the player.
	UPDATE_DOWNLOADING,  ///< Streaming the .cia into the install handle.
	UPDATE_INSTALLING,   ///< Download finished, AM is committing the title.
	UPDATE_DONE,         ///< Installed. The app should relaunch itself now.
	UPDATE_FAILED,       ///< Gave up. updaterMessage() says why.
} updateState;

// Brings up the services the updater needs - sockets, AM, and the RomFs the
// certificate bundle lives in. Safe to call once, from gameInit. Returns false
// if any of them refused, in which case the button reports itself unavailable
// rather than the game failing to boot.
bool updaterInit(void);
void updaterExit(void);

// False when updaterInit could not get its services up. The Options page greys
// the button out rather than offering something that cannot work.
bool updaterAvailable(void);

// Starts the version check on the worker thread and returns immediately.
// Ignored unless the updater is idle or settled from a previous attempt.
void updaterStartCheck(void);

// Starts the download and install, also on the worker thread. Only meaningful
// once a check has reported UPDATE_AVAILABLE; ignored otherwise.
void updaterStartInstall(void);

// Where things are. Cheap; call every frame.
updateState updaterState(void);

// True while the worker thread owns the job and the player must not be offered
// a way out - a cancelled download would leave a half-written title behind.
bool updaterBusy(void);

// One line of plain English for the top screen - the failure reason when the
// state is UPDATE_FAILED, a description of what is happening otherwise.
const char* updaterMessage(void);

// Percent of the download completed, or -1 when that is not a meaningful
// question yet. The console shows a bar only when this is >= 0.
int updaterProgress(void);

// The newest release tag GitHub reported, valid from UPDATE_AVAILABLE onward.
// Empty string before that.
const char* updaterLatestVersion(void);

// Points the chainloader back at this title, which after a successful install
// is the new build. Only meaningful on UPDATE_DONE.
//
// It returns immediately - the jump happens when the app exits - so the caller
// must fall out of its main loop right after calling this. If the system
// declines the jump the player simply lands on the HOME menu, and tapping the
// icon starts the new version anyway, so there is no failure path to handle.
void updaterRelaunch(void);
