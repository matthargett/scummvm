#include <cxxtest/TestSuite.h>

#include "common/inspector/sourcemap.h"
#include "common/formats/json.h"
#include "common/base64.h"

/**
 * Source Map v3 generation — VLQ corner cases from nodejs/node#31490 and
 * the ECMA-426 relative-offset semantics (ledger #22, #23).
 */
class InspectorSourceMapTestSuite : public CxxTest::TestSuite {
	typedef Inspector::SourceMapBuilder Builder;

	static Common::String vlq(int32 v) {
		Common::String out;
		Inspector::SourceMapBuilder::encodeVLQ(v, out);
		return out;
	}

public:
	void test_vlq_known_vectors() {
		TS_ASSERT_EQUALS(vlq(0), "A");
		TS_ASSERT_EQUALS(vlq(1), "C");
		TS_ASSERT_EQUALS(vlq(-1), "D");
		TS_ASSERT_EQUALS(vlq(2), "E");
		TS_ASSERT_EQUALS(vlq(-2), "F");
		TS_ASSERT_EQUALS(vlq(15), "e");
		TS_ASSERT_EQUALS(vlq(-15), "f");
		TS_ASSERT_EQUALS(vlq(16), "gB");
		TS_ASSERT_EQUALS(vlq(123), "2H");
		TS_ASSERT_EQUALS(vlq(1000), "w+B");
		TS_ASSERT_EQUALS(vlq(-1000), "x+B");
	}

	// nodejs/node#31490: values with the 31st bit set break encoders that
	// use arithmetic (sign-extending) right shift; bytecode-offset columns
	// are huge by design, so these must round-trip exactly.
	void test_vlq_huge_values() {
		TS_ASSERT_EQUALS(vlq(2147483647), "+/////D");
		TS_ASSERT_EQUALS(vlq(-2147483647), "//////D");
		TS_ASSERT_EQUALS(vlq(1073741824), "ggggggC");

		uint32 pos = 0;
		int32 v = 0;
		Common::String enc = vlq(2147483647);
		TS_ASSERT(Builder::decodeVLQ(enc, pos, v));
		TS_ASSERT_EQUALS(v, 2147483647);

		pos = 0;
		enc = vlq(-2147483647);
		TS_ASSERT(Builder::decodeVLQ(enc, pos, v));
		TS_ASSERT_EQUALS(v, -2147483647);
	}

	// INT32_MIN encodes as "negative zero" and breaks the round-trip;
	// the encoder must refuse it (nodejs/node#31490).
	void test_vlq_int32_min_rejected() {
		Common::String out;
		TS_ASSERT(!Builder::encodeVLQ((int32)-2147483647 - 1, out));
		TS_ASSERT_EQUALS(out.size(), 0u);
	}

	void test_vlq_roundtrip_sweep() {
		static const int32 values[] = {
			0, 1, -1, 15, -16, 31, 32, -33, 1024, -4095, 65536, -65537,
			(int32)0x3FFFFFFF, (int32)-0x40000000
		};
		for (uint32 i = 0; i < ARRAYSIZE(values); i++) {
			Common::String enc;
			TS_ASSERT(Builder::encodeVLQ(values[i], enc));
			uint32 pos = 0;
			int32 back = 12345;
			TS_ASSERT(Builder::decodeVLQ(enc, pos, back));
			TS_ASSERT_EQUALS(back, values[i]);
			TS_ASSERT_EQUALS(pos, enc.size());
		}
	}

	void test_vlq_decode_rejects_malformed() {
		uint32 pos = 0;
		int32 v;
		// Dangling continuation bit.
		Common::String dangling = "g";
		TS_ASSERT(!Builder::decodeVLQ(dangling, pos, v));
		// Not a base64 character.
		pos = 0;
		Common::String bogus = "!";
		TS_ASSERT(!Builder::decodeVLQ(bogus, pos, v));
		// Unsigned accumulation beyond 2^32 must be rejected (ECMA-426).
		pos = 0;
		Common::String tooBig = "ggggggggE"; // > 32 bits of payload
		TS_ASSERT(!Builder::decodeVLQ(tooBig, pos, v));
	}

	// ECMA-426: the generated-column field resets at each generated line;
	// source index/line/column deltas do NOT — a generator that resets all
	// four appears to work on line one and drifts afterwards (ledger #23).
	void test_mappings_relative_semantics() {
		Builder b("listing.txt");
		int src = b.addSource("room11.lsp", "(if (== V13 5)\n  (print \"hi\"))\n");
		TS_ASSERT_EQUALS(src, 0);
		TS_ASSERT(b.addMapping(0, 0, src, 0, 0));
		TS_ASSERT(b.addMapping(1, 0, src, 0, 4));
		TS_ASSERT(b.addMapping(2, 0, src, 1, 2));
		// generated line 3 has no mappings
		TS_ASSERT(b.addMapping(4, 0, src, 1, 2));

		Common::String json = b.build();
		Common::JSONValue *root = Common::JSON::parse(json.c_str());
		TS_ASSERT(root);
		if (!root)
			return;
		const Common::JSONObject &obj = root->asObject();
		TS_ASSERT_EQUALS(obj["version"]->asIntegerNumber(), 3);
		TS_ASSERT_EQUALS(obj["file"]->asString(), "listing.txt");
		TS_ASSERT_EQUALS(obj["mappings"]->asString(), "AAAA;AAAI;AACF;;AAAA");
		TS_ASSERT_EQUALS(obj["sources"]->asArray()[0]->asString(), "room11.lsp");
		TS_ASSERT_EQUALS(obj["sourcesContent"]->asArray()[0]->asString(),
		                 "(if (== V13 5)\n  (print \"hi\"))\n");
		delete root;
	}

	// Several segments on one generated line: comma-separated, column
	// delta relative to the previous segment on the same line.
	void test_multiple_segments_per_line() {
		Builder b("l");
		int src = b.addSource("s", "x");
		TS_ASSERT(b.addMapping(0, 0, src, 0, 0));
		TS_ASSERT(b.addMapping(0, 5, src, 0, 8));
		Common::String json = b.build();
		Common::JSONValue *root = Common::JSON::parse(json.c_str());
		TS_ASSERT(root);
		if (!root)
			return;
		TS_ASSERT_EQUALS(root->asObject()["mappings"]->asString(), "AAAA,KAAQ");
		delete root;
	}

	// 1-field segments mark generated ranges with no original source.
	void test_unmapped_ranges() {
		Builder b("l");
		int src = b.addSource("s", "x");
		TS_ASSERT(b.addMapping(0, 0, src, 0, 0));
		TS_ASSERT(b.addUnmappedRange(1, 0));
		TS_ASSERT(b.addMapping(2, 0, src, 1, 0));
		Common::String json = b.build();
		Common::JSONValue *root = Common::JSON::parse(json.c_str());
		TS_ASSERT(root);
		if (!root)
			return;
		TS_ASSERT_EQUALS(root->asObject()["mappings"]->asString(), "AAAA;A;AACA");
		delete root;
	}

	// Out-of-order mappings are refused (spec requires sorted output).
	void test_out_of_order_rejected() {
		Builder b("l");
		int src = b.addSource("s", "x");
		TS_ASSERT(b.addMapping(2, 0, src, 0, 0));
		TS_ASSERT(!b.addMapping(1, 0, src, 0, 0));
		TS_ASSERT(b.addMapping(2, 0, src, 0, 5)); // same position is fine
	}

	// The data: URL must round-trip through base64 to the exact JSON.
	void test_data_url() {
		Builder b("listing.txt");
		int src = b.addSource("a.lsp", "(code)");
		b.addMapping(0, 0, src, 0, 0);
		Common::String url = b.buildDataURL();
		Common::String prefix = "data:application/json;charset=utf-8;base64,";
		TS_ASSERT_EQUALS(Common::String(url.c_str(), prefix.size()), prefix);
		Common::String b64 = Common::String(url.c_str() + prefix.size());
		Common::String decoded = Common::b64DecodeString(b64);
		TS_ASSERT_EQUALS(decoded, b.build());
	}
};
