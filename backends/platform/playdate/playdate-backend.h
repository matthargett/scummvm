#ifndef BACKENDS_PLATFORM_PLAYDATE_BACKEND_H
#define BACKENDS_PLATFORM_PLAYDATE_BACKEND_H

extern "C" {
#include "pd_api.h"
}
#include "backends/modular-backend.h"
#include "backends/base-backend.h"
#include "common/scummsys.h"
#include "common/system.h"

// Expose backbuffer helpers for the Playdate main thread blitter
const Common::Array<uint8_t> &playdateBackBuffer();
bool playdateConsumeDirtyFlag();
int playdateBackBufferWidth();
int playdateBackBufferHeight();
int playdateReservedRight();
int playdateReservedBottom();

class PlaydateEventSource;

class OSystem_Playdate : public ModularMixerBackend {
public:
	OSystem_Playdate(PlaydateAPI *pd);
	virtual ~OSystem_Playdate();

	virtual void initBackend();
	virtual void engineInit();
	virtual void engineDone();
	virtual bool hasFeature(OSystem::Feature f);
	virtual void setFeatureState(OSystem::Feature f, bool enable);
	virtual bool getFeatureState(OSystem::Feature f);
	virtual const OSystem::GraphicsMode *getSupportedGraphicsModes() const;
	virtual int getDefaultGraphicsMode() const;
	virtual bool setGraphicsMode(int mode, uint flags);
	virtual int getGraphicsMode() const;
	virtual float getHiDPIScreenFactor() const { return 1.0f; }
	virtual void getDefaultResolution(uint &w, uint &h);
	virtual void initSize(uint w, uint h, const Graphics::PixelFormat *format = NULL);
	virtual int16 getWidth();
	virtual int16 getHeight();
	virtual Graphics::PixelFormat getScreenFormat() const;
	virtual Common::List<Graphics::PixelFormat> getSupportedFormats() const;
	virtual void setPalette(const byte *colors, uint start, uint num);
	virtual void grabPalette(byte *colors, uint start, uint num) const;
	virtual void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h);
	virtual void updateScreen();
	virtual Graphics::Surface *lockScreen();
	virtual void unlockScreen();
	virtual void setShakePos(int shakeXOffset, int shakeYOffset);
	virtual void showOverlay(bool inGUI);
	virtual void hideOverlay();
	virtual bool isOverlayVisible() const;
	virtual void clearOverlay();
	virtual void grabOverlay(Graphics::Surface &surface);
	virtual void copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h);
	virtual int16 getOverlayHeight() const;
	virtual int16 getOverlayWidth() const;
	virtual Graphics::PixelFormat getOverlayFormat() const;
	virtual bool showMouse(bool visible);
	virtual void warpMouse(int x, int y);
	virtual void setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor, bool dontScale, const Graphics::PixelFormat *format, const byte *mask);
	virtual void setCursorPalette(const byte *colors, uint start, uint num);
	virtual uint32 getMillis(bool skipRecord = false);
	virtual void delayMillis(uint msecs);
	virtual void getTimeAndDate(TimeDate &td, bool skipRecord = false) const;
	virtual Common::MutexInternal *createMutex();
	virtual void quit();
	virtual Common::EventSource *getDefaultEventSource();
	virtual FilesystemFactory *getFilesystemFactory();
	virtual PaletteManager *getPaletteManager() { return _paletteManager; }
	virtual void logMessage(LogMessageType::Type type, const char *message);

	void handleEvent(PDSystemEvent event, uint32_t arg);

private:
	PlaydateAPI *_pd;
	uint32 _startTime;
	uint16 _screenWidth = 0;
	uint16 _screenHeight = 0;
	PlaydateEventSource *_eventSource = nullptr;
	PaletteManager *_paletteManager = nullptr;
};

#endif
