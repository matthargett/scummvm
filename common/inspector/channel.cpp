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

#include "common/inspector/channel.h"
#include "common/system.h"

namespace Inspector {

MessageChannel::MessageChannel() :
	_incomingHead(0), _outgoingHead(0), _connected(false) {
}

void MessageChannel::pushIncoming(const Common::String &msg) {
	Common::StackLock lock(_mutex);
	_incoming.push_back(msg);
}

bool MessageChannel::popIncoming(Common::String &msg) {
	Common::StackLock lock(_mutex);
	if (_incomingHead >= _incoming.size()) {
		_incoming.clear();
		_incomingHead = 0;
		return false;
	}
	msg = _incoming[_incomingHead++];
	return true;
}

void MessageChannel::pushOutgoing(const Common::String &msg) {
	Common::StackLock lock(_mutex);
	_outgoing.push_back(msg);
}

bool MessageChannel::popOutgoing(Common::String &msg) {
	Common::StackLock lock(_mutex);
	if (_outgoingHead >= _outgoing.size()) {
		_outgoing.clear();
		_outgoingHead = 0;
		return false;
	}
	msg = _outgoing[_outgoingHead++];
	return true;
}

void MessageChannel::closeConnection() {
	Common::StackLock lock(_mutex);
	_connected = false;
}

void MessageChannel::markConnected() {
	Common::StackLock lock(_mutex);
	_connected = true;
}

bool MessageChannel::connected() const {
	Common::StackLock lock(_mutex);
	return _connected;
}

ChannelPump::ChannelPump(Session &session, MessageChannel &channel) :
	_session(session), _channel(channel), _wasConnected(false) {
}

void ChannelPump::transportTick() {
	const bool connected = _channel.connected();
	if (_wasConnected && !connected) {
		// The socket thread saw the client go away; apply the session
		// cleanup here, on the engine thread (resume, drop breakpoints).
		_session.onDisconnect();
	}
	_wasConnected = connected;

	Common::String msg;
	while (_channel.popIncoming(msg))
		_session.onMessage(msg);
	while (_session.nextOutgoing(msg))
		_channel.pushOutgoing(msg);
}

bool ChannelPump::pumpWhilePaused() {
	if (!_channel.connected()) {
		// Client gone: tell the session to abandon the pause.
		return false;
	}
	Common::String msg;
	bool didWork = false;
	while (_channel.popIncoming(msg)) {
		didWork = true;
		_session.onMessage(msg);
	}
	while (_session.nextOutgoing(msg)) {
		didWork = true;
		_channel.pushOutgoing(msg);
	}
	if (!didWork && g_system) {
		// Idle: yield so a paused game does not spin a core. 5 ms keeps
		// evaluate round-trips snappy at the 20 Hz socket pump cadence.
		g_system->delayMillis(5);
	}
	return true;
}

} // End of namespace Inspector
