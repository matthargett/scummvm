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

#ifndef BACKENDS_NETWORKING_SDL_NET_INSPECTORSERVER_H
#define BACKENDS_NETWORKING_SDL_NET_INSPECTORSERVER_H

#include "common/inspector/discovery.h"
#include "common/inspector/websocket.h"
#include "common/mutex.h"
#include "common/singleton.h"
#include "common/str.h"

typedef struct _SDLNet_SocketSet *SDLNet_SocketSet;
typedef struct _TCPsocket *TCPsocket;

namespace Networking {

/**
 * The socket side of the script inspector: a tiny TCP server (default
 * port 9229) speaking the node-style CDP discovery protocol plus one
 * WebSocket debug connection.
 *
 * Structure mirrors LocalWebserver: a 20 Hz TimerManager proc (which on
 * desktop backends runs on SDL's timer thread) polls an SDL_net socket
 * set under a mutex. This thread ONLY moves bytes: HTTP/WebSocket framing
 * happens right here via the transport-agnostic codecs, but every decoded
 * CDP message is handed to Inspector::transportChannel(), and the engine
 * thread applies it to the session (see common/inspector/channel.h for
 * the threading contract).
 *
 * A small number of simultaneous HTTP connections is allowed because
 * js-debug fetches /json/version and /json/list in parallel while
 * polling; only ONE connection may upgrade to the WebSocket — a second
 * upgrade attempt is refused with 503 (single-session server, DESIGN.md
 * ledger #27).
 */
class InspectorServer : public Common::Singleton<InspectorServer> {
	static const uint32 FRAMES_PER_SECOND = 20;
	static const uint32 TIMER_INTERVAL = 1000000 / FRAMES_PER_SECOND;
	static const uint32 MAX_CONNECTIONS = 4;
	static const uint16 DEFAULT_PORT = 9229;

	friend void inspectorServerTimer(void *); // calls handle()

public:
	InspectorServer();
	~InspectorServer();

	/** Start listening (port from the "inspector_port" config key). */
	void start();
	void stop();
	bool running() const { return _timerStarted; }

private:
	friend class Common::Singleton<SingletonBaseType>;

	struct Connection {
		TCPsocket socket;
		bool upgraded;            ///< true once this is the WebSocket
		bool closing;             ///< close after flushing sendBuffer
		Common::String requestBuffer;
		Common::String sendBuffer;
		Inspector::WebSocketCodec *codec; ///< created on upgrade

		Connection() : socket(nullptr), upgraded(false), closing(false), codec(nullptr) {}
	};

	Common::Mutex _handleMutex;
	SDLNet_SocketSet _set;
	TCPsocket _serverSocket;
	Connection _connections[MAX_CONNECTIONS];
	bool _timerStarted;
	bool _hasWebSocketClient;
	uint16 _port;
	Inspector::DiscoveryHandler *_discovery;

	void startTimer(int interval = TIMER_INTERVAL);
	void stopTimer();
	void handle();
	void acceptConnections();
	void serviceConnection(Connection &conn);
	void handleHttpData(Connection &conn);
	void handleWebSocketData(Connection &conn);
	void pumpOutgoing(Connection &conn);
	void flushSendBuffer(Connection &conn);
	void dropConnection(Connection &conn);
};

} // End of namespace Networking

#endif
