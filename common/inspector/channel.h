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

#ifndef COMMON_INSPECTOR_CHANNEL_H
#define COMMON_INSPECTOR_CHANNEL_H

#include "common/scummsys.h"
#include "common/array.h"
#include "common/mutex.h"
#include "common/str.h"
#include "common/inspector/session.h"

namespace Inspector {

/**
 * Thread-safe string-message channel between the socket pump (a timer
 * thread on desktop backends, exactly like LocalWebserver's) and the
 * engine thread that owns the Session.
 *
 * The Session itself is deliberately single-threaded: the transport side
 * only ever touches this channel, and the engine thread applies messages
 * via pumpSession()/ChannelPump. That confinement is what makes
 * evaluate-while-paused re-entrancy safe (ledger #13) without locking
 * any VM state.
 */
class MessageChannel {
public:
	MessageChannel();

	// --- socket-thread side ---
	void pushIncoming(const Common::String &msg);
	bool popOutgoing(Common::String &msg);
	/** Mark the client connection gone (socket closed / protocol failure). */
	void closeConnection();
	void markConnected();

	// --- engine-thread side ---
	bool popIncoming(Common::String &msg);
	void pushOutgoing(const Common::String &msg);
	bool connected() const;

private:
	mutable Common::Mutex _mutex;
	Common::Array<Common::String> _incoming;
	Common::Array<Common::String> _outgoing;
	uint32 _incomingHead;
	uint32 _outgoingHead;
	bool _connected;
};

/**
 * PausePump over a MessageChannel: while the VM is paused, keep applying
 * incoming client messages to the session and flushing its output, with
 * a short OSystem delay per idle tick so the pause does not spin a core.
 */
class ChannelPump : public PausePump {
public:
	ChannelPump(Session &session, MessageChannel &channel);

	bool pumpWhilePaused() override;

	/**
	 * The non-paused variant, to be called from a per-frame engine spot:
	 * applies queued messages and flushes output. Cheap when idle (one
	 * mutex-guarded emptiness check per direction). Also notices client
	 * disconnects and runs Session::onDisconnect() on the engine thread.
	 */
	void transportTick();

private:
	Session &_session;
	MessageChannel &_channel;
	bool _wasConnected;
};

} // End of namespace Inspector

#endif
