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

#include <SDL_net.h>

#include "backends/networking/sdl_net/inspectorserver.h"
#include "base/version.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/inspector/inspector.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/timer.h"

namespace Common {
DECLARE_SINGLETON(Networking::InspectorServer);
}

namespace Networking {

InspectorServer::InspectorServer() :
	_set(nullptr),
	_serverSocket(nullptr),
	_timerStarted(false),
	_hasWebSocketClient(false),
	_port(DEFAULT_PORT),
	_discovery(nullptr) {
}

InspectorServer::~InspectorServer() {
	stop();
	delete _discovery;
}

void inspectorServerTimer(void *ignored) {
	InspectorServer::instance().handle();
}

void InspectorServer::startTimer(int interval) {
	Common::TimerManager *manager = g_system->getTimerManager();
	if (manager->installTimerProc(inspectorServerTimer, interval, nullptr,
	                              "Networking::InspectorServer's Timer")) {
		_timerStarted = true;
	} else {
		warning("Failed to install Networking::InspectorServer's timer");
	}
}

void InspectorServer::stopTimer() {
	if (!_timerStarted)
		return;
	g_system->getTimerManager()->removeTimerProc(inspectorServerTimer);
	_timerStarted = false;
}

void InspectorServer::start() {
	Common::StackLock lock(_handleMutex);
	if (_timerStarted)
		return;

	_port = DEFAULT_PORT;
	if (ConfMan.hasKey("inspector_port"))
		_port = (uint16)ConfMan.getInt("inspector_port");

	if (!_discovery) {
		// A per-run pseudo-UUID: clients treat the id as opaque and use
		// the advertised webSocketDebuggerUrl verbatim.
		uint32 millis = g_system->getMillis();
		Common::String uuid = Common::String::format(
			"scummvm-%08x-%04x-0000-0000-%08x", millis, (uint16)(millis >> 8), _port);
		Common::String title = "ScummVM";
		if (ConfMan.getActiveDomain())
			title = ConfMan.getActiveDomainName() + " (ScummVM)";
		_discovery = new Inspector::DiscoveryHandler(_port, uuid, title, gScummVMVersion);
	}

	IPaddress ip;
	if (SDLNet_ResolveHost(&ip, nullptr, _port) == -1) {
		warning("InspectorServer: SDLNet_ResolveHost: %s", SDLNet_GetError());
		return;
	}
	_serverSocket = SDLNet_TCP_Open(&ip);
	if (!_serverSocket) {
		warning("InspectorServer: SDLNet_TCP_Open: %s (port %u in use?)",
		        SDLNet_GetError(), _port);
		return;
	}
	_set = SDLNet_AllocSocketSet(MAX_CONNECTIONS + 1);
	if (!_set) {
		warning("InspectorServer: SDLNet_AllocSocketSet: %s", SDLNet_GetError());
		SDLNet_TCP_Close(_serverSocket);
		_serverSocket = nullptr;
		return;
	}
	SDLNet_TCP_AddSocket(_set, _serverSocket);

	startTimer();
	debug(1, "InspectorServer: listening on 127.0.0.1:%u (CDP)", _port);
}

void InspectorServer::stop() {
	Common::StackLock lock(_handleMutex);
	stopTimer();
	for (uint32 i = 0; i < MAX_CONNECTIONS; i++)
		if (_connections[i].socket)
			dropConnection(_connections[i]);
	if (_serverSocket) {
		SDLNet_TCP_Close(_serverSocket);
		_serverSocket = nullptr;
	}
	if (_set) {
		SDLNet_FreeSocketSet(_set);
		_set = nullptr;
	}
}

void InspectorServer::handle() {
	Common::StackLock lock(_handleMutex);
	if (!_serverSocket)
		return;
	// Non-blocking poll of every socket in the set.
	SDLNet_CheckSockets(_set, 0);
	acceptConnections();
	for (uint32 i = 0; i < MAX_CONNECTIONS; i++)
		if (_connections[i].socket)
			serviceConnection(_connections[i]);
}

void InspectorServer::acceptConnections() {
	if (!SDLNet_SocketReady(_serverSocket))
		return;
	TCPsocket client = SDLNet_TCP_Accept(_serverSocket);
	if (!client)
		return;
	for (uint32 i = 0; i < MAX_CONNECTIONS; i++) {
		if (!_connections[i].socket) {
			_connections[i] = Connection();
			_connections[i].socket = client;
			SDLNet_TCP_AddSocket(_set, client);
			return;
		}
	}
	// All slots busy: close immediately (the client will retry; js-debug
	// polls discovery every 200 ms).
	SDLNet_TCP_Close(client);
}

void InspectorServer::serviceConnection(Connection &conn) {
	if (SDLNet_SocketReady(conn.socket)) {
		byte buffer[16 * 1024];
		int received = SDLNet_TCP_Recv(conn.socket, buffer, sizeof(buffer));
		if (received <= 0) {
			dropConnection(conn);
			return;
		}
		if (conn.upgraded) {
			conn.codec->addData(buffer, received);
			handleWebSocketData(conn);
		} else {
			conn.requestBuffer += Common::String((const char *)buffer, received);
			handleHttpData(conn);
		}
		if (!conn.socket)
			return; // dropped during processing
	}
	if (conn.upgraded)
		pumpOutgoing(conn);
	flushSendBuffer(conn);
	if (conn.closing && conn.sendBuffer.empty())
		dropConnection(conn);
}

void InspectorServer::handleHttpData(Connection &conn) {
	if (!Inspector::HttpRequest::isComplete(conn.requestBuffer))
		return; // keep reading
	Inspector::HttpRequest request;
	if (!request.parse(conn.requestBuffer)) {
		conn.sendBuffer += "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
		conn.closing = true;
		return;
	}
	// Only one live debug socket: refuse a second upgrade cleanly
	// instead of letting two clients fight over pause state.
	if (_hasWebSocketClient && request.path == "/" + _discovery->uuid()) {
		conn.sendBuffer += "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
		conn.closing = true;
		return;
	}
	Inspector::DiscoveryHandler::Response response = _discovery->handleRequest(request);
	conn.sendBuffer += response.raw;
	if (response.upgraded) {
		conn.upgraded = true;
		conn.codec = new Inspector::WebSocketCodec();
		conn.requestBuffer.clear();
		_hasWebSocketClient = true;
		Inspector::transportChannel().markConnected();
		debug(1, "InspectorServer: debugger attached");
	} else {
		conn.closing = true;
	}
}

void InspectorServer::handleWebSocketData(Connection &conn) {
	typedef Inspector::WebSocketCodec::Message Message;
	Message message;
	while (conn.codec->nextMessage(message)) {
		switch (message.type) {
		case Message::kTypeText:
			Inspector::transportChannel().pushIncoming(message.payload);
			break;
		case Message::kTypePing:
			conn.sendBuffer += Inspector::WebSocketCodec::encodePong(message.payload);
			break;
		case Message::kTypeClose:
			conn.sendBuffer += Inspector::WebSocketCodec::encodeClose(message.closeStatus);
			conn.closing = true;
			return;
		default:
			break; // pongs and binary frames are ignored
		}
	}
	if (conn.codec->failed()) {
		conn.sendBuffer += Inspector::WebSocketCodec::encodeClose(conn.codec->failStatus());
		conn.closing = true;
	}
}

void InspectorServer::pumpOutgoing(Connection &conn) {
	Common::String msg;
	while (Inspector::transportChannel().popOutgoing(msg))
		conn.sendBuffer += Inspector::WebSocketCodec::encodeText(msg);
}

void InspectorServer::flushSendBuffer(Connection &conn) {
	if (conn.sendBuffer.empty() || !conn.socket)
		return;
	// SDLNet_TCP_Send blocks until the whole buffer is queued; CDP
	// replies are small compared to the 20 Hz tick budget.
	int sent = SDLNet_TCP_Send(conn.socket, conn.sendBuffer.c_str(), conn.sendBuffer.size());
	if (sent < (int)conn.sendBuffer.size()) {
		dropConnection(conn);
		return;
	}
	conn.sendBuffer.clear();
}

void InspectorServer::dropConnection(Connection &conn) {
	if (conn.upgraded) {
		_hasWebSocketClient = false;
		// The engine thread notices and runs Session::onDisconnect().
		Inspector::transportChannel().closeConnection();
		debug(1, "InspectorServer: debugger detached");
	}
	if (conn.socket) {
		SDLNet_TCP_DelSocket(_set, conn.socket);
		SDLNet_TCP_Close(conn.socket);
	}
	delete conn.codec;
	conn = Connection();
}

} // End of namespace Networking
