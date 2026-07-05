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

#ifndef COMMON_INSPECTOR_PROTOCOL_H
#define COMMON_INSPECTOR_PROTOCOL_H

#include "common/scummsys.h"
#include "common/formats/json.h"
#include "common/str.h"

namespace Inspector {

/**
 * Chrome DevTools Protocol message framing.
 *
 * CDP is JSON-RPC-shaped but not JSON-RPC: commands are
 * {"id": <integer>, "method": "Domain.name", "params": {...}?}, success
 * replies {"id": n, "result": {...}}, errors {"id": n, "error": {"code":
 * c, "message": m}}, and events {"method": ..., "params": ...} without an
 * id. V8 reuses the JSON-RPC error codes.
 *
 * Iron rules from client behaviour (see DESIGN.md ledger #1, #3, #28):
 * reply to every command exactly once with the same id — even if only
 * with kErrMethodNotFound (clients feature-detect on it); process
 * commands strictly in the order received; never emit an id on events.
 */
enum ProtocolErrorCode {
	kErrParse          = -32700,
	kErrInvalidRequest = -32600,
	kErrMethodNotFound = -32601,
	kErrInvalidParams  = -32602,
	kErrServer         = -32000
};

/** One parsed incoming command. Non-copyable; owns the parsed JSON tree. */
class Command {
public:
	Command();
	~Command();

	/**
	 * Parse a message. On failure returns false and sets @p errorCode to
	 * kErrParse (malformed JSON) or kErrInvalidRequest (well-formed JSON
	 * that is not a valid command: missing/non-integer id, missing
	 * method, non-object params).
	 */
	bool parse(const Common::String &json, int &errorCode);

	int64 id() const { return _id; }
	/** True if the message carried an id (needed to answer at all). */
	bool hasId() const { return _hasId; }
	const Common::String &method() const { return _method; }
	/** "Debugger" for "Debugger.enable". */
	Common::String domain() const;

	/** Parameter accessors. Return false if absent or wrong type. */
	bool getInt(const char *name, int64 &out) const;
	bool getString(const char *name, Common::String &out) const;
	bool getBool(const char *name, bool &out) const;
	/** The raw params object, or nullptr if none was sent. */
	const Common::JSONObject *params() const { return _params; }

private:
	Common::JSONValue *_root;
	const Common::JSONObject *_params;
	Common::String _method;
	int64 _id;
	bool _hasId;

	Command(const Command &);            // = delete
	Command &operator=(const Command &); // = delete
};

/**
 * Outgoing message builders. All take ownership of the JSONValue they are
 * given and return the serialized message. The serializer emits pure-ASCII
 * JSON (non-ASCII escaped as \uXXXX), which conveniently guarantees every
 * outgoing WebSocket text frame is valid UTF-8 (ledger #19).
 */
Common::String buildResult(int64 id, Common::JSONValue *result);
Common::String buildError(int64 id, int code, const Common::String &message);
Common::String buildEvent(const Common::String &method, Common::JSONValue *params);

} // End of namespace Inspector

#endif
