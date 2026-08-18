// The sounds the bench makes.
//
// There are no sound files. Every clip is synthesised into memory at startup
// from a few lines of arithmetic, which is why the game still has no romfs and
// no data directory - a snip is a noise burst under a fast decay, a click is two
// short blips, and both cost a few kilobytes of linear memory rather than an
// asset pipeline. What they should eventually sound like is a design decision;
// what this file guarantees is that there is somewhere for that decision to go.
//
// Every function here is safe to call when the DSP never started. A retail 3DS
// needs dspfirm.cdc dumped off the console before ndsp will initialise at all,
// and a player who has not done that must still be able to build models - so a
// failed init leaves the whole module as a set of no-ops rather than stopping
// the game or faulting on the first snip.

#pragma once

#include <stdbool.h>

typedef enum
{
	SND_SNIP = 0,   // nippers through a gate
	SND_FILE,       // one stroke of the file across a nub
	SND_CLICK,      // a part seating into its socket
	SND_REFUSE,     // a seat that was not allowed
	SND_UNSEAT,     // a fitted part pulled back off
	SND_BOX,        // the kit box lid coming off
	SND_UI,         // a button on the front end or the pause menu
	SND_COUNT
} soundId;

// Starts the DSP and builds the clips. Returns false if the console has no DSP
// firmware, which is not fatal - see the header comment. Call once, after
// gfxInitDefault.
bool audioInit(void);
void audioExit(void);

// Whether anything will actually be heard. Worth reporting once at startup so a
// silent game is explained rather than mysterious.
bool audioAvailable(void);
// Why not, when it is not. Never NULL.
const char* audioReason(void);

// Plays a clip on the next free voice. Several can overlap; the oldest voice is
// taken when they are all busy, so a sound is never dropped silently.
void audioPlay(soundId id);

// The same, but refuses to retrigger a clip that started less than minGapMs ago.
// Filing is a continuous rub that the game samples many times a second, and
// without this it would restart the same rasp on every one of those samples and
// come out as a buzz.
void audioPlayThrottled(soundId id, unsigned int minGapMs);

// Pushes the master volume from settings into the DSP. Cheap enough for the
// frame loop; only touches the DSP when the number has actually moved.
void audioUpdateVolume(void);
