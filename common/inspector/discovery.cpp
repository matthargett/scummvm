/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "common/inspector/discovery.h"
#include "common/inspector/sha1.h"
#include "common/base64.h"
#include "common/formats/json.h"

namespace Inspector {

bool HttpRequest::isComplete(const Common::String &raw) {
	return raw.contains("\r\n\r\n");
}

bool HttpRequest::parse(const Common::String &raw) {
	method.clear();
	path.clear();
	query.clear();
	headers.clear();

	if (!isComplete(raw))
		return false;

	// Request line: METHOD SP PATH SP VERSION CRLF
	uint32 lineEnd = 0;
	while (lineEnd + 1 < raw.size() && !(raw[lineEnd] == '\r' && raw[lineEnd + 1] == '\n'))
		lineEnd++;
	Common::String requestLine(raw.c_str(), lineEnd);

	uint32 sp1 = 0;
	while (sp1 < requestLine.size() && requestLine[sp1] != ' ')
		sp1++;
	if (sp1 == 0 || sp1 >= requestLine.size())
		return false;
	method = Common::String(requestLine.c_str(), sp1);

	uint32 sp2 = sp1 + 1;
	while (sp2 < requestLine.size() && requestLine[sp2] != ' ')
		sp2++;
	if (sp2 == sp1 + 1 || sp2 >= requestLine.size())
		return false;
	Common::String target(requestLine.c_str() + sp1 + 1, sp2 - sp1 - 1);
	if (!requestLine.contains("HTTP/"))
		return false;

	// Split the query string off: discovery paths must match with or
	// without one (Chrome sends /json/list?for_tab — workerd#1388).
	uint32 qPos = 0;
	bool hasQuery = false;
	for (uint32 i = 0; i < target.size(); i++) {
		if (target[i] == '?') {
			qPos = i;
			hasQuery = true;
			break;
		}
	}
	if (hasQuery) {
		path = Common::String(target.c_str(), qPos);
		query = Common::String(target.c_str() + qPos + 1);
	} else {
		path = target;
	}

	// Header lines up to the blank line.
	uint32 pos = lineEnd + 2;
	while (pos + 1 < raw.size()) {
		if (raw[pos] == '\r' && raw[pos + 1] == '\n')
			break; // blank line: end of head
		uint32 end = pos;
		while (end + 1 < raw.size() && !(raw[end] == '\r' && raw[end + 1] == '\n'))
			end++;
		Common::String line(raw.c_str() + pos, end - pos);
		for (uint32 i = 0; i < line.size(); i++) {
			if (line[i] == ':') {
				Common::String name(line.c_str(), i);
				name.toLowercase();
				uint32 vStart = i + 1;
				while (vStart < line.size() && line[vStart] == ' ')
					vStart++;
				uint32 vEnd = line.size();
				while (vEnd > vStart && (line[vEnd - 1] == ' ' || line[vEnd - 1] == '\t'))
					vEnd--;
				headers[name] = Common::String(line.c_str() + vStart, vEnd - vStart);
				break;
			}
		}
		pos = end + 2;
	}
	return true;
}

Common::String HttpRequest::header(const Common::String &name) const {
	Common::String key = name;
	key.toLowercase();
	Common::HashMap<Common::String, Common::String>::const_iterator it = headers.find(key);
	if (it == headers.end())
		return Common::String();
	return it->_value;
}

DiscoveryHandler::DiscoveryHandler(uint16 port, const Common::String &uuid,
                                   const Common::String &title, const Common::String &version) :
	_port(port), _uuid(uuid), _title(title), _version(version) {
}

Common::String DiscoveryHandler::computeAcceptKey(const Common::String &secWebSocketKey) {
	// RFC 6455 4.2.2 /4/: concatenate the base64 key AS RECEIVED (not
	// decoded) with the fixed GUID, SHA-1 it, base64 the digest.
	Common::String input = secWebSocketKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	SHA1Digest digest = sha1(input);
	return Common::b64EncodeData(digest.bytes, 20);
}

bool DiscoveryHandler::hostAllowed(const Common::String &hostHeader) {
	if (hostHeader.empty())
		return true; // HTTP/1.0-style clients; nothing to validate
	// Strip the port. IPv6 literals come bracketed: [::1]:9229.
	Common::String host = hostHeader;
	if (host[0] == '[') {
		for (uint32 i = 1; i < host.size(); i++) {
			if (host[i] == ']') {
				host = Common::String(host.c_str() + 1, i - 1);
				break;
			}
		}
	} else {
		for (uint32 i = 0; i < host.size(); i++) {
			if (host[i] == ':') {
				host = Common::String(host.c_str(), i);
				break;
			}
		}
	}
	host.toLowercase();
	if (host == "localhost")
		return true;
	// IPv4 literal: digits and dots only.
	bool ipv4 = !host.empty();
	for (uint32 i = 0; i < host.size(); i++) {
		char c = host[i];
		if ((c < '0' || c > '9') && c != '.') {
			ipv4 = false;
			break;
		}
	}
	if (ipv4)
		return true;
	// IPv6 literal: hex digits and colons.
	bool ipv6 = host.contains(':');
	for (uint32 i = 0; ipv6 && i < host.size(); i++) {
		char c = host[i];
		bool hexDigit = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == ':' || c == '.';
		if (!hexDigit)
			ipv6 = false;
	}
	return ipv6;
}

Common::String DiscoveryHandler::httpResponse(int code, const Common::String &status,
                                              const Common::String &contentType,
                                              const Common::String &body) {
	Common::String response = Common::String::format("HTTP/1.1 %d %s\r\n", code, status.c_str());
	response += "Content-Type: " + contentType + "\r\n";
	response += Common::String::format("Content-Length: %u\r\n", body.size());
	response += "Connection: close\r\n\r\n";
	response += body;
	return response;
}

Common::String DiscoveryHandler::jsonVersionBody() const {
	// Node-shaped: exactly Browser and Protocol-Version, and NO
	// webSocketDebuggerUrl — js-debug's dual /json/version + /json/list
	// fetch exists precisely because node omits it here.
	Common::JSONObject obj;
	obj["Browser"] = new Common::JSONValue(Common::String("ScummVM/") + _version);
	obj["Protocol-Version"] = new Common::JSONValue("1.1");
	Common::JSONValue root(obj);
	return root.stringify();
}

Common::String DiscoveryHandler::jsonListBody() const {
	Common::String wsUrl = Common::String::format("ws://127.0.0.1:%u/%s", _port, _uuid.c_str());
	Common::JSONObject target;
	target["description"] = new Common::JSONValue("ScummVM game script instance");
	target["devtoolsFrontendUrl"] = new Common::JSONValue(Common::String::format(
		"devtools://devtools/bundled/js_app.html?experiments=true&v8only=true&ws=127.0.0.1:%u/%s",
		_port, _uuid.c_str()));
	target["faviconUrl"] = new Common::JSONValue("https://www.scummvm.org/favicon.ico");
	target["id"] = new Common::JSONValue(_uuid);
	target["title"] = new Common::JSONValue(_title);
	// type "node" groups the target correctly in chrome://inspect and is
	// what js-debug's node attach flow expects.
	target["type"] = new Common::JSONValue("node");
	target["url"] = new Common::JSONValue("file://");
	target["webSocketDebuggerUrl"] = new Common::JSONValue(wsUrl);
	Common::JSONArray list;
	list.push_back(new Common::JSONValue(target));
	Common::JSONValue root(list);
	return root.stringify();
}

DiscoveryHandler::Response DiscoveryHandler::handleRequest(const HttpRequest &request) const {
	Response response;

	if (!hostAllowed(request.header("host"))) {
		response.raw = httpResponse(400, "Bad Request", "text/plain",
			"Host header is specified and is not an IP address or localhost.\n");
		return response;
	}

	if (request.method != "GET") {
		response.raw = httpResponse(405, "Method Not Allowed", "text/plain", "GET only\n");
		return response;
	}

	// Query strings are ignored when matching (ledger #24).
	const Common::String &path = request.path;

	if (path == "/json" || path == "/json/" || path == "/json/list") {
		response.raw = httpResponse(200, "OK", "application/json; charset=UTF-8", jsonListBody());
		return response;
	}
	if (path == "/json/version") {
		response.raw = httpResponse(200, "OK", "application/json; charset=UTF-8", jsonVersionBody());
		return response;
	}

	// The WebSocket endpoint lives at exactly the advertised path.
	if (path == "/" + _uuid) {
		Common::String upgrade = request.header("upgrade");
		upgrade.toLowercase();
		Common::String key = request.header("sec-websocket-key");
		if (upgrade != "websocket" || key.empty()) {
			response.raw = httpResponse(400, "Bad Request", "text/plain",
			                            "WebSocket upgrade required\n");
			return response;
		}
		response.raw = "HTTP/1.1 101 Switching Protocols\r\n"
		               "Upgrade: websocket\r\n"
		               "Connection: Upgrade\r\n"
		               "Sec-WebSocket-Accept: " + computeAcceptKey(key) + "\r\n\r\n";
		response.upgraded = true;
		response.keepOpen = true;
		return response;
	}

	// Unknown path: refuse BEFORE upgrading (a client that guessed a
	// wrong ws path must get a clean HTTP error, not a socket hang).
	response.raw = httpResponse(404, "Not Found", "text/plain", "Unknown path\n");
	return response;
}

} // End of namespace Inspector
