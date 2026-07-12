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

// pd->file has members named like the (forbidden) POSIX calls
#define FORBIDDEN_SYMBOL_EXCEPTION_mkdir
#define FORBIDDEN_SYMBOL_EXCEPTION_unlink

#include "backends/fs/playdate/playdate-fs.h"
#include "backends/fs/playdate/playdate-stream.h"

#include "backends/platform/playdate/playdate-sdk.h"

namespace {

struct ListDirContext {
	PlaydateAPI *pd;
	const PlaydateFilesystemNode *node;
	AbstractFSList *list;
	Common::FSNode::ListMode mode;
	bool hidden;
};

} // End of anonymous namespace

PlaydateFilesystemNode::PlaydateFilesystemNode(PlaydateAPI *pd)
	: _pd(pd), _displayName("Root"), _path("/"), _isDirectory(true), _isValid(true) {
}

PlaydateFilesystemNode::PlaydateFilesystemNode(PlaydateAPI *pd, const Common::String &path, bool verify)
	: _pd(pd), _isDirectory(false), _isValid(true) {
	assert(!path.empty());

	_path = path;
	// Normalize away trailing slashes (getChildren appends none, but
	// caller supplied paths may have them)
	while (_path.size() > 1 && _path.lastChar() == '/')
		_path.deleteLastChar();

	if (_path == "/") {
		_displayName = "Root";
		_isDirectory = true;
		return;
	}

	_displayName = lastPathComponent(_path, '/');

	if (verify) {
		FileStat st;
		if (_pd->file->stat(toSDPath().c_str(), &st) == 0) {
			_isDirectory = (st.isdir != 0);
		} else {
			_isValid = false;
		}
	}
}

Common::String PlaydateFilesystemNode::toSDPath() const {
	Common::String sdPath = _path;
	while (!sdPath.empty() && sdPath.firstChar() == '/')
		sdPath.deleteChar(0);
	return sdPath;
}

bool PlaydateFilesystemNode::exists() const {
	if (_path == "/")
		return true;

	FileStat st;
	return _pd->file->stat(toSDPath().c_str(), &st) == 0;
}

bool PlaydateFilesystemNode::isReadable() const {
	return exists();
}

bool PlaydateFilesystemNode::isWritable() const {
	// Everything in the data directory is writable; files coming from
	// the pdx bundle are not, but this cannot be distinguished cheaply.
	return true;
}

AbstractFSNode *PlaydateFilesystemNode::getChild(const Common::String &n) const {
	assert(_isDirectory);

	Common::String newPath(_path);
	if (newPath.lastChar() != '/')
		newPath += '/';
	newPath += n;

	return new PlaydateFilesystemNode(_pd, newPath, true);
}

void PlaydateFilesystemNode::listFilesCallback(const char *filename, void *userdata) {
	ListDirContext *ctx = (ListDirContext *)userdata;

	Common::String name(filename);
	if (name.empty() || name == "./" || name == "../")
		return;

	// pd->file->listfiles() marks directories with a trailing slash
	bool isDir = false;
	if (name.lastChar() == '/') {
		isDir = true;
		name.deleteLastChar();
	}
	if (name.empty())
		return;

	if (name.firstChar() == '.' && !ctx->hidden)
		return;

	if ((ctx->mode == Common::FSNode::kListFilesOnly && isDir) ||
	    (ctx->mode == Common::FSNode::kListDirectoriesOnly && !isDir))
		return;

	Common::String childPath(ctx->node->getPath());
	if (childPath.lastChar() != '/')
		childPath += '/';
	childPath += name;

	PlaydateFilesystemNode *child = new PlaydateFilesystemNode(ctx->pd, childPath, false);
	child->_isDirectory = isDir;
	ctx->list->push_back(child);
}

bool PlaydateFilesystemNode::getChildren(AbstractFSList &list, ListMode mode, bool hidden) const {
	assert(_isDirectory);

	ListDirContext context = { _pd, this, &list, mode, hidden };

	Common::String sdPath = toSDPath();
	if (sdPath.empty())
		sdPath = "/";

	return _pd->file->listfiles(sdPath.c_str(), listFilesCallback, &context, hidden ? 1 : 0) == 0;
}

AbstractFSNode *PlaydateFilesystemNode::getParent() const {
	if (_path == "/")
		return nullptr;

	const char *start = _path.c_str();
	const char *end = lastPathComponent(_path, '/');

	Common::String parentPath(start, end - start);
	if (parentPath.empty())
		parentPath = "/";

	return new PlaydateFilesystemNode(_pd, parentPath, false);
}

Common::SeekableReadStream *PlaydateFilesystemNode::createReadStream() {
	return PlaydateIoStream::makeFromPath(_pd, toSDPath(), false);
}

Common::SeekableWriteStream *PlaydateFilesystemNode::createWriteStream(bool atomic) {
	// TODO: Add atomic support if possible
	return PlaydateIoStream::makeFromPath(_pd, toSDPath(), true);
}

bool PlaydateFilesystemNode::createDirectory() {
	if (_pd->file->mkdir(toSDPath().c_str()) == 0) {
		_isValid = true;
		_isDirectory = true;
	}
	return _isValid && _isDirectory;
}
