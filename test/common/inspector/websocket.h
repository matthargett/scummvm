#include <cxxtest/TestSuite.h>

#include "common/inspector/websocket.h"

/**
 * RFC 6455 frame codec corner cases, several of them taken from bugs hit
 * by non-Chrome CDP/WebSocket implementors (see common/inspector/DESIGN.md,
 * "corner-case ledger" items 15-20).
 */
class InspectorWebSocketTestSuite : public CxxTest::TestSuite {
	typedef Inspector::WebSocketCodec Codec;
	typedef Inspector::WebSocketCodec::Message Message;

	static void feed(Codec &c, const Common::String &bytes) {
		c.addData((const byte *)bytes.c_str(), bytes.size());
	}

	static Common::String clientText(const Common::String &payload, uint32 mask = 0xA1B2C3D4) {
		return Codec::encodeClientFrame(0x1, (const byte *)payload.c_str(), payload.size(), mask);
	}

public:
	// --- decoding ---

	void test_masked_client_text_frame_roundtrip() {
		Codec c;
		feed(c, clientText("{\"id\":1,\"method\":\"Debugger.enable\"}"));
		Message m;
		TS_ASSERT(c.nextMessage(m));
		TS_ASSERT_EQUALS(m.type, Message::kTypeText);
		TS_ASSERT_EQUALS(m.payload, "{\"id\":1,\"method\":\"Debugger.enable\"}");
		TS_ASSERT(!c.nextMessage(m));
		TS_ASSERT(!c.failed());
	}

	// RFC 6455 5.1: a server MUST close the connection upon receiving a
	// frame that is not masked (ledger #15).
	void test_unmasked_client_frame_fails_1002() {
		Codec c;
		Common::String serverStyle = Codec::encodeText("hello");
		feed(c, serverStyle);
		Message m;
		TS_ASSERT(!c.nextMessage(m));
		TS_ASSERT(c.failed());
		TS_ASSERT_EQUALS(c.failStatus(), (uint16)Inspector::kWSStatusProtocolError);
	}

	// Payload length representation boundaries: 125 (7-bit), 126 and 65535
	// (16-bit), 65536 (64-bit) — ledger #16.
	void test_payload_length_boundaries() {
		static const uint32 sizes[] = { 0, 125, 126, 65535, 65536 };
		for (uint32 i = 0; i < ARRAYSIZE(sizes); i++) {
			Codec c;
			Common::String payload;
			for (uint32 j = 0; j < sizes[i]; j++)
				payload += (char)('a' + (j % 26));
			feed(c, clientText(payload));
			Message m;
			TS_ASSERT(c.nextMessage(m));
			TS_ASSERT_EQUALS(m.payload.size(), sizes[i]);
			TS_ASSERT_EQUALS(m.payload, payload);
			TS_ASSERT(!c.failed());
		}
	}

	// TCP chunking: deliver a frame one byte at a time (ledger #20).
	void test_byte_at_a_time_delivery() {
		Codec c;
		Common::String wire = clientText("stepOver");
		for (uint32 i = 0; i < wire.size(); i++)
			c.addData((const byte *)wire.c_str() + i, 1);
		Message m;
		TS_ASSERT(c.nextMessage(m));
		TS_ASSERT_EQUALS(m.payload, "stepOver");
	}

	// Two messages arriving in a single read must both decode.
	void test_two_messages_one_chunk() {
		Codec c;
		feed(c, clientText("first") + clientText("second", 0x00000000));
		Message m;
		TS_ASSERT(c.nextMessage(m));
		TS_ASSERT_EQUALS(m.payload, "first");
		TS_ASSERT(c.nextMessage(m));
		TS_ASSERT_EQUALS(m.payload, "second");
	}

	// Fragmented message: text start frame (FIN=0), continuation (FIN=0),
	// final continuation (FIN=1); a ping interleaved between fragments
	// must surface as its own control message (ledger #17).
	void test_fragmentation_with_interleaved_ping() {
		Codec c;
		Common::String wire;
		wire += Codec::encodeClientFrame(0x1, (const byte *)"Debugger", 8, 0x11223344, false);
		wire += Codec::encodeClientFrame(0x9, (const byte *)"marco", 5, 0x55667788, true); // ping
		wire += Codec::encodeClientFrame(0x0, (const byte *)".", 1, 0x99AABBCC, false);
		wire += Codec::encodeClientFrame(0x0, (const byte *)"pause", 5, 0xDDEEFF00, true);
		feed(c, wire);

		// The ping completes before the fragmented message does.
		Message m;
		TS_ASSERT(c.nextMessage(m));
		TS_ASSERT_EQUALS(m.type, Message::kTypePing);
		TS_ASSERT_EQUALS(m.payload, "marco");

		TS_ASSERT(c.nextMessage(m));
		TS_ASSERT_EQUALS(m.type, Message::kTypeText);
		TS_ASSERT_EQUALS(m.payload, "Debugger.pause");
		TS_ASSERT(!c.failed());
	}

	// A continuation frame with no message in progress is a protocol error.
	void test_orphan_continuation_fails() {
		Codec c;
		feed(c, Codec::encodeClientFrame(0x0, (const byte *)"x", 1, 0x01020304, true));
		Message m;
		TS_ASSERT(!c.nextMessage(m));
		TS_ASSERT(c.failed());
		TS_ASSERT_EQUALS(c.failStatus(), (uint16)Inspector::kWSStatusProtocolError);
	}

	// A new data frame while another message is being reassembled is a
	// protocol error (control frames are the only legal interleave).
	void test_interleaved_data_frame_fails() {
		Codec c;
		Common::String wire;
		wire += Codec::encodeClientFrame(0x1, (const byte *)"abc", 3, 0x01020304, false);
		wire += Codec::encodeClientFrame(0x1, (const byte *)"def", 3, 0x05060708, true);
		feed(c, wire);
		Message m;
		TS_ASSERT(!c.nextMessage(m));
		TS_ASSERT(c.failed());
	}

	// Control frames must not be fragmented (FIN=0 ping is illegal).
	void test_fragmented_control_frame_fails() {
		Codec c;
		feed(c, Codec::encodeClientFrame(0x9, (const byte *)"p", 1, 0x01020304, false));
		Message m;
		TS_ASSERT(!c.nextMessage(m));
		TS_ASSERT(c.failed());
		TS_ASSERT_EQUALS(c.failStatus(), (uint16)Inspector::kWSStatusProtocolError);
	}

	// Close frame carries a big-endian status code (ledger #18).
	void test_close_frame_status() {
		Codec c;
		byte payload[2] = { 0x03, 0xE9 }; // 1001 going away
		feed(c, Codec::encodeClientFrame(0x8, payload, 2, 0x01020304, true));
		Message m;
		TS_ASSERT(c.nextMessage(m));
		TS_ASSERT_EQUALS(m.type, Message::kTypeClose);
		TS_ASSERT_EQUALS(m.closeStatus, 1001);
	}

	// Invalid UTF-8 in a text message must fail the connection with 1007
	// (ledger #19; the npm 'ws' client hard-closes on this, ws#2252).
	void test_invalid_utf8_text_fails_1007() {
		Codec c;
		byte bad[3] = { 'a', 0xC0, 0xAF }; // overlong '/', classic exploit bytes
		feed(c, Codec::encodeClientFrame(0x1, bad, 3, 0x01020304, true));
		Message m;
		TS_ASSERT(!c.nextMessage(m));
		TS_ASSERT(c.failed());
		TS_ASSERT_EQUALS(c.failStatus(), (uint16)Inspector::kWSStatusInvalidPayload);
	}

	// Lone UTF-16 surrogate encoded as UTF-8 (CESU-8 style) is invalid.
	void test_surrogate_utf8_fails() {
		Codec c;
		byte bad[3] = { 0xED, 0xA0, 0x80 }; // U+D800
		feed(c, Codec::encodeClientFrame(0x1, bad, 3, 0x01020304, true));
		Message m;
		TS_ASSERT(!c.nextMessage(m));
		TS_ASSERT(c.failed());
		TS_ASSERT_EQUALS(c.failStatus(), (uint16)Inspector::kWSStatusInvalidPayload);
	}

	// Valid multi-byte UTF-8 passes.
	void test_valid_multibyte_utf8() {
		Codec c;
		// "λ 💩" — 2-byte, ASCII, 4-byte sequences.
		byte good[] = { 0xCE, 0xBB, ' ', 0xF0, 0x9F, 0x92, 0xA9 };
		feed(c, Codec::encodeClientFrame(0x1, good, sizeof(good), 0x01020304, true));
		Message m;
		TS_ASSERT(c.nextMessage(m));
		TS_ASSERT_EQUALS(m.payload.size(), sizeof(good));
		TS_ASSERT(!c.failed());
	}

	// Binary frames are not UTF-8 validated.
	void test_binary_frame_not_utf8_validated() {
		Codec c;
		byte bin[4] = { 0xFF, 0xFE, 0x00, 0x80 };
		feed(c, Codec::encodeClientFrame(0x2, bin, 4, 0x01020304, true));
		Message m;
		TS_ASSERT(c.nextMessage(m));
		TS_ASSERT_EQUALS(m.type, Message::kTypeBinary);
		TS_ASSERT_EQUALS(m.payload.size(), 4u);
		TS_ASSERT(!c.failed());
	}

	// Message size cap → 1009 (ledger: keep single CDP messages bounded).
	void test_message_too_big_fails_1009() {
		Codec c;
		c.setMaxMessageSize(64);
		Common::String big;
		for (int i = 0; i < 65; i++)
			big += 'x';
		feed(c, clientText(big));
		Message m;
		TS_ASSERT(!c.nextMessage(m));
		TS_ASSERT(c.failed());
		TS_ASSERT_EQUALS(c.failStatus(), (uint16)Inspector::kWSStatusMessageTooBig);
	}

	// Reserved bits must be zero (no extension was negotiated).
	void test_rsv_bits_fail() {
		Codec c;
		Common::String wire = clientText("x");
		// Set RSV1 in the first byte.
		Common::String tampered;
		tampered += (char)((byte)wire[0] | 0x40);
		for (uint32 i = 1; i < wire.size(); i++)
			tampered += wire[i];
		feed(c, tampered);
		Message m;
		TS_ASSERT(!c.nextMessage(m));
		TS_ASSERT(c.failed());
		TS_ASSERT_EQUALS(c.failStatus(), (uint16)Inspector::kWSStatusProtocolError);
	}

	// --- encoding ---

	// Server-to-client frames must NOT be masked (clients kill masked
	// server frames) and must use minimal length encodings.
	void test_server_frame_unmasked_and_lengths() {
		Common::String small = Codec::encodeText("hi");
		TS_ASSERT_EQUALS((byte)small[0], 0x81); // FIN | text
		TS_ASSERT_EQUALS((byte)small[1], 0x02); // unmasked, len 2
		TS_ASSERT_EQUALS(small.size(), 4u);

		Common::String mid;
		for (int i = 0; i < 126; i++)
			mid += 'x';
		Common::String midWire = Codec::encodeText(mid);
		TS_ASSERT_EQUALS((byte)midWire[1], 126); // 16-bit length marker
		TS_ASSERT_EQUALS((byte)midWire[2], 0x00);
		TS_ASSERT_EQUALS((byte)midWire[3], 126);
		TS_ASSERT_EQUALS(midWire.size(), 4u + 126u);

		Common::String big;
		for (int i = 0; i < 65536; i++)
			big += 'x';
		Common::String bigWire = Codec::encodeText(big);
		TS_ASSERT_EQUALS((byte)bigWire[1], 127); // 64-bit length marker
		TS_ASSERT_EQUALS(bigWire.size(), 10u + 65536u);
		// Big-endian 64-bit length: 00 00 00 00 00 01 00 00 at offsets 2-9.
		TS_ASSERT_EQUALS((byte)bigWire[6], 0x00);
		TS_ASSERT_EQUALS((byte)bigWire[7], 0x01);
		TS_ASSERT_EQUALS((byte)bigWire[8], 0x00);
		TS_ASSERT_EQUALS((byte)bigWire[9], 0x00);
	}

	void test_encode_close_status() {
		Common::String wire = Codec::encodeClose(1007);
		TS_ASSERT_EQUALS((byte)wire[0], 0x88);
		TS_ASSERT_EQUALS((byte)wire[1], 0x02);
		TS_ASSERT_EQUALS((byte)wire[2], 0x03);
		TS_ASSERT_EQUALS((byte)wire[3], 0xEF);
	}

	// Pong must echo the ping payload byte-for-byte (RFC 6455 5.5.3).
	void test_pong_echoes_payload() {
		Common::String wire = Codec::encodePong("marco");
		TS_ASSERT_EQUALS((byte)wire[0], 0x8A);
		TS_ASSERT_EQUALS(wire.size(), 7u);
		TS_ASSERT_EQUALS(Common::String(wire.c_str() + 2), "marco");
	}

	// A server-encoded frame fed back to the decoder must be rejected for
	// the *masking* reason, while a client-encoded frame with zero mask key
	// still counts as masked (the MASK bit is what matters, not the key).
	void test_zero_mask_key_is_still_masked() {
		Codec c;
		feed(c, clientText("ok", 0x00000000));
		Message m;
		TS_ASSERT(c.nextMessage(m));
		TS_ASSERT_EQUALS(m.payload, "ok");
	}

	// UTF-8 validator edge cases, directly.
	void test_utf8_validator() {
		TS_ASSERT(Codec::isValidUtf8((const byte *)"", 0));
		TS_ASSERT(Codec::isValidUtf8((const byte *)"plain ascii", 11));
		byte maxCp[4] = { 0xF4, 0x8F, 0xBF, 0xBF }; // U+10FFFF
		TS_ASSERT(Codec::isValidUtf8(maxCp, 4));
		byte beyond[4] = { 0xF4, 0x90, 0x80, 0x80 }; // U+110000
		TS_ASSERT(!Codec::isValidUtf8(beyond, 4));
		byte truncated[2] = { 0xE2, 0x82 }; // needs 3 bytes
		TS_ASSERT(!Codec::isValidUtf8(truncated, 2));
		byte overlongNul[2] = { 0xC0, 0x80 }; // modified-UTF-8 NUL
		TS_ASSERT(!Codec::isValidUtf8(overlongNul, 2));
		byte bareCont[1] = { 0x80 };
		TS_ASSERT(!Codec::isValidUtf8(bareCont, 1));
	}
};
