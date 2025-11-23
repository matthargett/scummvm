#include "cxxtest/TestSuite.h"
#include "common/array.h"

// Simple replication of the Playdate blit bit-packing logic: map a 1-byte-per-pixel
// source row into a 1-bit-per-pixel destination row using MSB-first packing.
static void packPlaydateRow(const uint8_t *srcRow, int srcW, int destW, int offsetX, uint8_t *dstRow, int stride) {
	// Clear only the bytes we will touch
	const int startByte = offsetX / 8;
	const int endByte = (offsetX + destW + 7) / 8;
	for (int b = startByte; b < endByte; ++b)
		dstRow[b] = 0;

	for (int x = 0; x < destW; ++x) {
		const int destX = x + offsetX;
		const int srcX = (x < srcW) ? x : srcW - 1;
		if (!srcRow[srcX])
			continue;
		const int byteIndex = destX / 8;
		const int bitIndex = 7 - (destX % 8);
		dstRow[byteIndex] |= (1 << bitIndex);
	}
}

class PlaydateBitPackTestSuite : public CxxTest::TestSuite {
public:
	void testStraightPack() {
		// 8 pixels -> one byte: 00110011 => 0x33
		uint8_t src[8] = {0, 0, 1, 1, 0, 0, 1, 1};
		uint8_t dst[2] = {0xFF, 0xFF};
		packPlaydateRow(src, 8, 8, 0, dst, 2);
		TS_ASSERT_EQUALS(dst[0], 0x33);
		TS_ASSERT_EQUALS(dst[1], 0xFF); // untouched byte remains unchanged
	}

	void testOffsetPack() {
		// 16 pixels -> two bytes, offset zero: 10101010 10101010 -> 0xAA 0xAA
		uint8_t src[16];
		for (int i = 0; i < 16; ++i)
			src[i] = (i & 1) ? 0 : 1;
		uint8_t dst[4] = {0};
		packPlaydateRow(src, 16, 16, 0, dst, 4);
		TS_ASSERT_EQUALS(dst[0], 0xAA);
		TS_ASSERT_EQUALS(dst[1], 0xAA);
		TS_ASSERT_EQUALS(dst[2], 0x00); // untouched
		TS_ASSERT_EQUALS(dst[3], 0x00);
	}

	void testOffsetIntoRow() {
		// Same pattern but starting at offset 8; expect first byte untouched, second byte 0xAA.
		uint8_t src[16];
		for (int i = 0; i < 16; ++i)
			src[i] = (i & 1) ? 0 : 1;
		uint8_t dst[4] = {0xFF, 0xFF, 0, 0};
		packPlaydateRow(src, 16, 8, 8, dst, 4);
		TS_ASSERT_EQUALS(dst[0], 0xFF); // untouched before offset
		TS_ASSERT_EQUALS(dst[1], 0xAA); // packed at offset
	}
};
