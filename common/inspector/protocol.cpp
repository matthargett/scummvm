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

#include "common/inspector/protocol.h"

namespace Inspector {

Command::Command() : _root(nullptr), _params(nullptr), _id(0), _hasId(false) {
}

Command::~Command() {
	delete _root;
}

bool Command::parse(const Common::String &json, int &errorCode) {
	delete _root;
	_root = nullptr;
	_params = nullptr;
	_method.clear();
	_id = 0;
	_hasId = false;

	_root = Common::JSON::parse(json.c_str());
	if (!_root) {
		errorCode = kErrParse;
		return false;
	}
	if (!_root->isObject()) {
		errorCode = kErrInvalidRequest;
		return false;
	}
	const Common::JSONObject &obj = _root->asObject();

	if (!obj.contains("id")) {
		errorCode = kErrInvalidRequest;
		return false;
	}
	const Common::JSONValue *idVal = obj["id"];
	if (idVal->isIntegerNumber()) {
		_id = idVal->asIntegerNumber();
	} else if (idVal->isNumber()) {
		// Tolerate ids serialized as doubles (e.g. 5.0).
		_id = (int64)idVal->asNumber();
	} else {
		errorCode = kErrInvalidRequest;
		return false;
	}
	_hasId = true;

	if (!obj.contains("method") || !obj["method"]->isString()) {
		errorCode = kErrInvalidRequest;
		return false;
	}
	_method = obj["method"]->asString();

	if (obj.contains("params")) {
		if (!obj["params"]->isObject()) {
			errorCode = kErrInvalidRequest;
			return false;
		}
		_params = &obj["params"]->asObject();
	}
	return true;
}

Common::String Command::domain() const {
	for (uint32 i = 0; i < _method.size(); i++)
		if (_method[i] == '.')
			return Common::String(_method.c_str(), i);
	return _method;
}

bool Command::getInt(const char *name, int64 &out) const {
	if (!_params || !_params->contains(name))
		return false;
	const Common::JSONValue *v = (*_params)[name];
	if (v->isIntegerNumber()) {
		out = v->asIntegerNumber();
		return true;
	}
	if (v->isNumber()) {
		out = (int64)v->asNumber();
		return true;
	}
	return false;
}

bool Command::getString(const char *name, Common::String &out) const {
	if (!_params || !_params->contains(name))
		return false;
	const Common::JSONValue *v = (*_params)[name];
	if (!v->isString())
		return false;
	out = v->asString();
	return true;
}

bool Command::getBool(const char *name, bool &out) const {
	if (!_params || !_params->contains(name))
		return false;
	const Common::JSONValue *v = (*_params)[name];
	if (!v->isBool())
		return false;
	out = v->asBool();
	return true;
}

Common::String buildResult(int64 id, Common::JSONValue *result) {
	Common::JSONObject msg;
	msg["id"] = new Common::JSONValue((long long int)id);
	msg["result"] = result ? result : new Common::JSONValue(Common::JSONObject());
	Common::JSONValue root(msg);
	return root.stringify();
}

Common::String buildError(int64 id, int code, const Common::String &message) {
	Common::JSONObject err;
	err["code"] = new Common::JSONValue((long long int)code);
	err["message"] = new Common::JSONValue(message);
	Common::JSONObject msg;
	msg["id"] = new Common::JSONValue((long long int)id);
	msg["error"] = new Common::JSONValue(err);
	Common::JSONValue root(msg);
	return root.stringify();
}

Common::String buildEvent(const Common::String &method, Common::JSONValue *params) {
	Common::JSONObject msg;
	msg["method"] = new Common::JSONValue(method);
	msg["params"] = params ? params : new Common::JSONValue(Common::JSONObject());
	Common::JSONValue root(msg);
	return root.stringify();
}

} // End of namespace Inspector
