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

#ifndef COMMON_INSPECTOR_WEBSOCKET_H
#define COMMON_INSPECTOR_WEBSOCKET_H

#include "common/scummsys.h"
#include "common/array.h"
#include "common/str.h"

namespace Inspector {

/**
 * RFC 6455 WebSocket framing, transport-agnostic.
 *
 * The decoder is fed raw bytes in arbitrary chunks (TCP gives no framing
 * guarantees) and yields complete application messages. It implements the
 * server side of the protocol:
 *  - client-to-server frames MUST be masked; an unmasked frame is a
 *    protocol violation and fails the connection with status 1002
 *  - 7/16/64-bit payload length encodings
 *  - fragmented messages (continuation frames), with control frames
 *    (ping/pong/close) legally interleaved between fragments
 *  - text messages are validated as UTF-8; violations fail with 1007
 *  - a configurable maximum message size fails with 1009
 *
 * Chrome DevTools Protocol traffic is exclusively text frames carrying one
 * JSON object per message, but a correct server must still accept the rest
 * of the framing layer: some client stacks fragment large payloads and
 * interleave pings.
 */

/** WebSocket close status codes used by this implementation. */
enum WebSocketStatus {
	kWSStatusNormal          = 1000,
	kWSStatusGoingAway       = 1001,
	kWSStatusProtocolError   = 1002,
	kWSStatusInvalidPayload  = 1007, ///< non-UTF-8 data in a text message
	kWSStatusMessageTooBig   = 1009
};

class WebSocketCodec {
public:
	struct Message {
		enum Type {
			kTypeText,
			kTypeBinary,
			kTypePing,
			kTypePong,
			kTypeClose
		};

		Type type;
		Common::String payload;  ///< message bytes (also used for binary data)
		uint16 closeStatus;      ///< only meaningful for kTypeClose

		Message() : type(kTypeText), closeStatus(kWSStatusNormal) {}
	};

	WebSocketCodec();

	/** Feed raw transport bytes; any chunking, including one byte at a time. */
	void addData(const byte *data, uint32 len);

	/**
	 * Fetch the next completely decoded message.
	 * @return false if no complete message is pending.
	 */
	bool nextMessage(Message &out);

	/** True once a protocol violation was detected; no further decoding happens. */
	bool failed() const { return _failed; }

	/** Close status the server should fail the connection with. */
	uint16 failStatus() const { return _failStatus; }

	/**
	 * Cap on the total reassembled message size (default 8 MiB). CDP
	 * messages should stay well below client-side caps (the 'ws' npm
	 * package kills connections above 100 MiB).
	 */
	void setMaxMessageSize(uint32 maxSize) { _maxMessageSize = maxSize; }

	// --- Encoding (server side: frames are never masked) ---

	/** Encode a single unfragmented frame with the given opcode. */
	static Common::String encodeFrame(byte opcode, const byte *payload, uint32 len);

	static Common::String encodeText(const Common::String &payload);
	static Common::String encodePong(const Common::String &payload);
	static Common::String encodePing(const Common::String &payload);
	static Common::String encodeClose(uint16 status);

	/**
	 * Encode a client-to-server frame (masked) — used by unit tests to
	 * simulate a client, and usable by a future client-mode transport.
	 */
	static Common::String encodeClientFrame(byte opcode, const byte *payload, uint32 len,
	                                        uint32 maskKey, bool fin = true);

	/** Validate a byte buffer as well-formed UTF-8 (rejects overlong forms,
	 *  surrogate code points and values above U+10FFFF). */
	static bool isValidUtf8(const byte *data, uint32 len);

private:
	enum Opcode {
		kOpContinuation = 0x0,
		kOpText         = 0x1,
		kOpBinary       = 0x2,
		kOpClose        = 0x8,
		kOpPing         = 0x9,
		kOpPong         = 0xA
	};

	Common::Array<byte> _buffer;      ///< undecoded transport bytes
	uint32 _bufferStart;              ///< consumed prefix of _buffer

	Common::Array<Message> _messages; ///< decoded, ready for pickup

	// Reassembly state for a fragmented data message.
	bool _assembling;
	byte _assemblingOpcode;
	Common::String _assembled;

	bool _failed;
	uint16 _failStatus;
	uint32 _maxMessageSize;

	void fail(uint16 status);
	void decode();
	uint32 available() const { return _buffer.size() - _bufferStart; }
	const byte *head() const { return _buffer.begin() + _bufferStart; }
	void consume(uint32 n);
	void compact();
	void deliverData(byte opcode, const Common::String &payload, bool fin);
	void deliverControl(byte opcode, const Common::String &payload);
};

} // End of namespace Inspector

#endif
