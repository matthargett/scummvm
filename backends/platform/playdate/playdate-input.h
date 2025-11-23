#ifndef PLATFORM_PLAYDATE_INPUT_H
#define PLATFORM_PLAYDATE_INPUT_H

#include "common/events.h"
#include "common/queue.h"
#include "pd_api.h"

class PlaydateEventSource : public Common::EventSource {
public:
	PlaydateEventSource(PlaydateAPI *pd);
	bool pollEvent(Common::Event &event) override;

	static void enqueue(const Common::Event &event);

private:
	PlaydateAPI *_pd;
	Common::Queue<Common::Event> _pendingEvents;
	PDButtons _lastButtons;
	float _lastCrankAngle;
};

#endif // PLATFORM_PLAYDATE_INPUT_H
