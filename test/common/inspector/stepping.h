#include <cxxtest/TestSuite.h>

#include "common/inspector/stepping.h"

/**
 * Stepping semantics (ledger #9-12): frame identity instead of depth
 * counters (mobdebug's documented failure), step-over degrading to
 * step-out at returns (Hermes), loop back-edges stopping, and per-thread
 * step scoping.
 */
class InspectorSteppingTestSuite : public CxxTest::TestSuite {
	typedef Inspector::StepController Ctl;
	typedef Inspector::StepPoint P;
	typedef Inspector::FrameTracker Tracker;

	static Common::Array<uint64> chain1(uint64 a) {
		Common::Array<uint64> c;
		c.push_back(a);
		return c;
	}
	static Common::Array<uint64> chain2(uint64 a, uint64 b) {
		Common::Array<uint64> c;
		c.push_back(a);
		c.push_back(b);
		return c;
	}

public:
	// --- FrameTracker ---

	void test_tracker_depth_to_tokens() {
		Tracker t;
		uint64 main = t.feed(1);
		TS_ASSERT(main != 0);
		TS_ASSERT_EQUALS(t.feed(1), main);   // same depth, same activation
		uint64 callee = t.feed(2);           // call
		TS_ASSERT(callee != main);
		TS_ASSERT_EQUALS(t.feed(2), callee);
		TS_ASSERT_EQUALS(t.feed(1), main);   // return restores the caller token
	}

	// A new activation at a previously-seen depth is a NEW frame — this
	// is exactly what a depth counter cannot express (ledger #9).
	void test_tracker_recursion_gets_fresh_tokens() {
		Tracker t;
		t.feed(1);
		uint64 firstCall = t.feed(2);
		t.feed(1);                 // returned
		uint64 secondCall = t.feed(2); // called again (same depth as before)
		TS_ASSERT(firstCall != secondCall);
	}

	void test_tracker_caller_chain() {
		Tracker t;
		uint64 a = t.feed(1);
		uint64 b = t.feed(2);
		t.feed(3);
		Common::Array<uint64> chain;
		t.callerChain(chain);
		TS_ASSERT_EQUALS(chain.size(), 2u);
		TS_ASSERT_EQUALS(chain[0], a);
		TS_ASSERT_EQUALS(chain[1], b);
	}

	void test_tracker_explicit_call_return() {
		Tracker t;
		uint64 main = t.feed(1);
		uint64 callee = t.onCall();
		TS_ASSERT(callee != main);
		TS_ASSERT_EQUALS(t.depth(), 2u);
		TS_ASSERT_EQUALS(t.onReturn(), main);
	}

	// --- StepController ---

	void test_step_into_pauses_at_next_statement() {
		Ctl c;
		c.arm(Inspector::kStepInto, P(1, 100, 0, 3, 0x30), Common::Array<uint64>());
		TS_ASSERT(c.shouldPause(P(1, 100, 0, 4, 0x38))); // next statement
	}

	void test_step_into_enters_callee() {
		Ctl c;
		c.arm(Inspector::kStepInto, P(1, 100, 0, 3, 0x30), Common::Array<uint64>());
		TS_ASSERT(c.shouldPause(P(1, 200, 5, 0, 0x00))); // first stmt of callee
	}

	// Step-over: statements in a called (deeper) activation don't pause;
	// the next statement in the armed activation does.
	void test_step_over_skips_callee() {
		Ctl c;
		c.arm(Inspector::kStepOver, P(1, 100, 0, 3, 0x30), Common::Array<uint64>());
		TS_ASSERT(!c.shouldPause(P(1, 200, 5, 0, 0x00))); // inside callee
		TS_ASSERT(!c.shouldPause(P(1, 200, 5, 1, 0x04)));
		TS_ASSERT(c.shouldPause(P(1, 100, 0, 4, 0x38)));  // back in armed frame
	}

	// Recursive call: same script, same statement lines, deeper NEW
	// activation — must not stop there (ledger #9; the depth-counter bug).
	void test_step_over_skips_recursive_activation() {
		Tracker t;
		t.feed(1);
		uint64 armedFrame = t.feed(2); // recursive fn, first activation
		Common::Array<uint64> chain;
		t.callerChain(chain);

		Ctl c;
		c.arm(Inspector::kStepOver, P(1, armedFrame, 7, 2, 0x10), chain);

		// The armed statement calls the function recursively: depth 3,
		// then that returns and a NEW depth-2 activation is entered by a
		// sibling call from depth 1... all of these are foreign frames.
		uint64 recursive = t.feed(3);
		TS_ASSERT(!c.shouldPause(P(1, recursive, 7, 0, 0x00)));
		TS_ASSERT(!c.shouldPause(P(1, recursive, 7, 2, 0x10))); // same stmt, deeper!
		TS_ASSERT_EQUALS(t.feed(2), armedFrame); // return to armed activation
		TS_ASSERT(c.shouldPause(P(1, armedFrame, 7, 3, 0x18)));
	}

	// Step-over at a return: no more statements in the armed frame; the
	// step completes at the first statement of a captured caller
	// (degrades to step-out; Hermes does the same — ledger #10).
	void test_step_over_at_return_degrades_to_step_out() {
		Tracker t;
		uint64 mainTok = t.feed(1);
		uint64 armed = t.feed(2);
		Common::Array<uint64> chain;
		t.callerChain(chain);

		Ctl c;
		c.arm(Inspector::kStepOver, P(1, armed, 3, 9, 0x90), chain); // last stmt
		t.feed(1); // frame returned
		TS_ASSERT(c.shouldPause(P(1, mainTok, 0, 12, 0xC0)));
	}

	// Loop: the SAME statement re-entered through a back edge must stop
	// every iteration (Hermes sameStatementDifferentInstruction rule,
	// gdb's per-iteration stop — ledger #11).
	void test_step_over_stops_on_loop_backedge() {
		Ctl c;
		c.arm(Inspector::kStepOver, P(1, 100, 0, 5, 0x50), Common::Array<uint64>());
		TS_ASSERT(c.shouldPause(P(1, 100, 0, 5, 0x50))); // same stmt, same frame
	}

	// Steps are scoped to the initiating thread: another script thread
	// reaching statements never completes this thread's step (mobdebug's
	// coroutine guard — ledger #12).
	void test_step_scoped_to_thread() {
		Ctl c;
		c.arm(Inspector::kStepOver, P(1, 100, 0, 3, 0x30), Common::Array<uint64>());
		TS_ASSERT(!c.shouldPause(P(2, 100, 0, 4, 0x38))); // other thread
		TS_ASSERT(!c.shouldPause(P(2, 555, 9, 0, 0x00)));
		TS_ASSERT(c.shouldPause(P(1, 100, 0, 4, 0x38)));
	}

	void test_step_out() {
		Tracker t;
		uint64 mainTok = t.feed(1);
		uint64 midTok = t.feed(2);
		uint64 armed = t.feed(3);
		Common::Array<uint64> chain;
		t.callerChain(chain);
		TS_ASSERT_EQUALS(chain.size(), 2u);

		Ctl c;
		c.arm(Inspector::kStepOut, P(1, armed, 4, 2, 0x20), chain);
		// Statements in the armed frame do not pause.
		TS_ASSERT(!c.shouldPause(P(1, armed, 4, 3, 0x28)));
		// A callee doesn't either.
		TS_ASSERT(!c.shouldPause(P(1, 999, 8, 0, 0x00)));
		// Any captured caller does (normally the immediate one).
		TS_ASSERT(c.shouldPause(P(1, midTok, 2, 7, 0x70)));
		TS_ASSERT(c.shouldPause(P(1, mainTok, 0, 1, 0x08)));
	}

	void test_clear_disarms() {
		Ctl c;
		c.arm(Inspector::kStepInto, P(1, 100, 0, 3, 0x30), Common::Array<uint64>());
		TS_ASSERT(c.armed());
		c.clear();
		TS_ASSERT(!c.armed());
		TS_ASSERT(!c.shouldPause(P(1, 100, 0, 4, 0x38)));
	}
};
