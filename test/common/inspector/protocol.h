#include <cxxtest/TestSuite.h>

#include "common/inspector/protocol.h"

/**
 * CDP message framing (ledger #3, #5, #28): integer ids echoed verbatim,
 * optional params, JSON-RPC-style error codes, events without ids.
 */
class InspectorProtocolTestSuite : public CxxTest::TestSuite {
public:
	void test_parse_full_command() {
		Inspector::Command cmd;
		int err = 0;
		TS_ASSERT(cmd.parse("{\"id\":42,\"method\":\"Debugger.setBreakpointByUrl\","
		                    "\"params\":{\"lineNumber\":7,\"url\":\"file:///x\",\"ok\":true}}", err));
		TS_ASSERT_EQUALS(cmd.id(), 42);
		TS_ASSERT(cmd.hasId());
		TS_ASSERT_EQUALS(cmd.method(), "Debugger.setBreakpointByUrl");
		TS_ASSERT_EQUALS(cmd.domain(), "Debugger");
		int64 line = -1;
		TS_ASSERT(cmd.getInt("lineNumber", line));
		TS_ASSERT_EQUALS(line, 7);
		Common::String url;
		TS_ASSERT(cmd.getString("url", url));
		TS_ASSERT_EQUALS(url, "file:///x");
		bool ok = false;
		TS_ASSERT(cmd.getBool("ok", ok));
		TS_ASSERT(ok);
		// Absent / wrong-typed params fail cleanly.
		int64 nope;
		TS_ASSERT(!cmd.getInt("url", nope));
		TS_ASSERT(!cmd.getInt("missing", nope));
	}

	void test_params_are_optional() {
		Inspector::Command cmd;
		int err = 0;
		TS_ASSERT(cmd.parse("{\"id\":1,\"method\":\"Debugger.enable\"}", err));
		TS_ASSERT(cmd.params() == nullptr);
	}

	void test_id_zero_is_valid() {
		Inspector::Command cmd;
		int err = 0;
		TS_ASSERT(cmd.parse("{\"id\":0,\"method\":\"Runtime.enable\"}", err));
		TS_ASSERT_EQUALS(cmd.id(), 0);
	}

	void test_malformed_json_is_parse_error() {
		Inspector::Command cmd;
		int err = 0;
		TS_ASSERT(!cmd.parse("{\"id\":1,", err));
		TS_ASSERT_EQUALS(err, (int)Inspector::kErrParse);
	}

	void test_missing_id_is_invalid_request() {
		Inspector::Command cmd;
		int err = 0;
		TS_ASSERT(!cmd.parse("{\"method\":\"Debugger.enable\"}", err));
		TS_ASSERT_EQUALS(err, (int)Inspector::kErrInvalidRequest);
	}

	void test_non_integer_id_is_invalid_request() {
		Inspector::Command cmd;
		int err = 0;
		TS_ASSERT(!cmd.parse("{\"id\":\"one\",\"method\":\"Debugger.enable\"}", err));
		TS_ASSERT_EQUALS(err, (int)Inspector::kErrInvalidRequest);
	}

	void test_missing_method_is_invalid_request() {
		Inspector::Command cmd;
		int err = 0;
		TS_ASSERT(!cmd.parse("{\"id\":3}", err));
		TS_ASSERT_EQUALS(err, (int)Inspector::kErrInvalidRequest);
	}

	void test_non_object_root_is_invalid_request() {
		Inspector::Command cmd;
		int err = 0;
		TS_ASSERT(!cmd.parse("[1,2,3]", err));
		TS_ASSERT_EQUALS(err, (int)Inspector::kErrInvalidRequest);
	}

	void test_build_result_echoes_id() {
		Common::JSONObject payload;
		payload["debuggerId"] = new Common::JSONValue("scummvm-debugger-1");
		Common::String msg = Inspector::buildResult(9, new Common::JSONValue(payload));
		Common::JSONValue *parsed = Common::JSON::parse(msg.c_str());
		TS_ASSERT(parsed);
		if (!parsed)
			return;
		TS_ASSERT_EQUALS(parsed->asObject()["id"]->asIntegerNumber(), 9);
		TS_ASSERT_EQUALS(parsed->asObject()["result"]->asObject()["debuggerId"]->asString(),
		                 "scummvm-debugger-1");
		TS_ASSERT(!parsed->asObject().contains("error"));
		delete parsed;
	}

	void test_build_empty_result() {
		// Most acks are {"id":n,"result":{}} — the result member must be
		// present even when empty.
		Common::String msg = Inspector::buildResult(1, new Common::JSONValue(Common::JSONObject()));
		Common::JSONValue *parsed = Common::JSON::parse(msg.c_str());
		TS_ASSERT(parsed);
		if (!parsed)
			return;
		TS_ASSERT(parsed->asObject().contains("result"));
		TS_ASSERT(parsed->asObject()["result"]->isObject());
		delete parsed;
	}

	void test_build_error_shape() {
		Common::String msg = Inspector::buildError(7, Inspector::kErrMethodNotFound,
		                                           "'Network.enable' wasn't found");
		Common::JSONValue *parsed = Common::JSON::parse(msg.c_str());
		TS_ASSERT(parsed);
		if (!parsed)
			return;
		const Common::JSONObject &obj = parsed->asObject();
		TS_ASSERT_EQUALS(obj["id"]->asIntegerNumber(), 7);
		TS_ASSERT_EQUALS(obj["error"]->asObject()["code"]->asIntegerNumber(), -32601);
		TS_ASSERT_EQUALS(obj["error"]->asObject()["message"]->asString(),
		                 "'Network.enable' wasn't found");
		delete parsed;
	}

	void test_build_event_has_no_id() {
		Common::JSONObject params;
		params["reason"] = new Common::JSONValue("other");
		Common::String msg = Inspector::buildEvent("Debugger.paused", new Common::JSONValue(params));
		Common::JSONValue *parsed = Common::JSON::parse(msg.c_str());
		TS_ASSERT(parsed);
		if (!parsed)
			return;
		TS_ASSERT(!parsed->asObject().contains("id"));
		TS_ASSERT_EQUALS(parsed->asObject()["method"]->asString(), "Debugger.paused");
		TS_ASSERT_EQUALS(parsed->asObject()["params"]->asObject()["reason"]->asString(), "other");
		delete parsed;
	}

	// Common::JSON escapes all non-ASCII, so any outgoing message is
	// plain ASCII — automatically valid UTF-8 on the wire (ledger #19).
	void test_output_is_ascii_even_for_utf8_input() {
		Common::JSONObject params;
		params["text"] = new Common::JSONValue("caf\xC3\xA9 \xF0\x9F\x92\xA9");
		Common::String msg = Inspector::buildEvent("Runtime.consoleAPICalled",
		                                           new Common::JSONValue(params));
		for (uint32 i = 0; i < msg.size(); i++)
			TS_ASSERT((byte)msg[i] < 0x80);
		// And it round-trips back to the same UTF-8 bytes.
		Common::JSONValue *parsed = Common::JSON::parse(msg.c_str());
		TS_ASSERT(parsed);
		if (!parsed)
			return;
		TS_ASSERT_EQUALS(parsed->asObject()["params"]->asObject()["text"]->asString(),
		                 "caf\xC3\xA9 \xF0\x9F\x92\xA9");
		delete parsed;
	}

	void test_double_id_is_accepted_as_integer() {
		// Some client libraries serialize ids as 1.0; V8 tolerates it.
		Inspector::Command cmd;
		int err = 0;
		TS_ASSERT(cmd.parse("{\"id\":5.0,\"method\":\"Runtime.enable\"}", err));
		TS_ASSERT_EQUALS(cmd.id(), 5);
	}
};
