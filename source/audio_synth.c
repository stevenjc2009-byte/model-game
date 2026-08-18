#include "audio_synth.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Everything below is a noise source, a one-pole filter and an envelope, because
// that is enough for the noises a pair of nippers and a tube of plastic make.

// A fixed sequence rather than rand(), so a clip sounds the same on every boot
// and on either machine - which is what makes a PC render of it evidence about
// the console build rather than about a different roll of the dice.
static uint32_t noiseState;
static float noiseSample(void)
{
	noiseState = noiseState * 1664525u + 1013904223u;
	return (float)(int)((noiseState >> 8) & 0xFFFF) / 32768.0f - 1.0f;
}

// Exponential fall from 1 to near zero across the clip, with a short linear rise
// at the front so nothing starts on a discontinuity and pops.
static float envelope(uint32_t i, uint32_t n, float decay)
{
	float t = (float)i / (float)n;
	float attack = t < 0.02f ? t / 0.02f : 1.0f;
	return attack * expf(-decay * t);
}

// Filtered noise under an envelope - the shape of a cut, a rub or a scrape. A
// low cutoff is a rasp, a high one is a hiss.
static void genNoise(int16_t* dst, uint32_t count, float cutoff, float decay, float gain)
{
	float lp = 0.0f;
	for (uint32_t i = 0; i < count; i++)
	{
		lp += cutoff * (noiseSample() - lp);
		float v = lp * envelope(i, count, decay) * gain;
		if (v >  1.0f) v =  1.0f;
		if (v < -1.0f) v = -1.0f;
		dst[i] = (int16_t)(v * 32000.0f);
	}
}

// A pitched blip. Square-ish rather than a pure sine, because a plastic part
// clicking into a socket has an edge to it that a sine does not.
static void genBlip(int16_t* dst, uint32_t count, float startHz, float endHz, float decay, float gain)
{
	float phase = 0.0f;
	for (uint32_t i = 0; i < count; i++)
	{
		float t  = (float)i / (float)count;
		float hz = startHz + (endHz - startHz) * t;
		phase += hz / SYNTH_RATE;
		if (phase >= 1.0f) phase -= 1.0f;
		// Two-thirds sine, one-third square: keeps the note but adds the click.
		float s = sinf(phase * 2.0f * (float)M_PI);
		float v = (0.66f * s + 0.34f * (s >= 0.0f ? 1.0f : -1.0f))
		        * envelope(i, count, decay) * gain;
		dst[i] = (int16_t)(v * 32000.0f);
	}
}

// Mixes a second pass on top of what is already there, so a clip can be a blip
// with a noise transient in front of it without needing a scratch buffer.
static void overlayNoise(int16_t* dst, uint32_t count, float cutoff, float decay, float gain)
{
	float lp = 0.0f;
	for (uint32_t i = 0; i < count; i++)
	{
		lp += cutoff * (noiseSample() - lp);
		int v = dst[i] + (int)(lp * envelope(i, count, decay) * gain * 32000.0f);
		if (v >  32000) v =  32000;
		if (v < -32000) v = -32000;
		dst[i] = (int16_t)v;
	}
}

#define MS(ms) ((uint32_t)(SYNTH_RATE * (ms) / 1000.0f))

uint32_t audioSynthLengths(uint32_t out[SYNTH_CLIP_COUNT])
{
	const uint32_t lengths[SYNTH_CLIP_COUNT] =
	{
		MS(70),   // snip
		MS(200),  // file
		MS(90),   // click
		MS(150),  // refuse
		MS(110),  // unseat
		MS(320),  // box
		MS(45),   // ui
	};

	uint32_t total = 0;
	for (int i = 0; i < SYNTH_CLIP_COUNT; i++) { out[i] = lengths[i]; total += lengths[i]; }
	return total;
}

void audioSynthBuild(int16_t* pool)
{
	uint32_t len[SYNTH_CLIP_COUNT];
	audioSynthLengths(len);

	int16_t* c[SYNTH_CLIP_COUNT];
	int16_t* cursor = pool;
	for (int i = 0; i < SYNTH_CLIP_COUNT; i++) { c[i] = cursor; cursor += len[i]; }

	// Reset here rather than at file scope, so building twice in one process -
	// which the PC render does - gives the same samples both times.
	noiseState = 0x2545F491u;

	// The nippers: a hard bright transient, gone almost at once.
	genNoise(c[0], len[0], 0.85f, 26.0f, 0.9f);
	// The file: a slower, darker rasp that lasts as long as a stroke does.
	genNoise(c[1], len[1], 0.18f, 5.0f, 0.55f);
	// Seating: a short rise with a tick of noise on the front - two pieces of
	// styrene meeting and holding.
	genBlip(c[2], len[2], 720.0f, 1180.0f, 18.0f, 0.5f);
	overlayNoise(c[2], len[2], 0.9f, 40.0f, 0.35f);
	// Refused: the same idea going the wrong way, low and flat.
	genBlip(c[3], len[3], 300.0f, 190.0f, 9.0f, 0.45f);
	// Coming off: a downward blip, so undo reads as the opposite of the click.
	genBlip(c[4], len[4], 1000.0f, 560.0f, 14.0f, 0.42f);
	// The box: a long soft scrape of card on card.
	genNoise(c[5], len[5], 0.08f, 3.2f, 0.5f);
	// A button: quiet, high and immediate, nothing left ringing after it.
	genBlip(c[6], len[6], 1400.0f, 1400.0f, 22.0f, 0.3f);
}
