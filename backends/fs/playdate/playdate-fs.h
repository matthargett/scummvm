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

#ifndef BACKENDS_FS_PLAYDATE_FS_H
#define BACKENDS_FS_PLAYDATE_FS_H

#include "backends/fs/abstract-fs.h"

typedef struct PlaydateAPI PlaydateAPI;

/**
 * File system node for the Playdate, implemented on top of pd->file.
 *
 * The pd->file API exposes a single sandboxed namespace that merges
 * the read-only pdx bundle with the game's writable data directory.
 * Paths are presented to ScummVM with a leading '/' denoting the root
 * of that namespace.
 */
class PlaydateFilesystemNode final : public AbstractFSNode {
protected:
	PlaydateAPI *_pd;
	Common::String _displayName;
	Common::String _path;
	bool _isDirectory;
	bool _isValid;

public:
	/**
	 * Creates a node for the root of the pd->file namespace.
	 */
	explicit PlaydateFilesystemNode(PlaydateAPI *pd);

	/**
	 * Creates a node for a given path.
	 *
	 * @param path   Path the new node should point to.
	 * @param verify true if the isValid and isDirectory flags should
	 *               be verified during the construction.
	 */
	PlaydateFilesystemNode(PlaydateAPI *pd, const Common::String &path, bool verify = true);

	bool exists() const override;
	Common::U32String getDisplayName() const override { return _displayName; }
	Common::String getName() const override { return _displayName; }
	Common::String getPath() const override { return _path; }
	bool isDirectory() const override { return _isDirectory; }
	bool isReadable() const override;
	bool isWritable() const override;

	AbstractFSNode *getChild(const Common::String &n) const override;
	bool getChildren(AbstractFSList &list, ListMode mode, bool hidden) const override;
	AbstractFSNode *getParent() const override;

	Common::SeekableReadStream *createReadStream() override;
	Common::SeekableWriteStream *createWriteStream(bool atomic) override;
	bool createDirectory() override;

private:
	/** Strips the leading '/' for use with the pd->file API. */
	Common::String toSDPath() const;

	static void listFilesCallback(const char *filename, void *userdata);
};

#endif
