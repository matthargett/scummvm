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

#include "common/inspector/remoteobject.h"

namespace Inspector {

bool DebugValue::truthy() const {
	switch (type) {
	case kUndefined:
	case kNull:
		return false;
	case kBool:
		return boolVal;
	case kInt:
		return intVal != 0;
	case kDouble:
		return doubleVal != 0.0;
	case kString:
		return !stringVal.empty();
	case kObjectRef:
		return true;
	default:
		return false;
	}
}

RemoteObjectTable::RemoteObjectTable() : _generation(1) {
}

int RemoteObjectTable::createObject(const Common::String &className,
                                    const Common::String &description,
                                    const Common::String &objectGroup) {
	Entry e;
	e.live = true;
	e.className = className;
	e.description = description;
	e.group = objectGroup;
	_entries.push_back(e);
	return (int)_entries.size() - 1;
}

void RemoteObjectTable::addProperty(int ref, const Common::String &name, const DebugValue &value) {
	if (ref < 0 || (uint32)ref >= _entries.size() || !_entries[ref].live)
		return;
	Property p;
	p.name = name;
	p.value = value;
	_entries[ref].properties.push_back(p);
}

Common::String RemoteObjectTable::objectIdFor(int ref) const {
	return Common::String::format("insp:%u:%d", _generation, ref);
}

bool RemoteObjectTable::resolveObjectId(const Common::String &objectId, int &ref) const {
	uint32 gen = 0;
	int r = -1;
	if (sscanf(objectId.c_str(), "insp:%u:%d", &gen, &r) != 2)
		return false;
	if (gen != _generation)
		return false; // stale id from a previous pause
	if (r < 0 || (uint32)r >= _entries.size() || !_entries[r].live)
		return false;
	ref = r;
	return true;
}

Common::JSONValue *RemoteObjectTable::remoteObjectJSON(const DebugValue &v) const {
	Common::JSONObject obj;
	switch (v.type) {
	case DebugValue::kUndefined:
		obj["type"] = new Common::JSONValue("undefined");
		break;
	case DebugValue::kNull:
		obj["type"] = new Common::JSONValue("object");
		obj["subtype"] = new Common::JSONValue("null");
		obj["value"] = new Common::JSONValue();
		break;
	case DebugValue::kBool:
		obj["type"] = new Common::JSONValue("boolean");
		obj["value"] = new Common::JSONValue(v.boolVal);
		break;
	case DebugValue::kInt:
		obj["type"] = new Common::JSONValue("number");
		obj["value"] = new Common::JSONValue((long long int)v.intVal);
		obj["description"] = new Common::JSONValue(Common::String::format("%lld", (long long int)v.intVal));
		break;
	case DebugValue::kDouble:
		obj["type"] = new Common::JSONValue("number");
		obj["value"] = new Common::JSONValue(v.doubleVal);
		break;
	case DebugValue::kString:
		obj["type"] = new Common::JSONValue("string");
		obj["value"] = new Common::JSONValue(v.stringVal);
		break;
	case DebugValue::kObjectRef: {
		obj["type"] = new Common::JSONValue("object");
		bool known = v.objectRef >= 0 && (uint32)v.objectRef < _entries.size() &&
		             _entries[v.objectRef].live;
		if (known) {
			const Entry &e = _entries[v.objectRef];
			obj["className"] = new Common::JSONValue(e.className);
			obj["description"] = new Common::JSONValue(e.description);
			obj["objectId"] = new Common::JSONValue(objectIdFor(v.objectRef));
		} else {
			obj["subtype"] = new Common::JSONValue("null");
			obj["value"] = new Common::JSONValue();
		}
		break;
	}
	default:
		obj["type"] = new Common::JSONValue("undefined");
		break;
	}
	return new Common::JSONValue(obj);
}

Common::JSONValue *RemoteObjectTable::propertiesJSON(int ref) const {
	if (ref < 0 || (uint32)ref >= _entries.size() || !_entries[ref].live)
		return nullptr;
	const Entry &e = _entries[ref];
	Common::JSONArray result;
	for (uint32 i = 0; i < e.properties.size(); i++) {
		Common::JSONObject desc;
		desc["name"] = new Common::JSONValue(e.properties[i].name);
		desc["value"] = remoteObjectJSON(e.properties[i].value);
		desc["writable"] = new Common::JSONValue(true);
		desc["configurable"] = new Common::JSONValue(false);
		desc["enumerable"] = new Common::JSONValue(true);
		desc["isOwn"] = new Common::JSONValue(true);
		result.push_back(new Common::JSONValue(desc));
	}
	return new Common::JSONValue(result);
}

void RemoteObjectTable::releaseGroup(const Common::String &group) {
	if (group.empty())
		return;
	for (uint32 i = 0; i < _entries.size(); i++)
		if (_entries[i].group == group)
			_entries[i].live = false;
}

bool RemoteObjectTable::releaseObject(const Common::String &objectId) {
	int ref;
	if (!resolveObjectId(objectId, ref))
		return false;
	_entries[ref].live = false;
	return true;
}

void RemoteObjectTable::reset() {
	_entries.clear();
	_generation++;
}

} // End of namespace Inspector
