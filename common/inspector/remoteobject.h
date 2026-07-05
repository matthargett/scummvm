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

#ifndef COMMON_INSPECTOR_REMOTEOBJECT_H
#define COMMON_INSPECTOR_REMOTEOBJECT_H

#include "common/scummsys.h"
#include "common/array.h"
#include "common/formats/json.h"
#include "common/str.h"

namespace Inspector {

/**
 * The value model shared between engine adapters and the CDP layer.
 * Game-script VMs deal in small scalars (bytes, int16/32 registers,
 * flags, strings) plus a few shallow aggregates (variable banks, object
 * property lists), so a tagged union with table-backed aggregates covers
 * everything without dragging a JS object model into the engines.
 */
struct DebugValue {
	enum Type {
		kUndefined,
		kNull,
		kBool,
		kInt,
		kDouble,
		kString,
		kObjectRef ///< aggregate materialized in a RemoteObjectTable
	};

	Type type;
	bool boolVal;
	int64 intVal;
	double doubleVal;
	Common::String stringVal;
	int objectRef;

	DebugValue() : type(kUndefined), boolVal(false), intVal(0), doubleVal(0), objectRef(-1) {}

	static DebugValue undefined() { return DebugValue(); }
	static DebugValue null() {
		DebugValue v;
		v.type = kNull;
		return v;
	}
	static DebugValue fromBool(bool b) {
		DebugValue v;
		v.type = kBool;
		v.boolVal = b;
		return v;
	}
	static DebugValue fromInt(int64 i) {
		DebugValue v;
		v.type = kInt;
		v.intVal = i;
		return v;
	}
	static DebugValue fromDouble(double d) {
		DebugValue v;
		v.type = kDouble;
		v.doubleVal = d;
		return v;
	}
	static DebugValue fromString(const Common::String &s) {
		DebugValue v;
		v.type = kString;
		v.stringVal = s;
		return v;
	}
	static DebugValue fromObject(int ref) {
		DebugValue v;
		v.type = kObjectRef;
		v.objectRef = ref;
		return v;
	}

	/** ECMAScript-style truthiness, used for breakpoint conditions. */
	bool truthy() const;
};

/**
 * Table of expandable objects surfaced to the client as CDP RemoteObjects.
 *
 * objectId lifetime follows the V8 embedder convention: everything is
 * scoped to the current pause. reset() — called on resume/step — bumps a
 * generation counter baked into every objectId, so stale ids from a
 * previous pause fail to resolve instead of aliasing new objects.
 * Runtime.releaseObjectGroup releases per-group (evaluation results).
 */
class RemoteObjectTable {
public:
	RemoteObjectTable();

	/** Materialize a new (empty) aggregate. */
	int createObject(const Common::String &className,
	                 const Common::String &description,
	                 const Common::String &objectGroup = Common::String());

	void addProperty(int ref, const Common::String &name, const DebugValue &value);

	/** Serialize any DebugValue as a CDP RemoteObject (caller owns result). */
	Common::JSONValue *remoteObjectJSON(const DebugValue &v) const;

	/** PropertyDescriptor array for Runtime.getProperties (caller owns result).
	 *  Returns nullptr for unknown/stale refs. */
	Common::JSONValue *propertiesJSON(int ref) const;

	Common::String objectIdFor(int ref) const;
	/** Resolve an objectId string; false for malformed/stale/released ids. */
	bool resolveObjectId(const Common::String &objectId, int &ref) const;

	void releaseGroup(const Common::String &group);
	bool releaseObject(const Common::String &objectId);

	/** Invalidate every outstanding objectId (called when execution resumes). */
	void reset();

	uint32 generation() const { return _generation; }

private:
	struct Property {
		Common::String name;
		DebugValue value;
	};
	struct Entry {
		bool live;
		Common::String className;
		Common::String description;
		Common::String group;
		Common::Array<Property> properties;

		Entry() : live(false) {}
	};

	Common::Array<Entry> _entries;
	uint32 _generation;
};

} // End of namespace Inspector

#endif
