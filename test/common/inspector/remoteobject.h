#include <cxxtest/TestSuite.h>

#include "common/inspector/remoteobject.h"

/**
 * RemoteObject/objectId lifetime semantics: ids are scoped to a pause
 * (reset() on resume invalidates them via a generation counter) and to
 * object groups (Runtime.releaseObjectGroup).
 */
class InspectorRemoteObjectTestSuite : public CxxTest::TestSuite {
	typedef Inspector::DebugValue V;
	typedef Inspector::RemoteObjectTable Table;

public:
	void test_primitive_remote_objects() {
		Table t;
		Common::JSONValue *v;

		v = t.remoteObjectJSON(V::undefined());
		TS_ASSERT_EQUALS(v->asObject()["type"]->asString(), "undefined");
		delete v;

		v = t.remoteObjectJSON(V::null());
		TS_ASSERT_EQUALS(v->asObject()["type"]->asString(), "object");
		TS_ASSERT_EQUALS(v->asObject()["subtype"]->asString(), "null");
		delete v;

		v = t.remoteObjectJSON(V::fromBool(true));
		TS_ASSERT_EQUALS(v->asObject()["type"]->asString(), "boolean");
		TS_ASSERT(v->asObject()["value"]->asBool());
		delete v;

		v = t.remoteObjectJSON(V::fromInt(-42));
		TS_ASSERT_EQUALS(v->asObject()["type"]->asString(), "number");
		TS_ASSERT_EQUALS(v->asObject()["value"]->asIntegerNumber(), -42);
		delete v;

		v = t.remoteObjectJSON(V::fromString("hello"));
		TS_ASSERT_EQUALS(v->asObject()["type"]->asString(), "string");
		TS_ASSERT_EQUALS(v->asObject()["value"]->asString(), "hello");
		delete v;
	}

	void test_object_lifecycle_and_properties() {
		Table t;
		int obj = t.createObject("Scope", "Local (room11 entry)");
		t.addProperty(obj, "V13", V::fromInt(5));
		t.addProperty(obj, "actorName", V::fromString("Guybrush"));

		Common::JSONValue *ro = t.remoteObjectJSON(V::fromObject(obj));
		const Common::JSONObject &o = ro->asObject();
		TS_ASSERT_EQUALS(o["type"]->asString(), "object");
		TS_ASSERT_EQUALS(o["className"]->asString(), "Scope");
		TS_ASSERT(o.contains("objectId"));
		Common::String objectId = o["objectId"]->asString();
		delete ro;

		int resolved = -1;
		TS_ASSERT(t.resolveObjectId(objectId, resolved));
		TS_ASSERT_EQUALS(resolved, obj);

		Common::JSONValue *props = t.propertiesJSON(obj);
		TS_ASSERT(props);
		if (!props)
			return;
		const Common::JSONArray &arr = props->asArray();
		TS_ASSERT_EQUALS(arr.size(), 2u);
		TS_ASSERT_EQUALS(arr[0]->asObject()["name"]->asString(), "V13");
		TS_ASSERT_EQUALS(arr[0]->asObject()["value"]->asObject()["value"]->asIntegerNumber(), 5);
		TS_ASSERT_EQUALS(arr[1]->asObject()["name"]->asString(), "actorName");
		TS_ASSERT(arr[0]->asObject()["enumerable"]->asBool());
		delete props;
	}

	// Stale ids from a previous pause must fail to resolve, not alias a
	// fresh object with the same slot (generation counter).
	void test_reset_invalidates_object_ids() {
		Table t;
		int obj = t.createObject("Scope", "s");
		Common::String oldId = t.objectIdFor(obj);
		t.reset(); // execution resumed
		int dummy;
		TS_ASSERT(!t.resolveObjectId(oldId, dummy));
		// A new object may reuse slot 0 but gets a different id string.
		int obj2 = t.createObject("Scope", "s2");
		TS_ASSERT_EQUALS(obj2, 0);
		TS_ASSERT(t.objectIdFor(obj2) != oldId);
		TS_ASSERT(t.resolveObjectId(t.objectIdFor(obj2), dummy));
	}

	void test_release_group_and_object() {
		Table t;
		int a = t.createObject("Result", "a", "watch-group");
		int b = t.createObject("Result", "b", "watch-group");
		int c = t.createObject("Result", "c", "console");
		t.releaseGroup("watch-group");
		int dummy;
		TS_ASSERT(!t.resolveObjectId(t.objectIdFor(a), dummy));
		TS_ASSERT(!t.resolveObjectId(t.objectIdFor(b), dummy));
		TS_ASSERT(t.resolveObjectId(t.objectIdFor(c), dummy));
		TS_ASSERT(t.releaseObject(t.objectIdFor(c)));
		TS_ASSERT(!t.resolveObjectId(t.objectIdFor(c), dummy));
		// Releasing twice reports failure but does not crash.
		TS_ASSERT(!t.releaseObject(t.objectIdFor(c)));
	}

	void test_malformed_object_ids() {
		Table t;
		int dummy;
		TS_ASSERT(!t.resolveObjectId("", dummy));
		TS_ASSERT(!t.resolveObjectId("garbage", dummy));
		TS_ASSERT(!t.resolveObjectId("insp:1:99", dummy));
		TS_ASSERT(!t.resolveObjectId("insp:999:0", dummy));
		TS_ASSERT(t.propertiesJSON(42) == nullptr);
	}

	void test_truthiness_for_conditions() {
		TS_ASSERT(!V::undefined().truthy());
		TS_ASSERT(!V::null().truthy());
		TS_ASSERT(!V::fromBool(false).truthy());
		TS_ASSERT(V::fromBool(true).truthy());
		TS_ASSERT(!V::fromInt(0).truthy());
		TS_ASSERT(V::fromInt(-1).truthy());
		TS_ASSERT(!V::fromString("").truthy());
		TS_ASSERT(V::fromString("0").truthy()); // JS semantics: non-empty string
	}

	// Nested aggregates: a scope containing an inventory object.
	void test_nested_objects() {
		Table t;
		int inner = t.createObject("GameObject", "object 451");
		t.addProperty(inner, "x", V::fromInt(160));
		int outer = t.createObject("Scope", "Local");
		t.addProperty(outer, "heldObject", V::fromObject(inner));

		Common::JSONValue *props = t.propertiesJSON(outer);
		TS_ASSERT(props);
		if (!props)
			return;
		const Common::JSONObject &held = props->asArray()[0]->asObject()["value"]->asObject();
		TS_ASSERT_EQUALS(held["className"]->asString(), "GameObject");
		int innerRef = -1;
		TS_ASSERT(t.resolveObjectId(held["objectId"]->asString(), innerRef));
		TS_ASSERT_EQUALS(innerRef, inner);
		delete props;
	}
};
