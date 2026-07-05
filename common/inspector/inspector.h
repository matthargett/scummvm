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

#ifndef COMMON_INSPECTOR_INSPECTOR_H
#define COMMON_INSPECTOR_INSPECTOR_H

#include "common/inspector/agent.h"
#include "common/inspector/channel.h"
#include "common/inspector/session.h"

namespace Inspector {

/**
 * Engine-side bootstrap. An engine adapter calls initSession() with its
 * Agent during engine initialization:
 *
 *   _inspector = new MyEngineInspectorAgent(this);
 *   Inspector::initSession(_inspector);
 *
 * The session is only created when the user asked for it (config key
 * "inspector_enable", plus "inspector_wait" for the --inspect-brk-style
 * hold at the first instruction and "inspector_port" for discovery).
 * When disabled, initSession() returns nullptr, Inspector::g_session
 * stays null and every VM hook stays a single predictable branch.
 *
 * The socket transport (backends/networking/sdl_net, USE_SDL_NET) is
 * independent: it moves WebSocket messages in and out of
 * transportChannel() on a timer thread; the session applies them on the
 * engine thread via transportTick() and the pause pump.
 *
 * @return the created session, or nullptr when the inspector is disabled
 */
Session *initSession(Agent *agent);

/** Tear down the session created by initSession() (engine shutdown).
 *  The agent passed to initSession() is NOT deleted (the adapter owns it). */
void shutdownSession();

/** The shared transport channel between the socket server and session. */
MessageChannel &transportChannel();

/**
 * Engine per-frame transport pump: applies queued client messages to the
 * session and flushes queued output. Engine adapters call this from a
 * once-per-frame spot (the same cadence GUI::Debugger::onFrame runs at).
 * Cheap no-op when no inspector session exists.
 */
void transportTick();

} // End of namespace Inspector

#endif
