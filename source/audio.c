#include "audio.h"
#include "audio_synth.h"
#include "settings.h"

#include <3ds.h>
#include <math.h>
#include <string.h>

// The clip generator owns the rate; the channels are set to the same one so
// nothing is ever resampled and a click stays a click.
#define SND_RATE SYNTH_RATE

// The two clip orders are the same list written twice - once as an enum the game
// reads and once as a table the generator fills - so they are checked rather
// than trusted.
_Static_assert(SND_COUNT == SYNTH_CLIP_COUNT,
	"audio.h's soundId order and audio_synth.c's clip table must match");

// Four voices. A snip can land while a file stroke is still rasping and while
// the box lid is still coming off, which is three; four leaves one spare so the
// oldest voice is only ever stolen in a genuine pile-up.
#define VOICE_COUNT 4

typedef struct
{
	s16* samples;
	u32  count;
} soundClip;

static soundClip clips[SND_COUNT];
static s16*      clipPool;              // one linear allocation behind every clip
static ndspWaveBuf waveBufs[VOICE_COUNT];
static u64  voiceStart[VOICE_COUNT];    // when each voice was last given work
static u64  lastPlayed[SND_COUNT];      // for audioPlayThrottled
static int  nextVoice;
static bool started;
static int  pushedVolume = -1;          // last value handed to the DSP, 0-100
static const char* reason = "not started";

bool audioAvailable(void) { return started; }
const char* audioReason(void) { return reason; }

static bool buildClips(void)
{
	uint32_t len[SYNTH_CLIP_COUNT];
	u32 total = audioSynthLengths(len);

	// One allocation for the lot. linearAlloc is required: the DSP reads these
	// addresses directly and cannot follow a normal heap pointer.
	clipPool = (s16*)linearAlloc(total * sizeof(s16));
	if (!clipPool) { reason = "out of linear memory for clips"; return false; }

	audioSynthBuild(clipPool);

	s16* cursor = clipPool;
	for (int i = 0; i < SND_COUNT; i++)
	{
		clips[i].samples = cursor;
		clips[i].count   = len[i];
		cursor += len[i];
	}

	// The DSP reads through its own view of memory, so what the CPU just wrote
	// has to be pushed out before any of it is queued.
	DSP_FlushDataCache(clipPool, total * sizeof(s16));
	return true;
}

bool audioInit(void)
{
	if (R_FAILED(ndspInit()))
	{
		// The usual cause on real hardware by a mile: dspfirm.cdc has never been
		// dumped off the console. Say that rather than a hex code, because that
		// is the thing the player can act on.
		reason = "no DSP firmware (dump dspfirm.cdc)";
		return false;
	}

	if (!buildClips()) { ndspExit(); return false; }

	ndspSetOutputMode(NDSP_OUTPUT_STEREO);

	float mix[12];
	memset(mix, 0, sizeof(mix));
	mix[0] = mix[1] = 1.0f;   // front left and right, dead centre

	for (int v = 0; v < VOICE_COUNT; v++)
	{
		ndspChnSetFormat(v, NDSP_FORMAT_MONO_PCM16);
		ndspChnSetRate(v, SND_RATE);
		ndspChnSetMix(v, mix);
		memset(&waveBufs[v], 0, sizeof(waveBufs[v]));
	}

	started = true;
	reason  = "ok";
	pushedVolume = -1;        // force the first audioUpdateVolume to push
	audioUpdateVolume();
	return true;
}

void audioExit(void)
{
	if (!started) return;
	for (int v = 0; v < VOICE_COUNT; v++) ndspChnWaveBufClear(v);
	ndspExit();
	if (clipPool) { linearFree(clipPool); clipPool = NULL; }
	started = false;
	reason  = "stopped";
}

void audioUpdateVolume(void)
{
	if (!started) return;
	int v = settingsVolume();
	if (v == pushedVolume) return;
	pushedVolume = v;
	ndspSetMasterVol((float)v / 100.0f);
}

void audioPlay(soundId id)
{
	if (!started || id < 0 || id >= SND_COUNT) return;

	// Prefer a voice that has finished. Only when every one is still sounding is
	// the longest-running one taken, which is the one closest to being over.
	int pick = -1;
	for (int i = 0; i < VOICE_COUNT; i++)
	{
		int v = (nextVoice + i) % VOICE_COUNT;
		if (waveBufs[v].status == NDSP_WBUF_FREE || waveBufs[v].status == NDSP_WBUF_DONE)
		{
			pick = v;
			break;
		}
	}
	if (pick < 0)
	{
		pick = 0;
		for (int v = 1; v < VOICE_COUNT; v++)
			if (voiceStart[v] < voiceStart[pick]) pick = v;
		ndspChnWaveBufClear(pick);
	}
	nextVoice = (pick + 1) % VOICE_COUNT;

	ndspWaveBuf* buf = &waveBufs[pick];
	memset(buf, 0, sizeof(*buf));
	buf->data_vaddr = clips[id].samples;
	buf->nsamples   = clips[id].count;
	buf->looping    = false;

	voiceStart[pick] = osGetTime();
	lastPlayed[id]   = voiceStart[pick];
	ndspChnWaveBufAdd(pick, buf);
}

void audioPlayThrottled(soundId id, unsigned int minGapMs)
{
	if (!started || id < 0 || id >= SND_COUNT) return;
	u64 now = osGetTime();
	if (lastPlayed[id] != 0 && now - lastPlayed[id] < (u64)minGapMs) return;
	audioPlay(id);
}
