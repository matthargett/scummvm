#include <cxxtest/TestSuite.h>

#include "common/inspector/breakpoints.h"

/**
 * Breakpoint store: deferred resolution (ledger #2), urlRegex matching
 * (ledger #6), duplicate rejection (ledger #7), multi-script binding
 * (ledger #8) and statement snapping.
 */
class InspectorBreakpointsTestSuite : public CxxTest::TestSuite {
	typedef Inspector::BreakpointStore Store;
	typedef Inspector::ScriptListing Listing;
	typedef Inspector::ListingLine Line;

	static Listing script(const char *url) {
		Listing l;
		l.url = url;
		l.lines.push_back(Line(0x00, "op a", true));
		l.lines.push_back(Line(0x02, "op b", false)); // not a statement
		l.lines.push_back(Line(0x04, "op c", true));
		l.lines.push_back(Line(0x06, "op d", true));
		return l;
	}

public:
	void test_set_and_hit_by_exact_url() {
		Inspector::ScriptRegistry reg;
		int h = reg.addScript(script("scummvm-dbg://agi/logic-0"));
		Store store;
		Common::String error;
		const Store::Breakpoint *bp = store.setByUrl(
			"scummvm-dbg://agi/logic-0", "", 0, 0, "", reg, error);
		TS_ASSERT(bp);
		if (!bp)
			return;
		TS_ASSERT_EQUALS(bp->locations.size(), 1u);
		TS_ASSERT_EQUALS(bp->locations[0].scriptHandle, h);
		TS_ASSERT_EQUALS(bp->locations[0].line, 0);
		TS_ASSERT_EQUALS(bp->locations[0].offset, 0x00u);

		Common::Array<const Store::Breakpoint *> hits;
		TS_ASSERT(store.hitTest(h, 0x00, hits));
		TS_ASSERT_EQUALS(hits.size(), 1u);
		TS_ASSERT_EQUALS(hits[0]->id, bp->id);
		hits.clear();
		TS_ASSERT(!store.hitTest(h, 0x04, hits));
	}

	// The line snaps forward past non-statement lines, like V8 snapping
	// to the next break location.
	void test_line_snaps_to_next_statement() {
		Inspector::ScriptRegistry reg;
		int h = reg.addScript(script("scummvm-dbg://agi/logic-0"));
		Store store;
		Common::String error;
		const Store::Breakpoint *bp = store.setByUrl(
			"scummvm-dbg://agi/logic-0", "", 1, 0, "", reg, error);
		TS_ASSERT(bp);
		if (!bp)
			return;
		TS_ASSERT_EQUALS(bp->locations[0].line, 2); // snapped over line 1
		TS_ASSERT_EQUALS(bp->locations[0].offset, 0x04u);
		Common::Array<const Store::Breakpoint *> hits;
		TS_ASSERT(store.hitTest(h, 0x04, hits));
	}

	// js-debug sends urlRegex, never url, for file breakpoints (ledger #6).
	void test_url_regex_matching() {
		Inspector::ScriptRegistry reg;
		int h = reg.addScript(script("scummvm-dbg://sci/script-994"));
		Store store;
		Common::String error;
		const Store::Breakpoint *bp = store.setByUrl(
			"", "[sS][cC][uU][mM][mM][vV][mM]-[dD][bB][gG]:\\/\\/[sS][cC][iI]\\/"
			    "[sS][cC][rR][iI][pP][tT]-994($|\\?)", 0, 0, "", reg, error);
		TS_ASSERT(bp);
		if (!bp)
			return;
		TS_ASSERT_EQUALS(bp->locations.size(), 1u);
		TS_ASSERT_EQUALS(bp->locations[0].scriptHandle, h);
	}

	void test_invalid_regex_is_error() {
		Inspector::ScriptRegistry reg;
		Store store;
		Common::String error;
		TS_ASSERT(!store.setByUrl("", "[unclosed", 0, 0, "", reg, error));
		TS_ASSERT(!error.empty());
	}

	void test_both_or_neither_spec_is_error() {
		Inspector::ScriptRegistry reg;
		Store store;
		Common::String error;
		TS_ASSERT(!store.setByUrl("", "", 0, 0, "", reg, error));
		TS_ASSERT(!store.setByUrl("a", "b", 0, 0, "", reg, error));
	}

	// Set before script parsed: empty locations, NOT an error; binding
	// happens when the script registers (ledger #2 — js-debug#568).
	void test_deferred_resolution() {
		Inspector::ScriptRegistry reg;
		Store store;
		Common::String error;
		const Store::Breakpoint *bp = store.setByUrl(
			"scummvm-dbg://kyra/_STARTUP.EMC", "", 0, 0, "", reg, error);
		TS_ASSERT(bp);
		if (!bp)
			return;
		TS_ASSERT_EQUALS(bp->locations.size(), 0u); // unresolved, kept
		TS_ASSERT(!store.anyResolved());

		int h = reg.addScript(script("scummvm-dbg://kyra/_STARTUP.EMC"));
		Common::Array<Store::Resolution> resolved;
		store.bindScript(h, reg, resolved);
		// The caller emits Debugger.breakpointResolved from these.
		TS_ASSERT_EQUALS(resolved.size(), 1u);
		TS_ASSERT_EQUALS(resolved[0].breakpointId, bp->id);
		TS_ASSERT_EQUALS(resolved[0].location.scriptHandle, h);
		TS_ASSERT(store.anyResolved());

		Common::Array<const Store::Breakpoint *> hits;
		TS_ASSERT(store.hitTest(h, 0x00, hits));
	}

	// One regex breakpoint can bind into many scripts; every location is
	// kept and a hit in any of them reports the id (ledger #8).
	void test_one_breakpoint_multiple_scripts() {
		Inspector::ScriptRegistry reg;
		int h1 = reg.addScript(script("scummvm-dbg://scumm/room11/local-200"));
		int h2 = reg.addScript(script("scummvm-dbg://scumm/room12/local-200"));
		Store store;
		Common::String error;
		const Store::Breakpoint *bp = store.setByUrl(
			"", "room\\d+\\/local-200", 0, 0, "", reg, error);
		TS_ASSERT(bp);
		if (!bp)
			return;
		TS_ASSERT_EQUALS(bp->locations.size(), 2u);
		Common::Array<const Store::Breakpoint *> hits;
		TS_ASSERT(store.hitTest(h1, 0, hits));
		hits.clear();
		TS_ASSERT(store.hitTest(h2, 0, hits));
	}

	// Re-registering the same URL (script reloaded) binds again into the
	// new script instance.
	void test_rebinds_on_script_reload() {
		Inspector::ScriptRegistry reg;
		Store store;
		Common::String error;
		int h1 = reg.addScript(script("scummvm-dbg://director/movie1/cast-5"));
		const Store::Breakpoint *bp = store.setByUrl(
			"scummvm-dbg://director/movie1/cast-5", "", 0, 0, "", reg, error);
		TS_ASSERT(bp);
		int h2 = reg.addScript(script("scummvm-dbg://director/movie1/cast-5"));
		Common::Array<Store::Resolution> resolved;
		store.bindScript(h2, reg, resolved);
		TS_ASSERT_EQUALS(resolved.size(), 1u);
		Common::Array<const Store::Breakpoint *> hits;
		TS_ASSERT(store.hitTest(h1, 0, hits));
		hits.clear();
		TS_ASSERT(store.hitTest(h2, 0, hits));
	}

	// V8: "Breakpoint at specified location already exists." (ledger #7)
	void test_duplicate_rejected() {
		Inspector::ScriptRegistry reg;
		reg.addScript(script("scummvm-dbg://ags/globalscript"));
		Store store;
		Common::String error;
		TS_ASSERT(store.setByUrl("scummvm-dbg://ags/globalscript", "", 0, 0, "", reg, error));
		TS_ASSERT(!store.setByUrl("scummvm-dbg://ags/globalscript", "", 0, 0, "", reg, error));
		TS_ASSERT_EQUALS(error, "Breakpoint at specified location already exists.");
		// Same url, different line: fine.
		TS_ASSERT(store.setByUrl("scummvm-dbg://ags/globalscript", "", 2, 0, "", reg, error));
	}

	void test_remove() {
		Inspector::ScriptRegistry reg;
		int h = reg.addScript(script("scummvm-dbg://wintermute/game.script"));
		Store store;
		Common::String error;
		const Store::Breakpoint *bp1 = store.setByUrl(
			"scummvm-dbg://wintermute/game.script", "", 0, 0, "", reg, error);
		const Store::Breakpoint *bp2 = store.setByUrl(
			"scummvm-dbg://wintermute/game.script", "", 2, 0, "", reg, error);
		TS_ASSERT(bp1 && bp2);
		if (!bp1 || !bp2)
			return;
		Common::String id1 = bp1->id;
		TS_ASSERT(store.remove(id1));
		TS_ASSERT(!store.remove(id1)); // second remove fails
		Common::Array<const Store::Breakpoint *> hits;
		TS_ASSERT(!store.hitTest(h, 0x00, hits));
		TS_ASSERT(store.hitTest(h, 0x04, hits)); // bp2 still live
	}

	// Debugger.setBreakpointsActive false mutes everything (no removal).
	void test_set_breakpoints_active() {
		Inspector::ScriptRegistry reg;
		int h = reg.addScript(script("scummvm-dbg://tinsel/scene-3"));
		Store store;
		Common::String error;
		TS_ASSERT(store.setByUrl("scummvm-dbg://tinsel/scene-3", "", 0, 0, "", reg, error));
		Common::Array<const Store::Breakpoint *> hits;
		TS_ASSERT(store.hitTest(h, 0, hits));
		hits.clear();
		store.setActive(false);
		TS_ASSERT(!store.hitTest(h, 0, hits));
		store.setActive(true);
		TS_ASSERT(store.hitTest(h, 0, hits));
	}

	// A breakpoint past the last statement of a matching script stays
	// unresolved rather than erroring or binding nonsense.
	void test_line_past_end_stays_unresolved() {
		Inspector::ScriptRegistry reg;
		reg.addScript(script("scummvm-dbg://sludge/fn-77"));
		Store store;
		Common::String error;
		const Store::Breakpoint *bp = store.setByUrl(
			"scummvm-dbg://sludge/fn-77", "", 40, 0, "", reg, error);
		TS_ASSERT(bp);
		if (!bp)
			return;
		TS_ASSERT_EQUALS(bp->locations.size(), 0u);
	}

	void test_condition_is_stored() {
		Inspector::ScriptRegistry reg;
		reg.addScript(script("scummvm-dbg://agi/logic-90"));
		Store store;
		Common::String error;
		const Store::Breakpoint *bp = store.setByUrl(
			"scummvm-dbg://agi/logic-90", "", 0, 0, "v13 == 5", reg, error);
		TS_ASSERT(bp);
		if (!bp)
			return;
		TS_ASSERT_EQUALS(bp->condition, "v13 == 5");
	}
};
