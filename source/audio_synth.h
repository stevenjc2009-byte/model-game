// The arithmetic that makes the clips, with nothing 3DS in it.
//
// Split out from audio.c on purpose. The DSP cannot be started under an
// emulator without a dumped dspfirm.cdc, so the only way to check that a snip is
// actually a snip - and not silence, or a clipped mess - is to run the same
// generator on a PC and look at the samples that come out. That is only possible
// if the generator has no libctru in it, so it has none: plain C, stdint, math.h.
//
// The clips it writes are what the game plays. Not a copy of it, not a model of
// it - the same translation unit, compiled twice.

#pragma once

#include <stdint.h>

// Clip identities, and how many there are. audio.h's soundId mirrors this order
// and audio.c asserts they agree, so the two cannot drift.
#define SYNTH_CLIP_COUNT 7

// The rate the clips are generated at and must be played back at. The DSP's own
// rate, so nothing is ever resampled.
#define SYNTH_RATE 32728.0f

// How many samples each clip is, in clip order. Written into out, which must
// have room for SYNTH_CLIP_COUNT entries. Returns the total.
uint32_t audioSynthLengths(uint32_t out[SYNTH_CLIP_COUNT]);

// Fills pool - which must hold audioSynthLengths()'s total samples - with every
// clip laid end to end in clip order. Deterministic: the same samples every run,
// on either machine.
void audioSynthBuild(int16_t* pool);
