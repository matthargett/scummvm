#define FORBIDDEN_SYMBOL_ALLOW_ALL
#define FORBIDDEN_SYMBOL_EXCEPTION_time_h
#define FORBIDDEN_SYMBOL_EXCEPTION_wchar_h
#define FORBIDDEN_SYMBOL_EXCEPTION_stdio_h
#include "playdate-backend.h"
#include "backends/base-backend.h"
#include "backends/events/default/default-events.h"
#include "backends/timer/default/default-timer.h"
#include "backends/graphics/default-palette.h"
#include "backends/mixer/mixer.h"
#include "backends/mutex/null/null-mutex.h"
#include "backends/saves/default/default-saves.h"
#include "common/array.h"
#include "common/mutex.h"
#include <thread>
#include <chrono>
#include "common/array.h"
#include "common/config-manager.h"
#include "common/textconsole.h"
#include <cstring>
#include <string>
#include "pd_api.h"
#include "playdate-fs.h"
#include "playdate-input.h"

#ifdef mkdir
#undef mkdir
#endif

// Provided by playdate-main.cpp
extern std::string g_playdateGamePath;

static Common::Array<uint8_t> g_playdateBackBuffer;
static bool g_backBufferDirty = false;
static int g_backBufferW = 0;
static int g_backBufferH = 0;
static int g_reservedRight = 0;  // No longer reserving space for word list panel
static int g_reservedBottom = 0;

const Common::Array<uint8_t> &playdateBackBuffer() { return g_playdateBackBuffer; }
bool playdateConsumeDirtyFlag() {
	bool wasDirty = g_backBufferDirty;
	g_backBufferDirty = false;
	return wasDirty;
}
int playdateBackBufferWidth() { return g_backBufferW; }
int playdateBackBufferHeight() { return g_backBufferH; }
int playdateReservedRight() { return g_reservedRight; }
int playdateReservedBottom() { return g_reservedBottom; }

namespace {

class PlaydatePaletteManager : public DefaultPaletteManager {
protected:
	void setPaletteIntern(const byte *colors, uint start, uint num) override {
		// Monochrome display; we just store palette values in base class.
		(void)colors;
		(void)start;
		(void)num;
	}
};

class PlaydateMixerManager : public MixerManager {
public:
	PlaydateMixerManager(PlaydateAPI *pd) : _pd(pd), _source(nullptr), _samples(0) {}

	~PlaydateMixerManager() override {
		if (_pd && _pd->sound && _source)
			_pd->sound->removeSource(_source);
	}

	void init() override {
		_samples = AUDIO_FRAMES_PER_CYCLE;
		_buffer.resize(_samples * 2);

		_mixer = new Audio::MixerImpl(44100, true, _samples);
		if (_mixer)
			_mixer->setReady(true);

		if (_pd && _pd->sound)
			_source = _pd->sound->addSource(&PlaydateMixerManager::audioCallback, this, 1);
	}

	void suspendAudio() override { _audioSuspended = true; }

	int resumeAudio() override {
		_audioSuspended = false;
		return 0;
	}

private:
	static int audioCallback(void *ctx, int16_t *left, int16_t *right, int len) {
		PlaydateMixerManager *self = static_cast<PlaydateMixerManager *>(ctx);
		if (!self || !self->_mixer || self->_audioSuspended) {
			memset(left, 0, sizeof(int16_t) * len);
			memset(right, 0, sizeof(int16_t) * len);
			return 0;
		}

		const int needed = len * 2;
	if ((int)self->_buffer.size() < needed)
		self->_buffer.resize(needed);

	self->_mixer->mixCallback((byte *)self->_buffer.data(), len * 2 * sizeof(int16));
	for (int i = 0; i < len; ++i) {
		int32 sampleL = self->_buffer[i * 2];
		int32 sampleR = self->_buffer[i * 2 + 1];
		// Clamp to int16
		left[i] = (int16)CLIP<int32>(sampleL, -32768, 32767);
		right[i] = (int16)CLIP<int32>(sampleR, -32768, 32767);
	}

	return 1;
}

	PlaydateAPI *_pd;
	SoundSource *_source;
	Common::Array<int16> _buffer;
	int _samples;
};

} // namespace

OSystem_Playdate::OSystem_Playdate(PlaydateAPI *pd) : _pd(pd), _startTime(0) {
	if (g_playdateBackBuffer.empty()) {
		g_backBufferW = 320;
		g_backBufferH = 200;
		g_playdateBackBuffer.resize(g_backBufferW * g_backBufferH);
		std::fill(g_playdateBackBuffer.begin(), g_playdateBackBuffer.end(), 0);
	}
}

OSystem_Playdate::~OSystem_Playdate() {
	delete _eventSource;
	_eventSource = nullptr;
	delete _paletteManager;
	_paletteManager = nullptr;
}

void OSystem_Playdate::initBackend() {
	const Common::String gamePath = g_playdateGamePath.empty() ? "/games" : g_playdateGamePath.c_str();
	ConfMan.registerDefault("browser_lastpath", gamePath);
	ConfMan.registerDefault("path", gamePath);
	ConfMan.registerDefault("render_mode", "playdate");
	ConfMan.registerDefault("gui_disabled", "true");
	ConfMan.registerDefault("savepath", "/data");

	if (!_eventSource)
		_eventSource = new PlaydateEventSource(_pd);
	if (!_eventManager)
		_eventManager = new DefaultEventManager(_eventSource);

	if (!_fsFactory)
		_fsFactory = new PlaydateFilesystemFactory(_pd);
	if (!_savefileManager)
		_savefileManager = new DefaultSaveFileManager();

	if (!_timerManager)
		_timerManager = new DefaultTimerManager();

	if (!_mixerManager)
		_mixerManager = new PlaydateMixerManager(_pd);
	if (_mixerManager)
		_mixerManager->init();

	if (!_paletteManager)
		_paletteManager = new PlaydatePaletteManager();

	if (!g_playdateGamePath.empty())
		ConfMan.set("path", g_playdateGamePath.c_str(), Common::ConfigManager::kTransientDomain);

	// Ensure save path exists
	if (_pd && _pd->file) {
		const char *saveDir = ConfMan.get("savepath").c_str();
		_pd->file->mkdir(saveDir);
	}

	if (_pd && _pd->system)
		_pd->system->logToConsole("Configured AGI path: %s", ConfMan.get("path").c_str());

	BaseBackend::initBackend();
	_startTime = _pd->system->getCurrentTimeMilliseconds();
}

void OSystem_Playdate::engineInit() {
}

void OSystem_Playdate::engineDone() {
}

bool OSystem_Playdate::hasFeature(OSystem::Feature f) {
	return false;
}

void OSystem_Playdate::setFeatureState(OSystem::Feature f, bool enable) {
}

bool OSystem_Playdate::getFeatureState(OSystem::Feature f) {
	return false;
}

const OSystem::GraphicsMode *OSystem_Playdate::getSupportedGraphicsModes() const {
	static const OSystem::GraphicsMode modes[] = {
		{"default", "Default", 0},
		{0, 0, 0}};
	return modes;
}

int OSystem_Playdate::getDefaultGraphicsMode() const {
	return 0;
}

bool OSystem_Playdate::setGraphicsMode(int mode, uint flags) {
	return true;
}

int OSystem_Playdate::getGraphicsMode() const {
	return 0;
}

void OSystem_Playdate::initSize(uint width, uint height, const Graphics::PixelFormat *format) {
	_screenWidth = width;
	_screenHeight = height;
	g_backBufferW = width;
	g_backBufferH = height;
	g_playdateBackBuffer.clear();
	g_playdateBackBuffer.resize(g_backBufferW * g_backBufferH);
	std::fill(g_playdateBackBuffer.begin(), g_playdateBackBuffer.end(), 0);

	// Overlay matches game area
	_overlaySurface.free();
	_overlaySurface.create(width, height, Graphics::PixelFormat::createFormatCLUT8());
	_overlayVisible = false;
}

int16 OSystem_Playdate::getHeight() {
	return _screenHeight ? _screenHeight : 240;
}

int16 OSystem_Playdate::getWidth() {
	return _screenWidth ? _screenWidth : 400;
}

void OSystem_Playdate::setPalette(const byte *colors, uint start, uint num) {
}

void OSystem_Playdate::grabPalette(byte *colors, uint start, uint num) const {
}

void OSystem_Playdate::copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) {
	// Draw into a shared backbuffer; the Playdate main thread will blit it.
	uint8_t *frameBuffer = g_playdateBackBuffer.data();
	const byte *src = (const byte *)buf;

	const int destStride = g_backBufferW ? g_backBufferW : 320; // 1 byte per pixel backbuffer

	for (int j = 0; j < h; j++) {
		int dstY = y + j;
		if (dstY >= g_backBufferH)
			break;

		const byte *srcRow = src + j * pitch;
		uint8_t *dstRow = frameBuffer + dstY * destStride;

		// Clear the destination span so moving sprites/dithers can erase correctly
		if (x < g_backBufferW)
			memset(dstRow + x, 0, MIN<int>(w, g_backBufferW - x));

		for (int i = 0; i < w; i++) {
			int dstX = x + i;
			if (dstX >= g_backBufferW)
				break;
			dstRow[dstX] = srcRow[i] ? 1 : 0;
		}
	}
	g_backBufferDirty = true;
}

void OSystem_Playdate::updateScreen() {
	// Rendering is pulled from the Playdate main thread; nothing to do here.
}

Graphics::Surface *OSystem_Playdate::lockScreen() {
	return nullptr;
}

void OSystem_Playdate::unlockScreen() {
}

void OSystem_Playdate::setShakePos(int shakeXOffset, int shakeYOffset) {
}

void OSystem_Playdate::showOverlay(bool inGUI) {
	_overlayVisible = true;
}

void OSystem_Playdate::hideOverlay() {
	_overlayVisible = false;
}

bool OSystem_Playdate::isOverlayVisible() const {
	return _overlayVisible;
}

void OSystem_Playdate::clearOverlay() {
	if (!_overlaySurface.getPixels())
		return;
	_overlaySurface.fillRect(Common::Rect(_overlaySurface.w, _overlaySurface.h), 0);
}

void OSystem_Playdate::grabOverlay(Graphics::Surface &surface) {
	if (!_overlaySurface.getPixels())
		return;
	surface.copyFrom(_overlaySurface);
}

void OSystem_Playdate::copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) {
	// Lazily create overlay if missing
	if (!_overlaySurface.getPixels()) {
		_overlaySurface.create(g_backBufferW ? g_backBufferW : 320, g_backBufferH ? g_backBufferH : 200,
			Graphics::PixelFormat::createFormatCLUT8());
	}
	if (!_overlaySurface.getPixels())
		return;
	if (x >= _overlaySurface.w || y >= _overlaySurface.h)
		return;
	int maxW = MIN(w, _overlaySurface.w - x);
	int maxH = MIN(h, _overlaySurface.h - y);
	if (maxW <= 0 || maxH <= 0)
		return;
	_overlaySurface.copyRectToSurface((const byte *)buf, pitch, x, y, maxW, maxH);
	// Mark backbuffer dirty so overlay is composited on next blit
	g_backBufferDirty = true;
}

int16 OSystem_Playdate::getOverlayHeight() const {
	return 240;
}

int16 OSystem_Playdate::getOverlayWidth() const {
	return 400;
}

Graphics::PixelFormat OSystem_Playdate::getOverlayFormat() const {
	return Graphics::PixelFormat::createFormatCLUT8();
}

bool OSystem_Playdate::showMouse(bool visible) {
	return false;
}

void OSystem_Playdate::warpMouse(int x, int y) {
}

void OSystem_Playdate::setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor, bool dontScale, const Graphics::PixelFormat *format, const byte *mask) {
}

void OSystem_Playdate::setCursorPalette(const byte *colors, uint start, uint num) {
}

uint32 OSystem_Playdate::getMillis(bool skipRecord) {
	return _pd->system->getCurrentTimeMilliseconds() - _startTime;
}

void OSystem_Playdate::delayMillis(uint msecs) {
	if (_timerManager) {
		DefaultTimerManager *tm = dynamic_cast<DefaultTimerManager *>(_timerManager);
		if (tm)
			tm->handler();
	}
	if (msecs > 0)
		std::this_thread::sleep_for(std::chrono::milliseconds(MIN<uint32>(msecs, 2)));
}

void OSystem_Playdate::getTimeAndDate(TimeDate &td, bool skipRecord) const {
	if (!_pd || !_pd->system)
		return;

	unsigned int ms = 0;
	const unsigned int epochSeconds = _pd->system->getSecondsSinceEpoch(&ms);

	PDDateTime dt;
	_pd->system->convertEpochToDateTime(epochSeconds, &dt);

	td.tm_sec = dt.second;
	td.tm_min = dt.minute;
	td.tm_hour = dt.hour;
	td.tm_mday = dt.day;
	td.tm_mon = dt.month - 1;
	td.tm_year = dt.year - 1900;
}

Common::MutexInternal *OSystem_Playdate::createMutex() {
	return new NullMutexInternal();
}

void OSystem_Playdate::quit() {
}

Common::EventSource *OSystem_Playdate::getDefaultEventSource() {
	if (!_eventSource)
		_eventSource = new PlaydateEventSource(_pd);
	return _eventSource;
}

FilesystemFactory *OSystem_Playdate::getFilesystemFactory() {
	return new PlaydateFilesystemFactory(_pd);
}

void OSystem_Playdate::handleEvent(PDSystemEvent event, uint32_t arg) {
	// Handle Playdate system events
}

void OSystem_Playdate::logMessage(LogMessageType::Type type, const char *message) {
	if (_pd && _pd->system)
		_pd->system->logToConsole("%s", message);
}

Graphics::PixelFormat OSystem_Playdate::getScreenFormat() const {
	return Graphics::PixelFormat::createFormatCLUT8();
}

Common::List<Graphics::PixelFormat> OSystem_Playdate::getSupportedFormats() const {
	Common::List<Graphics::PixelFormat> list;
	list.push_back(getScreenFormat());
	return list;
}

void OSystem_Playdate::getDefaultResolution(uint &w, uint &h) {
	w = 400;
	h = 240;
}
