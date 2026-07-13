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

#include "backends/mixer/playdate/playdate-mixer.h"

#include "audio/mixer_intern.h"
#include "common/util.h"

#include "backends/platform/playdate/playdate-sdk.h"

// The Playdate audio system runs at a fixed 44100 Hz
static const uint32 kSampleRate = 44100;
// Ring capacity: 8192 stereo frames ~= 185 ms
static const uint32 kRingFrames = 8192;
// Frames mixed per mixCallback() call
static const uint32 kChunkFrames = 1024;

PlaydateMixerManager::PlaydateMixerManager(PlaydateAPI *pd)
	: _pd(pd),
	  _source(nullptr),
	  _ring(nullptr),
	  _ringFrames(kRingFrames),
	  _readPos(0),
	  _writePos(0),
	  _mixBuf(nullptr),
	  _clockStarted(false),
	  _startMs(0),
	  _producedFrames(0) {
}

PlaydateMixerManager::~PlaydateMixerManager() {
	if (_source)
		_pd->sound->removeSource(_source);

	delete[] _ring;
	delete[] _mixBuf;
}

void PlaydateMixerManager::init() {
	_ring = new int16[_ringFrames * 2]();
	_mixBuf = new int16[kChunkFrames * 2];

	_mixer = new Audio::MixerImpl(kSampleRate, true, kChunkFrames);
	_mixer->setReady(true);

	_source = _pd->sound->addSource(audioSourceCallback, this, 1);
}

void PlaydateMixerManager::update() {
	if (_audioSuspended || !_mixer)
		return;

	// Pace mixing by wall-clock time. mixCallback() both produces samples
	// and steps ScummVM's sound generators (e.g. the AGI sound driver),
	// and it is those generators that fire the "sound finished" flags that
	// game scripts block on. If production were gated only on free ring
	// space - as it was - a consumer that is not draining at real time (no
	// audio device, or a stalled audio thread) would let the ring fill once
	// and then stall the generators, freezing any sound-gated game logic.
	// Advancing by elapsed time keeps the generators moving regardless, so
	// the game never hangs; a half-ring of lookahead is produced for smooth
	// playback, and when the ring is full the freshly mixed frames are
	// discarded rather than blocking (dropping them here, instead of
	// advancing _readPos, keeps the ring strictly single-producer/single-
	// consumer with the audio callback).
	const uint32 now = _pd->system->getCurrentTimeMilliseconds();
	if (!_clockStarted) {
		_clockStarted = true;
		_startMs = now;
		_producedFrames = 0;
	}

	uint64 dueByNow = (uint64)(now - _startMs) * kSampleRate / 1000 + (_ringFrames / 2);
	// After a long pause (e.g. a slow room load) skip the backlog rather
	// than bursting through thousands of frames in a single call.
	if (dueByNow > _producedFrames + _ringFrames)
		_producedFrames = dueByNow - _ringFrames;

	while (_producedFrames + kChunkFrames <= dueByNow) {
		_mixer->mixCallback((byte *)_mixBuf, kChunkFrames * 2 * sizeof(int16));
		_producedFrames += kChunkFrames;

		if (_ringFrames - (_writePos - _readPos) < kChunkFrames)
			continue; // ring full: consumer not keeping up, drop these frames

		const uint32 mask = _ringFrames - 1;
		uint32 pos = _writePos & mask;
		const uint32 untilWrap = _ringFrames - pos;
		if (untilWrap >= kChunkFrames) {
			memcpy(_ring + pos * 2, _mixBuf, kChunkFrames * 2 * sizeof(int16));
		} else {
			memcpy(_ring + pos * 2, _mixBuf, untilWrap * 2 * sizeof(int16));
			memcpy(_ring, _mixBuf + untilWrap * 2, (kChunkFrames - untilWrap) * 2 * sizeof(int16));
		}

		// Publish the samples before advancing the write index, so the
		// consumer never reads a frame that has not been written yet.
		__sync_synchronize();
		_writePos += kChunkFrames;
	}
}

int PlaydateMixerManager::audioSourceCallback(void *context, int16 *left, int16 *right, int len) {
	PlaydateMixerManager *self = (PlaydateMixerManager *)context;

	const uint32 mask = self->_ringFrames - 1;
	uint32 avail = self->_writePos - self->_readPos;
	// Only read frames the producer has published (see update()).
	__sync_synchronize();
	uint32 count = MIN<uint32>(avail, (uint32)len);

	for (uint32 i = 0; i < count; i++) {
		const int16 *frame = self->_ring + ((self->_readPos + i) & mask) * 2;
		left[i] = frame[0];
		right[i] = frame[1];
	}
	self->_readPos += count;

	// Underrun: pad with silence
	for (uint32 i = count; i < (uint32)len; i++) {
		left[i] = 0;
		right[i] = 0;
	}

	return 1;
}

void PlaydateMixerManager::suspendAudio() {
	_audioSuspended = true;
}

int PlaydateMixerManager::resumeAudio() {
	if (!_audioSuspended)
		return -1;
	_audioSuspended = false;
	return 0;
}
