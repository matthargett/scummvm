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

#include "common/inspector/websocket.h"

namespace Inspector {

WebSocketCodec::WebSocketCodec() :
	_bufferStart(0),
	_assembling(false),
	_assemblingOpcode(0),
	_failed(false),
	_failStatus(kWSStatusNormal),
	_maxMessageSize(8 * 1024 * 1024) {
}

void WebSocketCodec::fail(uint16 status) {
	if (_failed)
		return;
	_failed = true;
	_failStatus = status;
	_messages.clear();
}

void WebSocketCodec::addData(const byte *data, uint32 len) {
	if (_failed || len == 0)
		return;
	uint32 oldSize = _buffer.size();
	_buffer.resize(oldSize + len);
	memcpy(_buffer.begin() + oldSize, data, len);
	decode();
}

bool WebSocketCodec::nextMessage(Message &out) {
	if (_failed || _messages.empty())
		return false;
	out = _messages[0];
	_messages.remove_at(0);
	return true;
}

void WebSocketCodec::consume(uint32 n) {
	_bufferStart += n;
}

void WebSocketCodec::compact() {
	if (_bufferStart == 0)
		return;
	uint32 remaining = _buffer.size() - _bufferStart;
	if (remaining > 0)
		memmove(_buffer.begin(), _buffer.begin() + _bufferStart, remaining);
	_buffer.resize(remaining);
	_bufferStart = 0;
}

void WebSocketCodec::decode() {
	// Decode as many complete frames as the buffer holds. Header layout
	// (RFC 6455 5.2):
	//   byte 0: FIN(1) RSV(3) opcode(4)
	//   byte 1: MASK(1) len(7); len 126 -> +2 bytes, len 127 -> +8 bytes
	//   if MASK: 4 byte masking key
	while (!_failed) {
		if (available() < 2)
			break;
		const byte *p = head();
		const byte b0 = p[0];
		const byte b1 = p[1];
		const bool fin = (b0 & 0x80) != 0;
		const byte rsv = b0 & 0x70;
		const byte opcode = b0 & 0x0F;
		const bool masked = (b1 & 0x80) != 0;
		uint32 headerLen = 2;
		uint64 payloadLen = b1 & 0x7F;

		if (rsv != 0) {
			// No extension was negotiated, so RSV bits must be zero.
			fail(kWSStatusProtocolError);
			return;
		}

		if (payloadLen == 126) {
			if (available() < headerLen + 2)
				break;
			payloadLen = ((uint32)p[2] << 8) | p[3];
			headerLen += 2;
		} else if (payloadLen == 127) {
			if (available() < headerLen + 8)
				break;
			payloadLen = 0;
			for (int i = 0; i < 8; i++)
				payloadLen = (payloadLen << 8) | p[2 + i];
			headerLen += 8;
			if (payloadLen >> 63) {
				// RFC 6455 5.2: the most significant bit MUST be 0.
				fail(kWSStatusProtocolError);
				return;
			}
		}

		// A frame that is not masked is a protocol violation on the
		// server side (RFC 6455 5.1) — close with 1002.
		if (!masked) {
			fail(kWSStatusProtocolError);
			return;
		}
		headerLen += 4; // masking key

		if (payloadLen > _maxMessageSize ||
		    (uint64)_assembled.size() + payloadLen > _maxMessageSize) {
			fail(kWSStatusMessageTooBig);
			return;
		}

		if (available() < headerLen + payloadLen)
			break; // whole frame not here yet

		const byte *maskKey = p + headerLen - 4;
		const byte *payload = p + headerLen;
		Common::String data;
		for (uint32 i = 0; i < payloadLen; i++)
			data += (char)(payload[i] ^ maskKey[i % 4]);

		consume(headerLen + (uint32)payloadLen);

		const bool isControl = (opcode & 0x8) != 0;
		if (isControl) {
			// Control frames must not be fragmented and are <= 125 bytes.
			if (!fin || payloadLen > 125) {
				fail(kWSStatusProtocolError);
				return;
			}
			deliverControl(opcode, data);
		} else {
			deliverData(opcode, data, fin);
		}
	}
	compact();
}

void WebSocketCodec::deliverControl(byte opcode, const Common::String &payload) {
	Message m;
	switch (opcode) {
	case kOpPing:
		m.type = Message::kTypePing;
		break;
	case kOpPong:
		m.type = Message::kTypePong;
		break;
	case kOpClose:
		m.type = Message::kTypeClose;
		if (payload.size() >= 2)
			m.closeStatus = ((uint16)(byte)payload[0] << 8) | (byte)payload[1];
		else
			m.closeStatus = kWSStatusNormal;
		break;
	default:
		fail(kWSStatusProtocolError);
		return;
	}
	m.payload = payload;
	_messages.push_back(m);
}

void WebSocketCodec::deliverData(byte opcode, const Common::String &payload, bool fin) {
	if (opcode == kOpContinuation) {
		if (!_assembling) {
			// Continuation with nothing to continue.
			fail(kWSStatusProtocolError);
			return;
		}
		_assembled += payload;
	} else if (opcode == kOpText || opcode == kOpBinary) {
		if (_assembling) {
			// A new data frame may not start while another message is
			// still being reassembled; only control frames may interleave.
			fail(kWSStatusProtocolError);
			return;
		}
		_assembling = true;
		_assemblingOpcode = opcode;
		_assembled = payload;
	} else {
		fail(kWSStatusProtocolError);
		return;
	}

	if (!fin)
		return;

	// Message complete.
	Message m;
	if (_assemblingOpcode == kOpText) {
		if (!isValidUtf8((const byte *)_assembled.c_str(), _assembled.size())) {
			fail(kWSStatusInvalidPayload);
			return;
		}
		m.type = Message::kTypeText;
	} else {
		m.type = Message::kTypeBinary;
	}
	m.payload = _assembled;
	_messages.push_back(m);
	_assembling = false;
	_assembled.clear();
}

Common::String WebSocketCodec::encodeFrame(byte opcode, const byte *payload, uint32 len) {
	Common::String out;
	out += (char)(0x80 | (opcode & 0x0F)); // FIN, no RSV
	if (len <= 125) {
		out += (char)len;
	} else if (len <= 65535) {
		out += (char)126;
		out += (char)(len >> 8);
		out += (char)(len & 0xFF);
	} else {
		out += (char)127;
		for (int i = 7; i >= 0; i--)
			out += (char)((i >= 4) ? 0 : (len >> (i * 8)) & 0xFF);
	}
	for (uint32 i = 0; i < len; i++)
		out += (char)payload[i];
	return out;
}

Common::String WebSocketCodec::encodeText(const Common::String &payload) {
	return encodeFrame(kOpText, (const byte *)payload.c_str(), payload.size());
}

Common::String WebSocketCodec::encodePong(const Common::String &payload) {
	return encodeFrame(kOpPong, (const byte *)payload.c_str(), payload.size());
}

Common::String WebSocketCodec::encodePing(const Common::String &payload) {
	return encodeFrame(kOpPing, (const byte *)payload.c_str(), payload.size());
}

Common::String WebSocketCodec::encodeClose(uint16 status) {
	byte payload[2];
	payload[0] = (byte)(status >> 8);
	payload[1] = (byte)(status & 0xFF);
	return encodeFrame(kOpClose, payload, 2);
}

Common::String WebSocketCodec::encodeClientFrame(byte opcode, const byte *payload, uint32 len,
                                                 uint32 maskKey, bool fin) {
	Common::String out;
	out += (char)((fin ? 0x80 : 0x00) | (opcode & 0x0F));
	if (len <= 125) {
		out += (char)(0x80 | len);
	} else if (len <= 65535) {
		out += (char)(0x80 | 126);
		out += (char)(len >> 8);
		out += (char)(len & 0xFF);
	} else {
		out += (char)(0x80 | 127);
		for (int i = 7; i >= 0; i--)
			out += (char)((i >= 4) ? 0 : (len >> (i * 8)) & 0xFF);
	}
	byte key[4];
	key[0] = (byte)(maskKey >> 24);
	key[1] = (byte)(maskKey >> 16);
	key[2] = (byte)(maskKey >> 8);
	key[3] = (byte)(maskKey);
	for (int i = 0; i < 4; i++)
		out += (char)key[i];
	for (uint32 i = 0; i < len; i++)
		out += (char)(payload[i] ^ key[i % 4]);
	return out;
}

bool WebSocketCodec::isValidUtf8(const byte *data, uint32 len) {
	uint32 i = 0;
	while (i < len) {
		byte b = data[i];
		uint32 cp;
		uint32 extra;
		if (b < 0x80) {
			i++;
			continue;
		} else if ((b & 0xE0) == 0xC0) {
			cp = b & 0x1F;
			extra = 1;
		} else if ((b & 0xF0) == 0xE0) {
			cp = b & 0x0F;
			extra = 2;
		} else if ((b & 0xF8) == 0xF0) {
			cp = b & 0x07;
			extra = 3;
		} else {
			return false; // bare continuation byte or 5/6-byte form
		}
		if (i + extra >= len)
			return false; // truncated sequence
		for (uint32 j = 1; j <= extra; j++) {
			byte c = data[i + j];
			if ((c & 0xC0) != 0x80)
				return false;
			cp = (cp << 6) | (c & 0x3F);
		}
		// Reject overlong encodings.
		static const uint32 minCp[4] = { 0, 0x80, 0x800, 0x10000 };
		if (cp < minCp[extra])
			return false;
		// Reject UTF-16 surrogates and out-of-range code points.
		if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
			return false;
		i += extra + 1;
	}
	return true;
}

} // End of namespace Inspector
