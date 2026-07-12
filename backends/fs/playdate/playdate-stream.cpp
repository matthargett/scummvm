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

#include "backends/fs/playdate/playdate-stream.h"

#include "backends/platform/playdate/playdate-sdk.h"

PlaydateIoStream *PlaydateIoStream::makeFromPath(PlaydateAPI *pd, const Common::String &path, bool writeMode) {
	FileOptions mode = writeMode ? kFileWrite : (FileOptions)(kFileRead | kFileReadData);
	SDFile *handle = pd->file->open(path.c_str(), mode);
	if (!handle)
		return nullptr;

	return new PlaydateIoStream(pd, handle);
}

PlaydateIoStream::PlaydateIoStream(PlaydateAPI *pd, void *handle)
	: _pd(pd), _handle(handle), _err(false), _eos(false) {
}

PlaydateIoStream::~PlaydateIoStream() {
	_pd->file->close((SDFile *)_handle);
}

int64 PlaydateIoStream::pos() const {
	return _pd->file->tell((SDFile *)_handle);
}

int64 PlaydateIoStream::size() const {
	SDFile *file = (SDFile *)_handle;
	int oldPos = _pd->file->tell(file);
	_pd->file->seek(file, 0, SEEK_END);
	int length = _pd->file->tell(file);
	_pd->file->seek(file, oldPos, SEEK_SET);
	return length;
}

bool PlaydateIoStream::seek(int64 offs, int whence) {
	if (_pd->file->seek((SDFile *)_handle, (int)offs, whence) != 0) {
		_err = true;
		return false;
	}
	_eos = false;
	return true;
}

uint32 PlaydateIoStream::read(void *dataPtr, uint32 dataSize) {
	int result = _pd->file->read((SDFile *)_handle, dataPtr, dataSize);
	if (result < 0) {
		_err = true;
		return 0;
	}
	if ((uint32)result < dataSize)
		_eos = true;
	return (uint32)result;
}

uint32 PlaydateIoStream::write(const void *dataPtr, uint32 dataSize) {
	int result = _pd->file->write((SDFile *)_handle, dataPtr, dataSize);
	if (result < 0) {
		_err = true;
		return 0;
	}
	return (uint32)result;
}

bool PlaydateIoStream::flush() {
	return _pd->file->flush((SDFile *)_handle) >= 0;
}
