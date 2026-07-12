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

#ifndef BACKENDS_FS_PLAYDATE_STREAM_H
#define BACKENDS_FS_PLAYDATE_STREAM_H

#include "common/noncopyable.h"
#include "common/stream.h"
#include "common/str.h"

typedef struct PlaydateAPI PlaydateAPI;

/**
 * Wraps a Playdate SDFile handle into ScummVM streams.
 *
 * Read streams look in both the pdx bundle and the game's data
 * directory; write streams always go to the data directory.
 */
class PlaydateIoStream final : public Common::SeekableReadStream,
                               public Common::SeekableWriteStream,
                               public Common::NonCopyable {
public:
	/**
	 * @param path Path relative to the pd->file sandbox root.
	 * @return A stream, or nullptr on failure.
	 */
	static PlaydateIoStream *makeFromPath(PlaydateAPI *pd, const Common::String &path, bool writeMode);

	PlaydateIoStream(PlaydateAPI *pd, void *handle);
	~PlaydateIoStream() override;

	bool err() const override { return _err; }
	void clearErr() override { _err = false; _eos = false; }
	bool eos() const override { return _eos; }

	uint32 write(const void *dataPtr, uint32 dataSize) override;
	bool flush() override;

	int64 pos() const override;
	int64 size() const override;
	bool seek(int64 offs, int whence = SEEK_SET) override;
	uint32 read(void *dataPtr, uint32 dataSize) override;

private:
	PlaydateAPI *_pd;
	void *_handle;
	bool _err;
	bool _eos;
};

#endif
