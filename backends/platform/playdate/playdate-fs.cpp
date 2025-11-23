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

#include "playdate-fs.h"
#include "backends/fs/abstract-fs.h"
#include "common/stream.h"

// Wrapper classes for streams
class PlaydateReadStream : public Common::SeekableReadStream {
public:
	PlaydateReadStream(PlaydateAPI *pd, SDFile *file) : _pd(pd), _file(file) {}
	virtual ~PlaydateReadStream() { _pd->file->close(_file); }

	virtual bool err() const override { return false; }
	virtual void clearErr() override {}
	virtual bool eos() const override { return false; }
	virtual uint32 read(void *data, uint32 len) override {
		return _pd->file->read(_file, data, len);
	}
	virtual bool seek(int64 offset, int whence = SEEK_SET) override {
		return _pd->file->seek(_file, (int)offset, whence) == 0;
	}
	virtual int64 pos() const override {
		return _pd->file->tell(_file);
	}
	virtual int64 size() const override {
		int64 current = pos();
		_pd->file->seek(_file, 0, SEEK_END);
		int64 len = _pd->file->tell(_file);
		_pd->file->seek(_file, (int)current, SEEK_SET);
		return len;
	}

private:
	PlaydateAPI *_pd;
	SDFile *_file;
};

class PlaydateWriteStream : public Common::SeekableWriteStream {
public:
	PlaydateWriteStream(PlaydateAPI *pd, SDFile *file) : _pd(pd), _file(file) {}
	virtual ~PlaydateWriteStream() { _pd->file->close(_file); }

	virtual bool err() const override { return false; }
	virtual void clearErr() override {}
	virtual uint32 write(const void *data, uint32 len) override {
		return _pd->file->write(_file, data, len);
	}
	virtual bool flush() override {
		return _pd->file->flush(_file) == 0;
	}
	virtual int64 pos() const override {
		return _pd->file->tell(_file);
	}
	virtual bool seek(int64 offset, int whence = SEEK_SET) override {
		return _pd->file->seek(_file, (int)offset, whence) == 0;
	}
	virtual int64 size() const override {
		return 0;
	}

private:
	PlaydateAPI *_pd;
	SDFile *_file;
};

class PlaydateFilesystemNode : public AbstractFSNode {
public:
	PlaydateFilesystemNode(PlaydateAPI *pd, const Common::String &path, bool isDir)
		: AbstractFSNode(), _pd(pd), _path(path), _isDir(isDir) {}

	virtual Common::String getName() const override {
		const char *lastSlash = strrchr(_path.c_str(), '/');
		if (lastSlash)
			return lastSlash + 1;
		return _path;
	}

	virtual Common::String getPath() const override { return _path; }
	virtual bool isDirectory() const override { return _isDir; }
	virtual bool isReadable() const override { return true; }
	virtual bool isWritable() const override { return true; }

	virtual AbstractFSNode *getParent() const override {
		if (_path == "/" || _path.empty())
			return nullptr;
		const char *lastSlash = strrchr(_path.c_str(), '/');
		if (!lastSlash)
			return new PlaydateFilesystemNode(_pd, "/", true);
		Common::String parentPath = _path.substr(0, lastSlash - _path.c_str());
		if (parentPath.empty())
			parentPath = "/";
		return new PlaydateFilesystemNode(_pd, parentPath, true);
	}

	virtual AbstractFSNode *getChild(const Common::String &n) const override {
		Common::String childPath = _path;
		if (!childPath.hasSuffix("/"))
			childPath += "/";
		childPath += n;
		FileStat stat;
		if (_pd->file->stat(childPath.c_str(), &stat) == 0) {
			return new PlaydateFilesystemNode(_pd, childPath, stat.isdir);
		}
		return new PlaydateFilesystemNode(_pd, childPath, false);
	}

	virtual bool getChildren(AbstractFSList &list, ListMode mode, bool hidden) const override {
		if (!_isDir)
			return false;

		struct Context {
			AbstractFSList *list;
			PlaydateAPI *pd;
			Common::String parentPath;
			ListMode mode;
		} ctx;
		ctx.list = &list;
		ctx.pd = _pd;
		ctx.parentPath = _path;
		ctx.mode = mode;

		_pd->file->listfiles(_path.c_str(), [](const char *filename, void *userdata) {
			Context *c = (Context *)userdata;
			Common::String fullPath = c->parentPath;
			if (!fullPath.hasSuffix("/")) fullPath += "/";
			fullPath += filename;
			
			FileStat stat;
			bool isDir = false;
			if (c->pd->file->stat(fullPath.c_str(), &stat) == 0) {
				isDir = stat.isdir;
			}

			if (c->mode == Common::FSNode::kListFilesOnly && isDir) return;
			if (c->mode == Common::FSNode::kListDirectoriesOnly && !isDir) return;

			c->list->push_back(new PlaydateFilesystemNode(c->pd, fullPath, isDir)); }, &ctx, hidden ? 1 : 0);
		return true;
	}

	virtual bool exists() const override {
		FileStat stat;
		return _pd->file->stat(_path.c_str(), &stat) == 0;
	}

	virtual Common::U32String getDisplayName() const override {
		return Common::U32String(getName());
	}

	virtual bool createDirectory() override {
		// Work around forbidden.h mkdir macro
#ifdef mkdir
#undef mkdir
#endif
		return (_pd->file->mkdir(_path.c_str()) == 0);
	}

	virtual Common::SeekableReadStream *createReadStream() override {
		SDFile *file = _pd->file->open(_path.c_str(), kFileRead);
		if (!file)
			return nullptr;
		return new PlaydateReadStream(_pd, file);
	}

	virtual Common::SeekableWriteStream *createWriteStream(bool atomic) override {
		// Atomic write not fully supported, just open for write
		SDFile *file = _pd->file->open(_path.c_str(), kFileWrite);
		if (!file)
			return nullptr;
		return new PlaydateWriteStream(_pd, file);
	}

private:
	PlaydateAPI *_pd;
	Common::String _path;
	bool _isDir;
};

AbstractFSNode *PlaydateFilesystemFactory::makeRootFileNode() const {
	return new PlaydateFilesystemNode(_pd, "/", true);
}

AbstractFSNode *PlaydateFilesystemFactory::makeCurrentDirectoryFileNode() const {
	return new PlaydateFilesystemNode(_pd, ".", true);
}

AbstractFSNode *PlaydateFilesystemFactory::makeFileNodePath(const Common::String &path) const {
	FileStat stat;
	bool isDir = false;
	if (_pd && _pd->file && _pd->file->stat(path.c_str(), &stat) == 0)
		isDir = stat.isdir;
	return new PlaydateFilesystemNode(_pd, path, isDir);
}
