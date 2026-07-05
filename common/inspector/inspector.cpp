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

#include "common/inspector/inspector.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/textconsole.h"

namespace Inspector {

static Session *s_session = nullptr;
static ChannelPump *s_pump = nullptr;

static bool configFlag(const char *key) {
	return ConfMan.hasKey(key) && ConfMan.getBool(key);
}

MessageChannel &transportChannel() {
	static MessageChannel s_channel;
	return s_channel;
}

Session *initSession(Agent *agent) {
	if (!configFlag("inspector_enable"))
		return nullptr;
	if (s_session) {
		warning("Inspector: a session already exists; ignoring second agent");
		return nullptr;
	}

	s_session = new Session(agent);
	s_pump = new ChannelPump(*s_session, transportChannel());
	s_session->setPausePump(s_pump);
	const bool wait = configFlag("inspector_wait");
	if (wait)
		s_session->setWaitForDebugger(true);
	g_session = s_session;

	debug(1, "Inspector: session started for engine '%s'%s",
	      agent->engineId().c_str(), wait ? " (waiting for debugger)" : "");
	return s_session;
}

void shutdownSession() {
	g_session = nullptr;
	delete s_session;
	s_session = nullptr;
	delete s_pump;
	s_pump = nullptr;
	transportChannel().closeConnection();
}

void transportTick() {
	if (s_pump)
		s_pump->transportTick();
}

} // End of namespace Inspector
