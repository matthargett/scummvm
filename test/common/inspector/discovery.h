#include <cxxtest/TestSuite.h>

#include "common/inspector/discovery.h"
#include "common/formats/json.h"

/**
 * HTTP discovery endpoints + WebSocket upgrade (ledger #21, #24, #25):
 * node-shaped /json/version and /json/list, query-string tolerance
 * (Chrome's ?for_tab broke workerd), Host validation, and the RFC 6455
 * handshake vector.
 */
class InspectorDiscoveryTestSuite : public CxxTest::TestSuite {
	typedef Inspector::DiscoveryHandler Handler;
	typedef Inspector::HttpRequest Request;

	static Handler makeHandler() {
		return Handler(9229, "0f2c936f-b1cd-4ac9-aab3-f63b0f33d55e",
		               "Day of the Tentacle (SCUMM)", "2.10.0git");
	}

	static Request get(const char *target, const char *host = "127.0.0.1:9229") {
		Request r;
		Common::String raw = Common::String::format(
			"GET %s HTTP/1.1\r\nHost: %s\r\nAccept: */*\r\n\r\n", target, host);
		TS_ASSERT(r.parse(raw));
		return r;
	}

	// Extract the body (after the blank line) from a raw HTTP response.
	static Common::String body(const Common::String &response) {
		const char *sep = strstr(response.c_str(), "\r\n\r\n");
		TS_ASSERT(sep);
		if (!sep)
			return Common::String();
		return Common::String(sep + 4);
	}

public:
	void test_request_parsing() {
		Request r;
		TS_ASSERT(r.parse("GET /json/list?for_tab HTTP/1.1\r\n"
		                  "Host: localhost:9229\r\n"
		                  "Sec-WebSocket-Key:  abc==  \r\n\r\n"));
		TS_ASSERT_EQUALS(r.method, "GET");
		TS_ASSERT_EQUALS(r.path, "/json/list");
		TS_ASSERT_EQUALS(r.query, "for_tab");
		TS_ASSERT_EQUALS(r.header("host"), "localhost:9229");
		TS_ASSERT_EQUALS(r.header("HOST"), "localhost:9229"); // case-insensitive
		TS_ASSERT_EQUALS(r.header("sec-websocket-key"), "abc=="); // trimmed
		TS_ASSERT_EQUALS(r.header("missing"), "");

		Request incomplete;
		TS_ASSERT(!incomplete.parse("GET /json HTTP/1.1\r\nHost: x\r\n")); // no terminator
		TS_ASSERT(!Request::isComplete("GET / HTTP/1.1\r\n"));
		TS_ASSERT(Request::isComplete("GET / HTTP/1.1\r\n\r\n"));
		Request bogus;
		TS_ASSERT(!bogus.parse("NOT-HTTP\r\n\r\n"));
	}

	// Node's /json/version has exactly Browser and Protocol-Version and
	// NO webSocketDebuggerUrl — js-debug dual-fetches because of this.
	void test_json_version() {
		Handler h = makeHandler();
		Handler::Response resp = h.handleRequest(get("/json/version"));
		TS_ASSERT(!resp.upgraded);
		TS_ASSERT(resp.raw.contains("200 OK"));
		Common::JSONValue *v = Common::JSON::parse(body(resp.raw).c_str());
		TS_ASSERT(v);
		if (!v)
			return;
		const Common::JSONObject &o = v->asObject();
		TS_ASSERT_EQUALS(o["Browser"]->asString(), "ScummVM/2.10.0git");
		TS_ASSERT_EQUALS(o["Protocol-Version"]->asString(), "1.1");
		TS_ASSERT(!o.contains("webSocketDebuggerUrl"));
		delete v;
	}

	void test_json_list_shape() {
		Handler h = makeHandler();
		// /json, /json/ and /json/list are all the list endpoint.
		static const char *paths[] = { "/json", "/json/", "/json/list" };
		for (uint32 i = 0; i < ARRAYSIZE(paths); i++) {
			Handler::Response resp = h.handleRequest(get(paths[i]));
			Common::JSONValue *v = Common::JSON::parse(body(resp.raw).c_str());
			TS_ASSERT(v);
			if (!v)
				continue;
			const Common::JSONArray &list = v->asArray();
			TS_ASSERT_EQUALS(list.size(), 1u);
			const Common::JSONObject &t = list[0]->asObject();
			TS_ASSERT_EQUALS(t["type"]->asString(), "node");
			TS_ASSERT_EQUALS(t["id"]->asString(), "0f2c936f-b1cd-4ac9-aab3-f63b0f33d55e");
			TS_ASSERT_EQUALS(t["title"]->asString(), "Day of the Tentacle (SCUMM)");
			TS_ASSERT_EQUALS(t["webSocketDebuggerUrl"]->asString(),
				"ws://127.0.0.1:9229/0f2c936f-b1cd-4ac9-aab3-f63b0f33d55e");
			TS_ASSERT(t.contains("devtoolsFrontendUrl"));
			delete v;
		}
	}

	// Chrome sends /json/list?for_tab; matching must ignore the query
	// string (workerd#1388 — ledger #24).
	void test_query_string_ignored() {
		Handler h = makeHandler();
		Handler::Response resp = h.handleRequest(get("/json/list?for_tab"));
		TS_ASSERT(resp.raw.contains("200 OK"));
		Handler::Response resp2 = h.handleRequest(get("/json/version?x=1&y=2"));
		TS_ASSERT(resp2.raw.contains("200 OK"));
	}

	// RFC 6455 section 1.3 handshake vector (ledger #21).
	void test_websocket_upgrade() {
		Handler h = makeHandler();
		Request r;
		TS_ASSERT(r.parse(
			"GET /0f2c936f-b1cd-4ac9-aab3-f63b0f33d55e HTTP/1.1\r\n"
			"Host: 127.0.0.1:9229\r\n"
			"Upgrade: websocket\r\n"
			"Connection: Upgrade\r\n"
			"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
			"Sec-WebSocket-Version: 13\r\n\r\n"));
		Handler::Response resp = h.handleRequest(r);
		TS_ASSERT(resp.upgraded);
		TS_ASSERT(resp.keepOpen);
		TS_ASSERT(resp.raw.contains("HTTP/1.1 101 Switching Protocols"));
		TS_ASSERT(resp.raw.contains("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")); // RFC vector
		TS_ASSERT(resp.raw.contains("Upgrade: websocket"));
	}

	void test_accept_key_vector() {
		TS_ASSERT_EQUALS(Handler::computeAcceptKey("dGhlIHNhbXBsZSBub25jZQ=="),
		                 "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
	}

	// A wrong path must get a clean 404 BEFORE any upgrade; a GET to the
	// ws path without upgrade headers is a 400.
	void test_unknown_path_and_bad_upgrade() {
		Handler h = makeHandler();
		Handler::Response resp = h.handleRequest(get("/devtools/page/wrong-uuid"));
		TS_ASSERT(!resp.upgraded);
		TS_ASSERT(resp.raw.contains("404"));
		Handler::Response noUpgrade =
			h.handleRequest(get("/0f2c936f-b1cd-4ac9-aab3-f63b0f33d55e"));
		TS_ASSERT(!noUpgrade.upgraded);
		TS_ASSERT(noUpgrade.raw.contains("400"));
	}

	// Host validation, node-style (ledger #25): loopback spellings pass —
	// js-debug tries 127.0.0.1 AND [::1] — hostnames are rejected (DNS
	// rebinding defence).
	void test_host_validation() {
		TS_ASSERT(Handler::hostAllowed("localhost:9229"));
		TS_ASSERT(Handler::hostAllowed("localhost"));
		TS_ASSERT(Handler::hostAllowed("LOCALHOST:9229"));
		TS_ASSERT(Handler::hostAllowed("127.0.0.1:9229"));
		TS_ASSERT(Handler::hostAllowed("[::1]:9229"));
		TS_ASSERT(Handler::hostAllowed("[fe80::1]:9229"));
		TS_ASSERT(Handler::hostAllowed("192.168.1.20:9229")); // IP literals pass
		TS_ASSERT(Handler::hostAllowed(""));                  // no header at all
		TS_ASSERT(!Handler::hostAllowed("evil.example.com:9229"));
		TS_ASSERT(!Handler::hostAllowed("mymachine.local"));

		Handler h = makeHandler();
		Handler::Response resp = h.handleRequest(get("/json/list", "evil.example.com"));
		TS_ASSERT(resp.raw.contains("400"));
	}

	void test_non_get_rejected() {
		Handler h = makeHandler();
		Request r;
		TS_ASSERT(r.parse("PUT /json/new HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"));
		Handler::Response resp = h.handleRequest(r);
		TS_ASSERT(resp.raw.contains("405"));
	}
};
