/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef BACKENDS_MIXER_PLAYDATE_H
#define BACKENDS_MIXER_PLAYDATE_H

#include "backends/mixer/mixer.h"

typedef struct PlaydateAPI PlaydateAPI;
typedef struct SoundSource SoundSource;

/**
 * Audio mixer for the Playdate.
 *
 * The Playdate pulls samples through an audio source callback that
 * runs asynchronously (an interrupt on the device, a thread in the
 * simulator), while ScummVM runs single threaded. The two sides meet
 * in a lock-free single-producer/single-consumer ring buffer:
 * update(), called from the main loop, mixes into the ring; the
 * callback drains it.
 */
class PlaydateMixerManager : public MixerManager {
public:
	explicit PlaydateMixerManager(PlaydateAPI *pd);
	~PlaydateMixerManager() override;

	void init() override;
	void suspendAudio() override;
	int resumeAudio() override;

	/** Refills the ring buffer; called from the ScummVM main loop. */
	void update();

private:
	static int audioSourceCallback(void *context, int16 *left, int16 *right, int len);

	PlaydateAPI *_pd;
	SoundSource *_source;

	int16 *_ring; // interleaved stereo frames
	uint32 _ringFrames; // power of two
	volatile uint32 _readPos; // in frames, advanced only by the callback
	volatile uint32 _writePos; // in frames, advanced only by update()

	int16 *_mixBuf;

	// Wall-clock pacing so the sound generators advance (and fire their
	// completion flags) even when the consumer is not draining the ring.
	bool _clockStarted;
	uint32 _startMs;
	uint64 _producedFrames;
};

#endif
