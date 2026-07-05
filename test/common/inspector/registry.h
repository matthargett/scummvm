#include <cxxtest/TestSuite.h>

#include "common/inspector/registry.h"

/**
 * Script registry: the disassembly-listing script model (WASM-style
 * bytecode presentation, DESIGN.md), offset<->line translation and
 * statement snapping.
 */
class InspectorRegistryTestSuite : public CxxTest::TestSuite {
	typedef Inspector::ScriptListing Listing;
	typedef Inspector::ListingLine Line;

	// A tiny SCUMM-flavoured script: statement starts at lines 0, 2, 4;
	// lines 1 and 3 are continuation instructions of multi-instruction
	// statements. Offsets are byte offsets of each instruction.
	static Listing sample() {
		Listing l;
		l.url = "scummvm-dbg://scumm/room11/local-200";
		l.lines.push_back(Line(0x00, "[0000] push 13", true, 0));
		l.lines.push_back(Line(0x03, "[0003] push 5", false, 0));
		l.lines.push_back(Line(0x06, "[0006] eq", true, 0));
		l.lines.push_back(Line(0x07, "[0007] jump-if-false +12", false, 0));
		l.lines.push_back(Line(0x0C, "[000c] print \"hi\"", true, 1));
		l.originalSource = "(if (== Var[13] 5)\n  (print \"hi\"))\n";
		l.originalName = "room11-local-200.lsp";
		return l;
	}

public:
	void test_add_and_identity() {
		Inspector::ScriptRegistry reg;
		int h = reg.addScript(sample());
		TS_ASSERT(h >= 0);
		TS_ASSERT(reg.validHandle(h));
		// scriptId is a STRING (ledger #5) and round-trips.
		Common::String id = reg.scriptIdFor(h);
		TS_ASSERT(!id.empty());
		TS_ASSERT_EQUALS(reg.handleForScriptId(id), h);
		TS_ASSERT_EQUALS(reg.handleForScriptId("999"), -1);
		TS_ASSERT_EQUALS(reg.url(h), "scummvm-dbg://scumm/room11/local-200");
	}

	void test_reregistering_same_url_gets_new_script() {
		// V8 issues a fresh scriptId when the same resource is parsed
		// again; the old script stays addressable.
		Inspector::ScriptRegistry reg;
		int h1 = reg.addScript(sample());
		int h2 = reg.addScript(sample());
		TS_ASSERT(h1 != h2);
		TS_ASSERT(reg.scriptIdFor(h1) != reg.scriptIdFor(h2));
		TS_ASSERT_EQUALS(reg.scriptCount(), 2);
	}

	void test_descending_offsets_rejected() {
		Listing bad;
		bad.url = "x";
		bad.lines.push_back(Line(4, "a"));
		bad.lines.push_back(Line(2, "b"));
		Inspector::ScriptRegistry reg;
		TS_ASSERT_EQUALS(reg.addScript(bad), -1);
	}

	void test_source_text() {
		Inspector::ScriptRegistry reg;
		int h = reg.addScript(sample());
		Common::String text = reg.sourceText(h);
		TS_ASSERT_EQUALS(text,
			"[0000] push 13\n[0003] push 5\n[0006] eq\n"
			"[0007] jump-if-false +12\n[000c] print \"hi\"\n");
		TS_ASSERT_EQUALS(reg.lineCount(h), 5);
	}

	void test_offset_line_translation() {
		Inspector::ScriptRegistry reg;
		int h = reg.addScript(sample());
		// Exact hits.
		TS_ASSERT_EQUALS(reg.lineForOffset(h, 0x00), 0);
		TS_ASSERT_EQUALS(reg.lineForOffset(h, 0x06), 2);
		TS_ASSERT_EQUALS(reg.lineForOffset(h, 0x0C), 4);
		// Mid-instruction offsets resolve to the covering line.
		TS_ASSERT_EQUALS(reg.lineForOffset(h, 0x04), 1);
		TS_ASSERT_EQUALS(reg.lineForOffset(h, 0x08), 3);
		// Past the end sticks to the last line; before the start fails.
		TS_ASSERT_EQUALS(reg.lineForOffset(h, 0xFFFF), 4);

		uint32 ofs = 0;
		TS_ASSERT(reg.offsetForLine(h, 3, ofs));
		TS_ASSERT_EQUALS(ofs, 0x07u);
		TS_ASSERT(!reg.offsetForLine(h, 5, ofs));
		TS_ASSERT(!reg.offsetForLine(h, -1, ofs));
	}

	// Breakpoints snap forward to the next statement line, like V8 snaps
	// to the next break location (gdb "first instruction of a line").
	void test_statement_snapping() {
		Inspector::ScriptRegistry reg;
		int h = reg.addScript(sample());
		TS_ASSERT_EQUALS(reg.snapToStatement(h, 0), 0);
		TS_ASSERT_EQUALS(reg.snapToStatement(h, 1), 2); // line 1 is not a statement
		TS_ASSERT_EQUALS(reg.snapToStatement(h, 3), 4);
		TS_ASSERT_EQUALS(reg.snapToStatement(h, 4), 4);
		TS_ASSERT_EQUALS(reg.snapToStatement(h, 5), -1); // past the end
		// Statement covering an offset walks backwards.
		TS_ASSERT_EQUALS(reg.statementForOffset(h, 0x03), 0);
		TS_ASSERT_EQUALS(reg.statementForOffset(h, 0x07), 2);
		TS_ASSERT_EQUALS(reg.statementForOffset(h, 0x0C), 4);
	}

	void test_script_parsed_params() {
		Inspector::ScriptRegistry reg;
		int h = reg.addScript(sample());
		Common::JSONValue *params = reg.scriptParsedParams(h, 1);
		TS_ASSERT(params);
		if (!params)
			return;
		const Common::JSONObject &obj = params->asObject();
		// scriptId is a string; executionContextId an integer (ledger #4/#5).
		TS_ASSERT(obj["scriptId"]->isString());
		TS_ASSERT(obj["executionContextId"]->isIntegerNumber());
		TS_ASSERT_EQUALS(obj["executionContextId"]->asIntegerNumber(), 1);
		TS_ASSERT_EQUALS(obj["url"]->asString(), "scummvm-dbg://scumm/room11/local-200");
		TS_ASSERT_EQUALS(obj["startLine"]->asIntegerNumber(), 0);
		TS_ASSERT_EQUALS(obj["startColumn"]->asIntegerNumber(), 0);
		TS_ASSERT_EQUALS(obj["endLine"]->asIntegerNumber(), 5);
		// The original source produced a sourceMapURL data: URL.
		TS_ASSERT(obj.contains("sourceMapURL"));
		Common::String smu = obj["sourceMapURL"]->asString();
		TS_ASSERT_EQUALS(Common::String(smu.c_str(), 5), "data:");
		delete params;
	}

	void test_no_sourcemap_without_original_source() {
		Listing l;
		l.url = "scummvm-dbg://tinsel/scene-1";
		l.lines.push_back(Line(0, "op 12"));
		Inspector::ScriptRegistry reg;
		int h = reg.addScript(l);
		TS_ASSERT_EQUALS(reg.sourceMapURL(h), "");
		Common::JSONValue *params = reg.scriptParsedParams(h, 1);
		TS_ASSERT(params);
		if (!params)
			return;
		TS_ASSERT(!params->asObject().contains("sourceMapURL"));
		delete params;
	}

	void test_empty_listing_is_safe() {
		Listing l;
		l.url = "scummvm-dbg://empty";
		Inspector::ScriptRegistry reg;
		int h = reg.addScript(l);
		TS_ASSERT(h >= 0);
		TS_ASSERT_EQUALS(reg.lineForOffset(h, 0), -1);
		TS_ASSERT_EQUALS(reg.snapToStatement(h, 0), -1);
		TS_ASSERT_EQUALS(reg.sourceText(h), "");
	}
};
