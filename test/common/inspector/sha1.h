#include <cxxtest/TestSuite.h>

#include "common/inspector/sha1.h"

/**
 * SHA-1 (RFC 3174) — needed for the RFC 6455 Sec-WebSocket-Accept
 * computation. Vectors from RFC 3174 section 7.3 plus the WebSocket
 * handshake example from RFC 6455 section 1.3.
 */
class InspectorSha1TestSuite : public CxxTest::TestSuite {
public:
	void test_empty_string() {
		TS_ASSERT_EQUALS(Inspector::sha1(Common::String()).toHex(),
		                 "da39a3ee5e6b4b0d3255bfef95601890afd80709");
	}

	void test_abc() {
		TS_ASSERT_EQUALS(Inspector::sha1("abc").toHex(),
		                 "a9993e364706816aba3e25717850c26c9cd0d89d");
	}

	void test_two_block_message() {
		TS_ASSERT_EQUALS(
			Inspector::sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").toHex(),
			"84983e441c3bd26ebaae4aa1f95129e5e54670f1");
	}

	void test_million_a() {
		Common::String s;
		for (int i = 0; i < 1000000; i++)
			s += 'a';
		TS_ASSERT_EQUALS(Inspector::sha1(s).toHex(),
		                 "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
	}

	// Length padding boundaries: messages of 55, 56 and 64 bytes exercise
	// the "length no longer fits in the current block" padding paths.
	void test_padding_boundaries() {
		Common::String s55, s56, s64;
		for (int i = 0; i < 64; i++) {
			if (i < 55)
				s55 += 'x';
			if (i < 56)
				s56 += 'x';
			s64 += 'x';
		}
		TS_ASSERT_EQUALS(Inspector::sha1(s55).toHex(),
		                 "cef734ba81a024479e09eb5a75b6ddae62e6abf1");
		TS_ASSERT_EQUALS(Inspector::sha1(s56).toHex(),
		                 "901305367c259952f4e7af8323f480d59f81335b");
		TS_ASSERT_EQUALS(Inspector::sha1(s64).toHex(),
		                 "bb2fa3ee7afb9f54c6dfb5d021f14b1ffe40c163");
	}

	// RFC 6455 section 1.3: the value a WebSocket server must compute for
	// Sec-WebSocket-Key "dGhlIHNhbXBsZSBub25jZQ==". The base64 of this
	// digest must equal "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=".
	void test_rfc6455_handshake_vector() {
		Common::String input = "dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
		TS_ASSERT_EQUALS(Inspector::sha1(input).toHex(),
		                 "b37a4f2cc0624f1690f64606cf385945b2bec4ea");
	}
};
