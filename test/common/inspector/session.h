#include <cxxtest/TestSuite.h>

#include "common/inspector/session.h"
#include "common/formats/json.h"
#include "common/hash-str.h"
#include "common/hashmap.h"

/**
 * Session integration: the full attach -> breakpoint -> pause -> inspect
 * -> step -> resume flow against a mock engine agent, plus the client-
 * compatibility corner cases (ledger #1, #3, #4, #5, #13, #14, #26).
 */
namespace {

// A mock engine: one thread, two-frame call stack, a couple of variables.
class MockAgent : public Inspector::Agent {
public:
	int script;          ///< handle of the "current" script
	uint32 offset;       ///< current offset reported in frames
	int evalCount;
	Common::HashMap<Common::String, int64> vars;

	MockAgent() : script(-1), offset(0), evalCount(0) {
		vars["V13"] = 5;
		vars["flag7"] = 0;
	}

	Common::String engineId() const override { return "mock"; }
	Common::String targetTitle() const override { return "Mock Game"; }

	void buildCallFrames(uint32 threadId, Common::Array<Inspector::CallFrameInfo> &frames) override {
		Inspector::CallFrameInfo top;
		top.functionName = "logic-0";
		top.scriptHandle = script;
		top.offset = offset;
		Inspector::ScopeInfo local;
		local.type = "local";
		local.name = "Locals";
		top.scopes.push_back(local);
		Inspector::ScopeInfo global;
		global.type = "global";
		global.name = "Globals";
		top.scopes.push_back(global);
		frames.push_back(top);

		Inspector::CallFrameInfo caller;
		caller.functionName = "main";
		caller.scriptHandle = script;
		caller.offset = 0;
		frames.push_back(caller);
	}

	void buildScopeObject(uint32 threadId, int frameIndex, int scopeIndex,
	                      Inspector::RemoteObjectTable &table, int objRef) override {
		if (scopeIndex == 0) {
			table.addProperty(objRef, "V13", Inspector::DebugValue::fromInt(vars["V13"]));
		} else {
			table.addProperty(objRef, "flag7", Inspector::DebugValue::fromInt(vars["flag7"]));
			table.addProperty(objRef, "gameName", Inspector::DebugValue::fromString("mock quest"));
		}
	}

	bool evaluate(uint32 threadId, int frameIndex, const Common::String &expression,
	              Inspector::RemoteObjectTable &table, Inspector::DebugValue &result) override {
		evalCount++;
		if (vars.contains(expression)) {
			result = Inspector::DebugValue::fromInt(vars[expression]);
			return true;
		}
		return false;
	}
};

// Pump that feeds scripted messages one per tick while the VM is paused.
class ScriptedPump : public Inspector::PausePump {
public:
	Inspector::Session *session;
	Common::Array<Common::String> script;
	uint32 next;
	int idleTicks; ///< ticks to survive with no message before giving up

	ScriptedPump() : session(nullptr), next(0), idleTicks(0) {}

	void queue(const Common::String &msg) { script.push_back(msg); }

	bool pumpWhilePaused() override {
		if (next < script.size()) {
			session->onMessage(script[next++]);
			return true;
		}
		if (idleTicks-- > 0)
			return true;
		return false; // ran dry: abort pause so tests can't hang
	}
};

} // end of anonymous namespace

class InspectorSessionTestSuite : public CxxTest::TestSuite {
	typedef Inspector::ScriptListing Listing;
	typedef Inspector::ListingLine Line;

	static Listing logicScript() {
		Listing l;
		l.url = "scummvm-dbg://mock/logic-0";
		l.lines.push_back(Line(0x00, "assign v13, 5", true, 0));
		l.lines.push_back(Line(0x03, "push v13", false, 1));
		l.lines.push_back(Line(0x05, "call fn-1", true, 1));
		l.lines.push_back(Line(0x08, "print", true, 2));
		l.lines.push_back(Line(0x0A, "halt", true, 3));
		l.originalSource = "(assign V13 5)\n(call fn-1 V13)\n(print)\n(halt)\n";
		return l;
	}

	static Listing fnScript() {
		Listing l;
		l.url = "scummvm-dbg://mock/fn-1";
		l.lines.push_back(Line(0x00, "noop", true));
		l.lines.push_back(Line(0x02, "ret", true));
		return l;
	}

	// Drain all queued outgoing messages into parsed JSON values the
	// caller must delete.
	static void drain(Inspector::Session &s, Common::Array<Common::JSONValue *> &out) {
		Common::String msg;
		while (s.nextOutgoing(msg)) {
			Common::JSONValue *v = Common::JSON::parse(msg.c_str());
			TS_ASSERT(v);
			if (v)
				out.push_back(v);
		}
	}

	static void freeAll(Common::Array<Common::JSONValue *> &msgs) {
		for (uint32 i = 0; i < msgs.size(); i++)
			delete msgs[i];
		msgs.clear();
	}

	// Find the first event with the given method; nullptr if absent.
	static const Common::JSONValue *findEvent(const Common::Array<Common::JSONValue *> &msgs,
	                                          const char *method) {
		for (uint32 i = 0; i < msgs.size(); i++) {
			const Common::JSONObject &o = msgs[i]->asObject();
			if (o.contains("method") && o["method"]->asString() == method)
				return msgs[i];
		}
		return nullptr;
	}

	static const Common::JSONValue *findResponse(const Common::Array<Common::JSONValue *> &msgs,
	                                             int64 id) {
		for (uint32 i = 0; i < msgs.size(); i++) {
			const Common::JSONObject &o = msgs[i]->asObject();
			if (o.contains("id") && o["id"]->isIntegerNumber() &&
			    o["id"]->asIntegerNumber() == id)
				return msgs[i];
		}
		return nullptr;
	}

public:
	// Attach sequence: Runtime.enable / Debugger.enable responses come
	// before their events; context id is 1 and scriptParsed agrees
	// (ledger #4); scriptId is a string (ledger #5); commands answered
	// in order (ledger #1).
	void test_attach_flow() {
		MockAgent agent;
		Inspector::Session s(&agent);
		agent.script = s.registerScript(logicScript());
		TS_ASSERT(agent.script >= 0);

		s.onMessage("{\"id\":1,\"method\":\"Runtime.enable\"}");
		s.onMessage("{\"id\":2,\"method\":\"Profiler.enable\"}");
		s.onMessage("{\"id\":3,\"method\":\"Debugger.enable\"}");
		s.onMessage("{\"id\":4,\"method\":\"Debugger.setPauseOnExceptions\",\"params\":{\"state\":\"none\"}}");
		s.onMessage("{\"id\":5,\"method\":\"Runtime.runIfWaitingForDebugger\"}");

		Common::Array<Common::JSONValue *> msgs;
		drain(s, msgs);

		// All five commands answered, in order of arrival.
		int64 lastId = 0;
		uint32 responses = 0;
		for (uint32 i = 0; i < msgs.size(); i++) {
			const Common::JSONObject &o = msgs[i]->asObject();
			if (o.contains("id")) {
				responses++;
				TS_ASSERT(o["id"]->asIntegerNumber() > lastId);
				lastId = o["id"]->asIntegerNumber();
				TS_ASSERT(o.contains("result"));
			}
		}
		TS_ASSERT_EQUALS(responses, 5u);

		const Common::JSONValue *ctx = findEvent(msgs, "Runtime.executionContextCreated");
		TS_ASSERT(ctx);
		if (ctx) {
			const Common::JSONObject &c =
				ctx->asObject()["params"]->asObject()["context"]->asObject();
			TS_ASSERT_EQUALS(c["id"]->asIntegerNumber(), 1);
			TS_ASSERT(c["auxData"]->asObject()["isDefault"]->asBool());
		}

		const Common::JSONValue *parsed = findEvent(msgs, "Debugger.scriptParsed");
		TS_ASSERT(parsed);
		if (parsed) {
			const Common::JSONObject &p = parsed->asObject()["params"]->asObject();
			TS_ASSERT(p["scriptId"]->isString());
			TS_ASSERT_EQUALS(p["executionContextId"]->asIntegerNumber(), 1);
			TS_ASSERT_EQUALS(p["url"]->asString(), "scummvm-dbg://mock/logic-0");
		}

		// The Debugger.enable response precedes the scriptParsed replay.
		uint32 enableIdx = 0, parsedIdx = 0;
		for (uint32 i = 0; i < msgs.size(); i++) {
			const Common::JSONObject &o = msgs[i]->asObject();
			if (o.contains("id") && o["id"]->asIntegerNumber() == 3)
				enableIdx = i;
			if (o.contains("method") && o["method"]->asString() == "Debugger.scriptParsed")
				parsedIdx = i;
		}
		TS_ASSERT(enableIdx < parsedIdx);

		freeAll(msgs);
	}

	// Unknown methods must be answered with -32601, never swallowed
	// (ledger #3); malformed JSON answered with -32700.
	void test_unknown_method_and_parse_errors() {
		MockAgent agent;
		Inspector::Session s(&agent);
		s.onMessage("{\"id\":7,\"method\":\"Network.enable\"}");
		s.onMessage("{\"id\":8,\"method\":");
		Common::Array<Common::JSONValue *> msgs;
		drain(s, msgs);
		TS_ASSERT_EQUALS(msgs.size(), 2u);
		if (msgs.size() < 2)
			return;
		const Common::JSONObject &err = msgs[0]->asObject();
		TS_ASSERT_EQUALS(err["id"]->asIntegerNumber(), 7);
		TS_ASSERT_EQUALS(err["error"]->asObject()["code"]->asIntegerNumber(), -32601);
		const Common::JSONObject &parseErr = msgs[1]->asObject();
		TS_ASSERT(parseErr["id"]->isNull());
		TS_ASSERT_EQUALS(parseErr["error"]->asObject()["code"]->asIntegerNumber(), -32700);
		freeAll(msgs);
	}

	// Deferred breakpoint: set before the script exists -> empty
	// locations; registering the script emits breakpointResolved and the
	// breakpoint then hits (ledger #2).
	void test_deferred_breakpoint_resolution_and_hit() {
		MockAgent agent;
		Inspector::Session s(&agent);
		s.onMessage("{\"id\":1,\"method\":\"Debugger.enable\"}");
		s.onMessage("{\"id\":2,\"method\":\"Debugger.setBreakpointByUrl\",\"params\":"
		            "{\"lineNumber\":3,\"urlRegex\":\"[lL][oO][gG][iI][cC]-0($|\\\\?)\"}}");
		Common::Array<Common::JSONValue *> msgs;
		drain(s, msgs);
		const Common::JSONValue *resp = findResponse(msgs, 2);
		TS_ASSERT(resp);
		Common::String bpId;
		if (resp) {
			const Common::JSONObject &r = resp->asObject()["result"]->asObject();
			bpId = r["breakpointId"]->asString();
			TS_ASSERT_EQUALS(r["locations"]->asArray().size(), 0u); // not an error!
		}
		freeAll(msgs);

		agent.script = s.registerScript(logicScript());
		drain(s, msgs);
		const Common::JSONValue *resolved = findEvent(msgs, "Debugger.breakpointResolved");
		TS_ASSERT(resolved);
		if (resolved) {
			const Common::JSONObject &p = resolved->asObject()["params"]->asObject();
			TS_ASSERT_EQUALS(p["breakpointId"]->asString(), bpId);
			TS_ASSERT_EQUALS(p["location"]->asObject()["lineNumber"]->asIntegerNumber(), 3);
		}
		freeAll(msgs);

		// Drive the VM to the breakpoint offset; a scripted pump resumes.
		ScriptedPump pump;
		pump.session = &s;
		pump.queue("{\"id\":10,\"method\":\"Debugger.resume\"}");
		s.setPausePump(&pump);

		TS_ASSERT(s.armed());
		agent.offset = 0x00;
		s.instructionHook(1, agent.script, 0x00, 1); // no bp here
		TS_ASSERT(!s.paused());
		agent.offset = 0x08; // line 3
		s.instructionHook(1, agent.script, 0x08, 1);
		// The pump resumed us synchronously inside the hook.
		TS_ASSERT(!s.paused());

		drain(s, msgs);
		const Common::JSONValue *paused = findEvent(msgs, "Debugger.paused");
		TS_ASSERT(paused);
		if (paused) {
			const Common::JSONObject &p = paused->asObject()["params"]->asObject();
			TS_ASSERT_EQUALS(p["reason"]->asString(), "other");
			TS_ASSERT(p.contains("hitBreakpoints"));
			TS_ASSERT_EQUALS(p["hitBreakpoints"]->asArray().size(), 1u);
			TS_ASSERT_EQUALS(p["hitBreakpoints"]->asArray()[0]->asString(), bpId);
			const Common::JSONArray &frames = p["callFrames"]->asArray();
			TS_ASSERT_EQUALS(frames.size(), 2u);
			const Common::JSONObject &top = frames[0]->asObject();
			TS_ASSERT_EQUALS(top["functionName"]->asString(), "logic-0");
			TS_ASSERT_EQUALS(top["location"]->asObject()["lineNumber"]->asIntegerNumber(), 3);
			TS_ASSERT(top["location"]->asObject()["scriptId"]->isString());
		}
		// Response to resume precedes the resumed event (ledger #26).
		const Common::JSONValue *resumed = findEvent(msgs, "Debugger.resumed");
		TS_ASSERT(resumed);
		uint32 respIdx = 0, resumedIdx = 0;
		for (uint32 i = 0; i < msgs.size(); i++) {
			const Common::JSONObject &o = msgs[i]->asObject();
			if (o.contains("id") && o["id"]->isIntegerNumber() &&
			    o["id"]->asIntegerNumber() == 10)
				respIdx = i;
			if (o.contains("method") && o["method"]->asString() == "Debugger.resumed")
				resumedIdx = i;
		}
		TS_ASSERT(respIdx < resumedIdx);
		freeAll(msgs);
	}

	// While paused: getProperties on a scope objectId materializes the
	// agent's variables; evaluateOnCallFrame resolves them; stale ids
	// die after resume (evaluate-while-paused re-entrancy, ledger #13).
	void test_inspection_while_paused() {
		MockAgent agent;
		Inspector::Session s(&agent);
		s.onMessage("{\"id\":1,\"method\":\"Debugger.enable\"}");
		agent.script = s.registerScript(logicScript());
		s.onMessage("{\"id\":2,\"method\":\"Debugger.setBreakpointByUrl\",\"params\":"
		            "{\"lineNumber\":0,\"url\":\"scummvm-dbg://mock/logic-0\"}}");
		Common::Array<Common::JSONValue *> msgs;
		drain(s, msgs);
		freeAll(msgs);

		ScriptedPump pump;
		pump.session = &s;
		s.setPausePump(&pump);
		// While paused: expand scope, evaluate, then resume. The frame
		// and scope ids depend on the object generation, which starts at
		// 1 and is bumped by each reset; first pause -> generation 1.
		pump.queue("{\"id\":20,\"method\":\"Runtime.getProperties\",\"params\":{\"objectId\":\"scope:1:0:0\"}}");
		pump.queue("{\"id\":21,\"method\":\"Debugger.evaluateOnCallFrame\",\"params\":"
		           "{\"callFrameId\":\"frame:1:0\",\"expression\":\"V13\"}}");
		pump.queue("{\"id\":22,\"method\":\"Debugger.evaluateOnCallFrame\",\"params\":"
		           "{\"callFrameId\":\"frame:1:0\",\"expression\":\"no_such_var\"}}");
		pump.queue("{\"id\":23,\"method\":\"Debugger.resume\"}");

		agent.offset = 0x00;
		s.instructionHook(1, agent.script, 0x00, 1);
		TS_ASSERT(!s.paused());

		drain(s, msgs);
		const Common::JSONValue *props = findResponse(msgs, 20);
		TS_ASSERT(props);
		if (props) {
			const Common::JSONArray &arr =
				props->asObject()["result"]->asObject()["result"]->asArray();
			TS_ASSERT_EQUALS(arr.size(), 1u);
			TS_ASSERT_EQUALS(arr[0]->asObject()["name"]->asString(), "V13");
			TS_ASSERT_EQUALS(arr[0]->asObject()["value"]->asObject()["value"]->asIntegerNumber(), 5);
		}
		const Common::JSONValue *evalResp = findResponse(msgs, 21);
		TS_ASSERT(evalResp);
		if (evalResp) {
			const Common::JSONObject &r =
				evalResp->asObject()["result"]->asObject()["result"]->asObject();
			TS_ASSERT_EQUALS(r["type"]->asString(), "number");
			TS_ASSERT_EQUALS(r["value"]->asIntegerNumber(), 5);
		}
		// Unevaluable expression answers undefined, not an error (ledger #14).
		const Common::JSONValue *badEval = findResponse(msgs, 22);
		TS_ASSERT(badEval);
		if (badEval) {
			TS_ASSERT(badEval->asObject().contains("result"));
			TS_ASSERT_EQUALS(badEval->asObject()["result"]->asObject()["result"]
			                 ->asObject()["type"]->asString(), "undefined");
		}
		freeAll(msgs);

		// After resume the scope objectId is stale.
		s.onMessage("{\"id\":30,\"method\":\"Runtime.getProperties\",\"params\":{\"objectId\":\"scope:1:0:0\"}}");
		drain(s, msgs);
		const Common::JSONValue *stale = findResponse(msgs, 30);
		TS_ASSERT(stale);
		if (stale)
			TS_ASSERT(stale->asObject().contains("error"));
		freeAll(msgs);
	}

	// Step-over: the callee's statements execute without pausing; the
	// next statement in the armed frame pauses (ledger #9/#26 ordering).
	void test_step_over_call() {
		MockAgent agent;
		Inspector::Session s(&agent);
		s.onMessage("{\"id\":1,\"method\":\"Debugger.enable\"}");
		agent.script = s.registerScript(logicScript());
		int fn = s.registerScript(fnScript());
		s.onMessage("{\"id\":2,\"method\":\"Debugger.setBreakpointByUrl\",\"params\":"
		            "{\"lineNumber\":2,\"url\":\"scummvm-dbg://mock/logic-0\"}}");
		Common::Array<Common::JSONValue *> msgs;
		drain(s, msgs);
		freeAll(msgs);

		ScriptedPump pump;
		pump.session = &s;
		s.setPausePump(&pump);
		pump.queue("{\"id\":10,\"method\":\"Debugger.stepOver\"}");

		// Hit the breakpoint at the call site (line 2, offset 0x05).
		agent.offset = 0x05;
		s.instructionHook(1, agent.script, 0x05, 1);
		TS_ASSERT(!s.paused()); // stepOver armed and resumed
		TS_ASSERT(s.armed());

		// The callee runs at depth 2: none of its statements pause.
		s.instructionHook(1, fn, 0x00, 2);
		TS_ASSERT(!s.paused());
		s.instructionHook(1, fn, 0x02, 2);
		TS_ASSERT(!s.paused());

		// Another thread's statements do not complete the step (ledger #12).
		s.instructionHook(2, agent.script, 0x08, 1);
		TS_ASSERT(!s.paused());

		// Back in the armed frame: next statement pauses.
		pump.queue("{\"id\":11,\"method\":\"Debugger.resume\"}");
		agent.offset = 0x08;
		s.instructionHook(1, agent.script, 0x08, 1);

		drain(s, msgs);
		// Exactly two paused events in this test: breakpoint, then step.
		uint32 pausedCount = 0;
		for (uint32 i = 0; i < msgs.size(); i++) {
			const Common::JSONObject &o = msgs[i]->asObject();
			if (o.contains("method") && o["method"]->asString() == "Debugger.paused")
				pausedCount++;
		}
		TS_ASSERT_EQUALS(pausedCount, 2u);
		freeAll(msgs);
	}

	// Wait-for-debugger startup: the first instruction blocks until
	// Runtime.runIfWaitingForDebugger, then pauses for breakpoint
	// planting (deno#9886 — fast programs finish before attach).
	void test_wait_for_debugger() {
		MockAgent agent;
		Inspector::Session s(&agent);
		agent.script = s.registerScript(logicScript());
		s.setWaitForDebugger(true);
		TS_ASSERT(s.armed());

		ScriptedPump pump;
		pump.session = &s;
		s.setPausePump(&pump);
		pump.queue("{\"id\":1,\"method\":\"Runtime.enable\"}");
		pump.queue("{\"id\":2,\"method\":\"Debugger.enable\"}");
		pump.queue("{\"id\":3,\"method\":\"Runtime.runIfWaitingForDebugger\"}");
		// After release, the session pauses at the first statement; the
		// pump then resumes it.
		pump.queue("{\"id\":4,\"method\":\"Debugger.resume\"}");

		agent.offset = 0x00;
		s.instructionHook(1, agent.script, 0x00, 1);
		TS_ASSERT(!s.paused());

		Common::Array<Common::JSONValue *> msgs;
		drain(s, msgs);
		const Common::JSONValue *paused = findEvent(msgs, "Debugger.paused");
		TS_ASSERT(paused); // stopped on the first statement post-release
		freeAll(msgs);
	}

	// js-debug's process probe must get its string answer (ledger #14).
	void test_process_probe() {
		MockAgent agent;
		Inspector::Session s(&agent);
		s.onMessage("{\"id\":1,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":"
		            "\"typeof process === 'undefined' || process.pid === undefined ? 'process not defined' : process.pid\"}}");
		Common::Array<Common::JSONValue *> msgs;
		drain(s, msgs);
		const Common::JSONValue *resp = findResponse(msgs, 1);
		TS_ASSERT(resp);
		if (resp) {
			const Common::JSONObject &r =
				resp->asObject()["result"]->asObject()["result"]->asObject();
			TS_ASSERT_EQUALS(r["type"]->asString(), "string");
			TS_ASSERT_EQUALS(r["value"]->asString(), "process not defined");
		}
		// The mock agent was never asked — the probe is intercepted.
		TS_ASSERT_EQUALS(agent.evalCount, 0);
		freeAll(msgs);
	}

	void test_skip_all_pauses() {
		MockAgent agent;
		Inspector::Session s(&agent);
		s.onMessage("{\"id\":1,\"method\":\"Debugger.enable\"}");
		agent.script = s.registerScript(logicScript());
		s.onMessage("{\"id\":2,\"method\":\"Debugger.setBreakpointByUrl\",\"params\":"
		            "{\"lineNumber\":0,\"url\":\"scummvm-dbg://mock/logic-0\"}}");
		s.onMessage("{\"id\":3,\"method\":\"Debugger.setSkipAllPauses\",\"params\":{\"skip\":true}}");
		Common::Array<Common::JSONValue *> msgs;
		drain(s, msgs);
		freeAll(msgs);

		s.instructionHook(1, agent.script, 0x00, 1);
		TS_ASSERT(!s.paused());
		drain(s, msgs);
		TS_ASSERT(!findEvent(msgs, "Debugger.paused"));
		freeAll(msgs);
	}

	// GameScript watchpoints: write access pauses with vendor data.
	void test_watchpoint() {
		MockAgent agent;
		Inspector::Session s(&agent);
		s.onMessage("{\"id\":1,\"method\":\"Debugger.enable\"}");
		agent.script = s.registerScript(logicScript());
		s.onMessage("{\"id\":2,\"method\":\"GameScript.setWatchpoint\",\"params\":"
		            "{\"variable\":\"V13\",\"accessType\":\"write\"}}");
		Common::Array<Common::JSONValue *> msgs;
		drain(s, msgs);
		const Common::JSONValue *resp = findResponse(msgs, 2);
		TS_ASSERT(resp);
		freeAll(msgs);
		TS_ASSERT(s.watchArmed());

		ScriptedPump pump;
		pump.session = &s;
		s.setPausePump(&pump);
		pump.queue("{\"id\":10,\"method\":\"Debugger.resume\"}");

		// Reads don't trigger a write watchpoint.
		s.variableAccessHook(1, "V13", false, Inspector::DebugValue::fromInt(5));
		TS_ASSERT(!s.paused());
		drain(s, msgs);
		TS_ASSERT(!findEvent(msgs, "Debugger.paused"));
		freeAll(msgs);

		agent.offset = 0x00;
		s.variableAccessHook(1, "V13", true, Inspector::DebugValue::fromInt(9));
		drain(s, msgs);
		const Common::JSONValue *paused = findEvent(msgs, "Debugger.paused");
		TS_ASSERT(paused);
		if (paused) {
			const Common::JSONObject &p = paused->asObject()["params"]->asObject();
			const Common::JSONObject &data = p["data"]->asObject();
			TS_ASSERT_EQUALS(data["gameScriptReason"]->asString(), "watchpoint");
			TS_ASSERT_EQUALS(data["variable"]->asString(), "V13");
			TS_ASSERT_EQUALS(data["access"]->asString(), "write");
		}
		freeAll(msgs);
	}

	// Exceptions: Runtime.exceptionThrown always (when enabled); pause
	// only when setPauseOnExceptions is armed.
	void test_exceptions() {
		MockAgent agent;
		Inspector::Session s(&agent);
		s.onMessage("{\"id\":1,\"method\":\"Runtime.enable\"}");
		s.onMessage("{\"id\":2,\"method\":\"Debugger.enable\"}");
		agent.script = s.registerScript(logicScript());
		Common::Array<Common::JSONValue *> msgs;
		drain(s, msgs);
		freeAll(msgs);

		// Not armed: event only, no pause.
		s.reportException(1, agent.script, 0x05, "invalid opcode");
		TS_ASSERT(!s.paused());
		drain(s, msgs);
		TS_ASSERT(findEvent(msgs, "Runtime.exceptionThrown"));
		TS_ASSERT(!findEvent(msgs, "Debugger.paused"));
		freeAll(msgs);

		s.onMessage("{\"id\":3,\"method\":\"Debugger.setPauseOnExceptions\",\"params\":{\"state\":\"all\"}}");
		drain(s, msgs);
		freeAll(msgs);
		ScriptedPump pump;
		pump.session = &s;
		s.setPausePump(&pump);
		pump.queue("{\"id\":10,\"method\":\"Debugger.resume\"}");
		agent.offset = 0x05;
		s.reportException(1, agent.script, 0x05, "script error 42");
		drain(s, msgs);
		const Common::JSONValue *paused = findEvent(msgs, "Debugger.paused");
		TS_ASSERT(paused);
		if (paused)
			TS_ASSERT_EQUALS(paused->asObject()["params"]->asObject()["reason"]->asString(),
			                 "exception");
		freeAll(msgs);
	}

	// getScriptSource and getPossibleBreakpoints (statement lines only).
	void test_source_and_possible_breakpoints() {
		MockAgent agent;
		Inspector::Session s(&agent);
		s.onMessage("{\"id\":1,\"method\":\"Debugger.enable\"}");
		agent.script = s.registerScript(logicScript());
		Common::Array<Common::JSONValue *> msgs;
		drain(s, msgs);
		Common::String scriptId;
		const Common::JSONValue *parsed = findEvent(msgs, "Debugger.scriptParsed");
		TS_ASSERT(parsed);
		if (parsed)
			scriptId = parsed->asObject()["params"]->asObject()["scriptId"]->asString();
		freeAll(msgs);

		s.onMessage(Common::String::format(
			"{\"id\":2,\"method\":\"Debugger.getScriptSource\",\"params\":{\"scriptId\":\"%s\"}}",
			scriptId.c_str()));
		s.onMessage(Common::String::format(
			"{\"id\":3,\"method\":\"Debugger.getPossibleBreakpoints\",\"params\":"
			"{\"start\":{\"scriptId\":\"%s\",\"lineNumber\":0}}}", scriptId.c_str()));
		drain(s, msgs);
		const Common::JSONValue *src = findResponse(msgs, 2);
		TS_ASSERT(src);
		if (src) {
			Common::String text = src->asObject()["result"]->asObject()["scriptSource"]->asString();
			TS_ASSERT(text.contains("assign v13, 5"));
		}
		const Common::JSONValue *possible = findResponse(msgs, 3);
		TS_ASSERT(possible);
		if (possible) {
			const Common::JSONArray &locs =
				possible->asObject()["result"]->asObject()["locations"]->asArray();
			// Lines 0, 2, 3, 4 are statements; line 1 is not.
			TS_ASSERT_EQUALS(locs.size(), 4u);
			TS_ASSERT_EQUALS(locs[1]->asObject()["lineNumber"]->asIntegerNumber(), 2);
		}
		freeAll(msgs);
	}

	// Disconnect while paused: resume without events, drop breakpoints
	// (node's detach behaviour), disarm everything.
	void test_disconnect_cleans_up() {
		MockAgent agent;
		Inspector::Session s(&agent);
		s.onMessage("{\"id\":1,\"method\":\"Debugger.enable\"}");
		agent.script = s.registerScript(logicScript());
		s.onMessage("{\"id\":2,\"method\":\"Debugger.setBreakpointByUrl\",\"params\":"
		            "{\"lineNumber\":0,\"url\":\"scummvm-dbg://mock/logic-0\"}}");
		Common::Array<Common::JSONValue *> msgs;
		drain(s, msgs);
		freeAll(msgs);
		TS_ASSERT(s.armed());

		s.onDisconnect();
		TS_ASSERT(!s.armed());
		TS_ASSERT(!s.debuggerEnabled());
		s.instructionHook(1, agent.script, 0x00, 1);
		TS_ASSERT(!s.paused());

		// A pump that returns false immediately (dead transport) must
		// abort any later pause instead of hanging: simulated via pause
		// request with no pump at all.
		s.onMessage("{\"id\":3,\"method\":\"Debugger.enable\"}");
		s.onMessage("{\"id\":4,\"method\":\"Debugger.pause\"}");
		s.instructionHook(1, agent.script, 0x00, 1);
		TS_ASSERT(!s.paused()); // pause aborted, VM alive
		drain(s, msgs);
		freeAll(msgs);
	}

	// Debugger.pause pauses at the next statement in any thread.
	void test_explicit_pause() {
		MockAgent agent;
		Inspector::Session s(&agent);
		s.onMessage("{\"id\":1,\"method\":\"Debugger.enable\"}");
		agent.script = s.registerScript(logicScript());
		s.onMessage("{\"id\":2,\"method\":\"Debugger.pause\"}");
		Common::Array<Common::JSONValue *> msgs;
		drain(s, msgs);
		TS_ASSERT(findResponse(msgs, 2)); // ack precedes the pause
		freeAll(msgs);

		ScriptedPump pump;
		pump.session = &s;
		s.setPausePump(&pump);
		pump.queue("{\"id\":3,\"method\":\"Debugger.resume\"}");
		agent.offset = 0x03; // NOT a statement line: no pause yet
		s.instructionHook(7, agent.script, 0x03, 1);
		TS_ASSERT(s.armed());
		drain(s, msgs);
		TS_ASSERT(!findEvent(msgs, "Debugger.paused"));
		freeAll(msgs);
		agent.offset = 0x05;
		s.instructionHook(7, agent.script, 0x05, 1);
		drain(s, msgs);
		TS_ASSERT(findEvent(msgs, "Debugger.paused"));
		freeAll(msgs);
	}

	// Stepping while not paused is a server error, mirroring V8.
	void test_step_requires_pause() {
		MockAgent agent;
		Inspector::Session s(&agent);
		s.onMessage("{\"id\":1,\"method\":\"Debugger.enable\"}");
		s.onMessage("{\"id\":2,\"method\":\"Debugger.stepOver\"}");
		Common::Array<Common::JSONValue *> msgs;
		drain(s, msgs);
		const Common::JSONValue *resp = findResponse(msgs, 2);
		TS_ASSERT(resp);
		if (resp)
			TS_ASSERT(resp->asObject().contains("error"));
		freeAll(msgs);
	}
};
