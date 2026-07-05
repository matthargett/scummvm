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

#ifndef COMMON_INSPECTOR_SHA1_H
#define COMMON_INSPECTOR_SHA1_H

#include "common/scummsys.h"
#include "common/str.h"

namespace Inspector {

/**
 * SHA-1 message digest (RFC 3174).
 *
 * Needed for the RFC 6455 WebSocket opening handshake
 * (Sec-WebSocket-Accept). SHA-1 is cryptographically broken and must not
 * be used for anything security-sensitive; the WebSocket handshake uses it
 * only as a protocol-correctness check, which is why it is tucked away
 * here instead of next to common/md5.h.
 */
struct SHA1Digest {
	byte bytes[20];

	Common::String toHex() const;
	bool operator==(const SHA1Digest &other) const;
};

/** Compute the SHA-1 digest of a memory buffer (single shot). */
SHA1Digest sha1(const byte *data, uint32 len);

/** Convenience overload hashing the bytes of a string (no terminator). */
SHA1Digest sha1(const Common::String &data);

} // End of namespace Inspector

#endif
