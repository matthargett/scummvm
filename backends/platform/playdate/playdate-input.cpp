#include "playdate-input.h"

#include "backends/mutex/null/null-mutex.h"
#include "common/keyboard.h"
#include "common/util.h"
#include "playdate-backend.h"

PlaydateEventSource::PlaydateEventSource(PlaydateAPI *pd) : _pd(pd), _lastButtons((PDButtons)0), _lastCrankAngle(0.0f) {
	if (_pd && _pd->system)
		_lastCrankAngle = _pd->system->getCrankAngle();
}

static Common::Queue<Common::Event> g_inputQueue;
static Common::MutexInternal *g_inputMutex = nullptr;

void PlaydateEventSource::enqueue(const Common::Event &event) {
	if (!g_inputMutex)
		g_inputMutex = new NullMutexInternal();
	Common::StackLock lock(g_inputMutex);
	g_inputQueue.push(event);
}

bool PlaydateEventSource::pollEvent(Common::Event &event) {
	if (!_pendingEvents.empty()) {
		event = _pendingEvents.pop();
		return true;
	}

	if (g_inputMutex) {
		Common::StackLock lock(g_inputMutex);
		if (!g_inputQueue.empty()) {
			event = g_inputQueue.pop();
			return true;
		}
	}

	return false;
}
