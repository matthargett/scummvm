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

#include "common/inspector/sha1.h"

namespace Inspector {

namespace {

inline uint32 rol32(uint32 v, int bits) {
	return (v << bits) | (v >> (32 - bits));
}

struct SHA1Context {
	uint32 h[5];
	uint64 totalBits;
	byte block[64];
	uint32 blockFill;

	SHA1Context() {
		h[0] = 0x67452301;
		h[1] = 0xEFCDAB89;
		h[2] = 0x98BADCFE;
		h[3] = 0x10325476;
		h[4] = 0xC3D2E1F0;
		totalBits = 0;
		blockFill = 0;
	}

	void processBlock(const byte *p) {
		uint32 w[80];
		for (int i = 0; i < 16; i++)
			w[i] = ((uint32)p[i * 4] << 24) | ((uint32)p[i * 4 + 1] << 16) |
			       ((uint32)p[i * 4 + 2] << 8) | (uint32)p[i * 4 + 3];
		for (int i = 16; i < 80; i++)
			w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

		uint32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

		for (int i = 0; i < 80; i++) {
			uint32 f, k;
			if (i < 20) {
				f = (b & c) | ((~b) & d);
				k = 0x5A827999;
			} else if (i < 40) {
				f = b ^ c ^ d;
				k = 0x6ED9EBA1;
			} else if (i < 60) {
				f = (b & c) | (b & d) | (c & d);
				k = 0x8F1BBCDC;
			} else {
				f = b ^ c ^ d;
				k = 0xCA62C1D6;
			}
			uint32 tmp = rol32(a, 5) + f + e + k + w[i];
			e = d;
			d = c;
			c = rol32(b, 30);
			b = a;
			a = tmp;
		}

		h[0] += a;
		h[1] += b;
		h[2] += c;
		h[3] += d;
		h[4] += e;
	}

	void update(const byte *data, uint32 len) {
		totalBits += (uint64)len * 8;
		while (len > 0) {
			uint32 space = 64 - blockFill;
			uint32 take = (len < space) ? len : space;
			memcpy(block + blockFill, data, take);
			blockFill += take;
			data += take;
			len -= take;
			if (blockFill == 64) {
				processBlock(block);
				blockFill = 0;
			}
		}
	}

	SHA1Digest finish() {
		// Append the 0x80 terminator, pad with zeroes so that 8 bytes
		// remain for the big-endian bit length. The length is the message
		// length, captured before any padding is appended.
		uint64 savedBits = totalBits;
		byte pad = 0x80;
		update(&pad, 1);
		byte zero = 0;
		while (blockFill != 56)
			update(&zero, 1);
		byte lenBuf[8];
		for (int i = 0; i < 8; i++)
			lenBuf[i] = (byte)(savedBits >> (56 - i * 8));
		// update() also advances totalBits, but nothing reads it after this.
		update(lenBuf, 8);

		SHA1Digest digest;
		for (int i = 0; i < 5; i++) {
			digest.bytes[i * 4]     = (byte)(h[i] >> 24);
			digest.bytes[i * 4 + 1] = (byte)(h[i] >> 16);
			digest.bytes[i * 4 + 2] = (byte)(h[i] >> 8);
			digest.bytes[i * 4 + 3] = (byte)(h[i]);
		}
		return digest;
	}
};

} // End of anonymous namespace

Common::String SHA1Digest::toHex() const {
	static const char *hex = "0123456789abcdef";
	Common::String result;
	for (int i = 0; i < 20; i++) {
		result += hex[bytes[i] >> 4];
		result += hex[bytes[i] & 0xF];
	}
	return result;
}

bool SHA1Digest::operator==(const SHA1Digest &other) const {
	return memcmp(bytes, other.bytes, 20) == 0;
}

SHA1Digest sha1(const byte *data, uint32 len) {
	SHA1Context ctx;
	ctx.update(data, len);
	return ctx.finish();
}

SHA1Digest sha1(const Common::String &data) {
	return sha1((const byte *)data.c_str(), data.size());
}

} // End of namespace Inspector
