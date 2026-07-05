#include <cxxtest/TestSuite.h>

#include "common/inspector/channel.h"
#include "../../system/null_osystem.h"

/**
 * MessageChannel + ChannelPump: the engine-thread/socket-thread bridge.
 * Ordering (FIFO both ways), connection edge detection (disconnect must
 * run Session::onDisconnect on the engine thread) and pause-pump abort
 * on dead transports. Needs the null OSystem for Common::Mutex.
 */
class InspectorChannelTestSuite : public CxxTest::TestSuite {
#if NULL_OSYSTEM_IS_AVAILABLE
	class NullAgent : public Inspector::Agent {
	public:
		Common::String engineId() const override { return "null"; }
		Common::String targetTitle() const override { return "test"; }
		void buildCallFrames(uint32, Common::Array<Inspector::CallFrameInfo> &) override {}
		void buildScopeObject(uint32, int, int, Inspector::RemoteObjectTable &, int) override {}
	};
#endif

public:
	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
	}

	void tearDown() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	void test_fifo_both_directions() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Inspector::MessageChannel ch;
		ch.pushIncoming("one");
		ch.pushIncoming("two");
		Common::String msg;
		TS_ASSERT(ch.popIncoming(msg));
		TS_ASSERT_EQUALS(msg, "one");
		TS_ASSERT(ch.popIncoming(msg));
		TS_ASSERT_EQUALS(msg, "two");
		TS_ASSERT(!ch.popIncoming(msg));

		ch.pushOutgoing("a");
		ch.pushOutgoing("b");
		TS_ASSERT(ch.popOutgoing(msg));
		TS_ASSERT_EQUALS(msg, "a");
		TS_ASSERT(ch.popOutgoing(msg));
		TS_ASSERT_EQUALS(msg, "b");
		TS_ASSERT(!ch.popOutgoing(msg));
#endif
	}

	// transportTick applies queued client messages to the session and
	// moves the responses back; a connection drop is edge-detected and
	// cleans the session up on the engine thread.
	void test_pump_applies_messages_and_detects_disconnect() {
#if NULL_OSYSTEM_IS_AVAILABLE
		NullAgent agent;
		Inspector::Session session(&agent);
		Inspector::MessageChannel ch;
		Inspector::ChannelPump pump(session, ch);

		ch.markConnected();
		ch.pushIncoming("{\"id\":1,\"method\":\"Debugger.enable\"}");
		pump.transportTick();
		TS_ASSERT(session.debuggerEnabled());
		Common::String msg;
		TS_ASSERT(ch.popOutgoing(msg));
		TS_ASSERT(msg.contains("\"id\":1"));
		TS_ASSERT(!session.hasOutgoing());

		// Socket thread reports the client gone; next engine tick must
		// run the disconnect cleanup (debugger disabled again).
		ch.closeConnection();
		pump.transportTick();
		TS_ASSERT(!session.debuggerEnabled());
#endif
	}

	// A dead channel makes the pause pump give up so the game never
	// hangs waiting on a vanished client.
	void test_pause_pump_aborts_when_disconnected() {
#if NULL_OSYSTEM_IS_AVAILABLE
		NullAgent agent;
		Inspector::Session session(&agent);
		Inspector::MessageChannel ch;
		Inspector::ChannelPump pump(session, ch);
		TS_ASSERT(!pump.pumpWhilePaused()); // never connected
		ch.markConnected();
		TS_ASSERT(pump.pumpWhilePaused());
		ch.closeConnection();
		TS_ASSERT(!pump.pumpWhilePaused());
#endif
	}
};
