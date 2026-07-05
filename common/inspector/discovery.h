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

#ifndef COMMON_INSPECTOR_DISCOVERY_H
#define COMMON_INSPECTOR_DISCOVERY_H

#include "common/scummsys.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/str.h"

namespace Inspector {

/**
 * The HTTP side of a node-like CDP target: the /json discovery endpoints
 * VS Code js-debug and chrome://inspect poll, plus the RFC 6455 upgrade
 * handshake for the advertised WebSocket path. Pure string-in/string-out
 * so the whole exchange is unit-testable; the socket pump in backends/
 * feeds it bytes.
 *
 * Client contract notes (DESIGN.md ledger #21, #24, #25):
 *  - js-debug fetches BOTH /json/version and /json/list and uses
 *    whichever yields webSocketDebuggerUrl; node deliberately omits it
 *    from /json/version, and so do we.
 *  - query strings on discovery paths are ignored — recent Chrome sends
 *    /json/list?for_tab and workerd broke on it (workerd#1388).
 *  - the Host header is validated node-style against DNS rebinding:
 *    IP literals and localhost pass, hostnames are rejected.
 */
class HttpRequest {
public:
	Common::String method;   ///< "GET"
	Common::String path;     ///< path without the query string
	Common::String query;    ///< query string (no '?'), "" if none
	Common::HashMap<Common::String, Common::String> headers; ///< keys lowercased

	/**
	 * Parse one HTTP/1.1 request head (up to the blank line; any body is
	 * ignored — the discovery protocol is GET-only).
	 * @return false if @p raw is not a complete, well-formed request head
	 */
	bool parse(const Common::String &raw);

	/** Complete when the header terminator has arrived. */
	static bool isComplete(const Common::String &raw);

	Common::String header(const Common::String &name) const;
};

class DiscoveryHandler {
public:
	/**
	 * @param port  advertised in webSocketDebuggerUrl
	 * @param uuid  target id; the WebSocket endpoint is served at /<uuid>
	 * @param title human-readable target title (game description)
	 * @param version ScummVM version string for the Browser field
	 */
	DiscoveryHandler(uint16 port, const Common::String &uuid,
	                 const Common::String &title, const Common::String &version);

	struct Response {
		Common::String raw;   ///< full HTTP response bytes to send
		bool upgraded;        ///< true: switch this connection to WebSocket
		bool keepOpen;        ///< false: close after sending raw

		Response() : upgraded(false), keepOpen(false) {}
	};

	/** Serve one request: discovery JSON, WS upgrade, or 404. */
	Response handleRequest(const HttpRequest &request) const;

	/** RFC 6455 4.2.2: base64(SHA1(key + GUID)) — the GUID is the fixed
	 *  "258EAFA5-E914-47DA-95CA-C5AB0DC85B11". */
	static Common::String computeAcceptKey(const Common::String &secWebSocketKey);

	/** Node-style DNS-rebinding defence: accept IP literals (v4/v6) and
	 *  localhost, with or without port; reject anything else. */
	static bool hostAllowed(const Common::String &hostHeader);

	const Common::String &uuid() const { return _uuid; }

private:
	uint16 _port;
	Common::String _uuid;
	Common::String _title;
	Common::String _version;

	Common::String jsonVersionBody() const;
	Common::String jsonListBody() const;
	static Common::String httpResponse(int code, const Common::String &status,
	                                   const Common::String &contentType,
	                                   const Common::String &body);
};

} // End of namespace Inspector

#endif
