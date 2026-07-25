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

#include "backends/graphics/playdate/playdate-graphics.h"

#include "common/textconsole.h"
#include "common/util.h"
#include "graphics/surface.h"

#include "backends/platform/playdate/playdate-coroutine.h"
#include "backends/platform/playdate/playdate-sdk.h"

// 8x8 Bayer matrix, scaled to thresholds in 0..255
static const uint8 kBayer8[8][8] = {
	{   0, 128,  32, 160,   8, 136,  40, 168 },
	{ 192,  64, 224,  96, 200,  72, 232, 104 },
	{  48, 176,  16, 144,  56, 184,  24, 152 },
	{ 240, 112, 208,  80, 248, 120, 216,  88 },
	{  12, 140,  44, 172,   4, 132,  36, 164 },
	{ 204,  76, 236, 108, 196,  68, 228, 100 },
	{  60, 188,  28, 156,  52, 180,  20, 148 },
	{ 252, 124, 220,  92, 244, 116, 212,  84 }
};

static Graphics::PixelFormat rgb565Format() {
	return Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0);
}

PlaydateGraphicsManager::PlaydateGraphicsManager(PlaydateAPI *pd)
	: _pd(pd),
	  _screenChangeID(0),
	  _overlayVisible(false),
	  _overlayInGUI(false),
	  _cursorKeyColor(0),
	  _cursorVisible(false),
	  _cursorX(0), _cursorY(0),
	  _cursorHotspotX(0), _cursorHotspotY(0),
	  _cursorPaletteEnabled(false),
	  _pointerMode(false),
	  _shakeOffsetX(0), _shakeOffsetY(0),
	  _dirtyTop(LCD_ROWS), _dirtyBottom(-1), _forceFullRefresh(true),
	  _inTransaction(false) {
	memset(_palette, 0, sizeof(_palette));
	memset(_paletteLum, 0, sizeof(_paletteLum));
	memset(_cursorPalette, 0, sizeof(_cursorPalette));

	_overlay.create(LCD_COLUMNS, LCD_ROWS, rgb565Format());
}

PlaydateGraphicsManager::~PlaydateGraphicsManager() {
	_gameScreen.free();
	_overlay.free();
	_cursor.free();
}

bool PlaydateGraphicsManager::hasFeature(OSystem::Feature f) const {
	return f == OSystem::kFeatureCursorPalette;
}

void PlaydateGraphicsManager::setFeatureState(OSystem::Feature f, bool enable) {
	if (f == OSystem::kFeatureCursorPalette)
		_cursorPaletteEnabled = enable;
}

bool PlaydateGraphicsManager::getFeatureState(OSystem::Feature f) const {
	if (f == OSystem::kFeatureCursorPalette)
		return _cursorPaletteEnabled;
	return false;
}

#ifdef USE_RGB_COLOR
Graphics::PixelFormat PlaydateGraphicsManager::getScreenFormat() const {
	return Graphics::PixelFormat::createFormatCLUT8();
}

Common::List<Graphics::PixelFormat> PlaydateGraphicsManager::getSupportedFormats() const {
	Common::List<Graphics::PixelFormat> list;
	list.push_back(Graphics::PixelFormat::createFormatCLUT8());
	return list;
}
#endif

void PlaydateGraphicsManager::initSize(uint width, uint height, const Graphics::PixelFormat *format) {
	assert(!format || format->isCLUT8());

	if (_gameScreen.w == (int16)width && _gameScreen.h == (int16)height)
		return;

	_gameScreen.free();
	_gameScreen.create(width, height, Graphics::PixelFormat::createFormatCLUT8());
	_screenChangeID++;
	_forceFullRefresh = true;
}

void PlaydateGraphicsManager::beginGFXTransaction() {
	_inTransaction = true;
}

OSystem::TransactionError PlaydateGraphicsManager::endGFXTransaction() {
	_inTransaction = false;
	return OSystem::kTransactionSuccess;
}

uint8 PlaydateGraphicsManager::luminanceRGB(byte r, byte g, byte b) {
	// ITU-R BT.601
	return (uint8)((r * 77 + g * 150 + b * 29) >> 8);
}

uint8 PlaydateGraphicsManager::luminanceRGB565(uint16 color) {
	const byte r = ((color >> 11) & 0x1f) << 3;
	const byte g = ((color >> 5) & 0x3f) << 2;
	const byte b = (color & 0x1f) << 3;
	return luminanceRGB(r, g, b);
}

void PlaydateGraphicsManager::updatePaletteLuminance(uint start, uint num) {
	for (uint i = start; i < start + num; i++)
		_paletteLum[i] = luminanceRGB(_palette[i * 3], _palette[i * 3 + 1], _palette[i * 3 + 2]);
}

void PlaydateGraphicsManager::setPalette(const byte *colors, uint start, uint num) {
	assert(start + num <= 256);
	memcpy(_palette + start * 3, colors, num * 3);
	updatePaletteLuminance(start, num);
}

void PlaydateGraphicsManager::grabPalette(byte *colors, uint start, uint num) const {
	assert(start + num <= 256);
	memcpy(colors, _palette + start * 3, num * 3);
}

void PlaydateGraphicsManager::copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) {
	if (!_gameScreen.getPixels())
		return;

	assert(x >= 0 && y >= 0 && x + w <= _gameScreen.w && y + h <= _gameScreen.h);
	_gameScreen.copyRectToSurface(buf, pitch, x, y, w, h);
	// renderGameScreen maps game row r to frame row r + _shakeOffsetY.
	markDirtyRows(y + _shakeOffsetY, y + h - 1 + _shakeOffsetY);
}

// Record that frame rows [top, bottom] changed and must be reconverted.
void PlaydateGraphicsManager::markDirtyRows(int top, int bottom) {
	if (top < 0)
		top = 0;
	if (bottom > LCD_ROWS - 1)
		bottom = LCD_ROWS - 1;
	if (top > bottom)
		return;
	if (top < _dirtyTop)
		_dirtyTop = top;
	if (bottom > _dirtyBottom)
		_dirtyBottom = bottom;
}

Graphics::Surface *PlaydateGraphicsManager::lockScreen() {
	return &_gameScreen;
}

void PlaydateGraphicsManager::unlockScreen() {
}

void PlaydateGraphicsManager::fillScreen(uint32 col) {
	if (_gameScreen.getPixels()) {
		_gameScreen.fillRect(Common::Rect(0, 0, _gameScreen.w, _gameScreen.h), col);
		_forceFullRefresh = true;
	}
}

void PlaydateGraphicsManager::fillScreen(const Common::Rect &r, uint32 col) {
	if (_gameScreen.getPixels()) {
		_gameScreen.fillRect(r, col);
		markDirtyRows(r.top + _shakeOffsetY, r.bottom - 1 + _shakeOffsetY);
	}
}

void PlaydateGraphicsManager::putPixel(uint8 *frame, int x, int y, bool white) {
	if (x < 0 || x >= LCD_COLUMNS || y < 0 || y >= LCD_ROWS)
		return;

	uint8 *p = frame + y * LCD_ROWSIZE + (x >> 3);
	const uint8 bit = 0x80 >> (x & 7);
	if (white)
		*p |= bit;
	else
		*p &= ~bit;
}

// Convert frame rows [rowStart, rowEnd] (inclusive) of the game screen to the
// 1-bit framebuffer. A row range lets updateScreen reconvert only what changed.
void PlaydateGraphicsManager::renderGameScreen(uint8 *frame, int rowStart, int rowEnd) const {
	if (!_gameScreen.getPixels())
		return;

	// AGI renders in the Playdate Hercules mode: a 400x240 two-color
	// surface (index 0 black, index 1 bright) with authentic Sierra
	// fill patterns already baked in. It is displayed 1:1 at the top
	// left with a fixed threshold - dithering here would destroy the
	// original patterns.
	const int outW = MIN<int>(_gameScreen.w, LCD_COLUMNS);
	const int outH = MIN<int>(_gameScreen.h, LCD_ROWS);
	const int y0 = _shakeOffsetY;

	if (rowStart < 0)
		rowStart = 0;
	if (rowEnd > LCD_ROWS - 1)
		rowEnd = LCD_ROWS - 1;

	for (int fbY = rowStart; fbY <= rowEnd; fbY++) {
		const int y = fbY - y0; // game row that maps to this frame row
		if (y < 0 || y >= outH)
			continue;

		const byte *src = (const byte *)_gameScreen.getBasePtr(0, y);
		uint8 *dstRow = frame + fbY * LCD_ROWSIZE;

		for (int x = 0; x < outW; x++) {
			const int fbX = x + _shakeOffsetX;
			if (fbX < 0 || fbX >= LCD_COLUMNS)
				continue;

			const bool white = _paletteLum[src[x]] >= 128;
			const uint8 bit = 0x80 >> (fbX & 7);
			if (white)
				dstRow[fbX >> 3] |= bit;
			else
				dstRow[fbX >> 3] &= ~bit;
		}
	}
}

void PlaydateGraphicsManager::renderOverlay(uint8 *frame) const {
	for (int y = 0; y < _overlay.h; y++) {
		const uint16 *src = (const uint16 *)_overlay.getBasePtr(0, y);
		uint8 *dstRow = frame + y * LCD_ROWSIZE;
		const uint8 *bayerRow = kBayer8[y & 7];

		for (int x = 0; x < _overlay.w; x++) {
			const bool white = luminanceRGB565(src[x]) > bayerRow[x & 7];
			const uint8 bit = 0x80 >> (x & 7);
			if (white)
				dstRow[x >> 3] |= bit;
			else
				dstRow[x >> 3] &= ~bit;
		}
	}
}

void PlaydateGraphicsManager::renderCursor(uint8 *frame) const {
	if (!_cursor.getPixels())
		return;

	// Map the cursor position from game/overlay coordinates to
	// display coordinates.
	int baseX, baseY, step = 1;
	if (_overlayVisible) {
		baseX = 0;
		baseY = 0;
	} else {
		step = (_gameScreen.w > LCD_COLUMNS || _gameScreen.h > LCD_ROWS) ? 2 : 1;
		baseX = (LCD_COLUMNS - _gameScreen.w / step) / 2;
		baseY = (LCD_ROWS - _gameScreen.h / step) / 2;
	}

	const int posX = baseX + _cursorX / step - _cursorHotspotX;
	const int posY = baseY + _cursorY / step - _cursorHotspotY;

	const bool paletted = _cursor.format.isCLUT8();
	const byte *palette = _cursorPaletteEnabled ? _cursorPalette : _palette;

	for (int y = 0; y < _cursor.h; y++) {
		const uint8 *bayerRow = kBayer8[(posY + y) & 7];
		for (int x = 0; x < _cursor.w; x++) {
			uint8 lum;
			if (paletted) {
				const byte color = *(const byte *)_cursor.getBasePtr(x, y);
				if (color == _cursorKeyColor)
					continue;
				lum = luminanceRGB(palette[color * 3], palette[color * 3 + 1], palette[color * 3 + 2]);
			} else {
				const uint16 color = *(const uint16 *)_cursor.getBasePtr(x, y);
				if (color == _cursorKeyColor)
					continue;
				lum = luminanceRGB565(color);
			}

			putPixel(frame, posX + x, posY + y, lum > bayerRow[(posX + x) & 7]);
		}
	}
}

void PlaydateGraphicsManager::updateScreen() {
	uint8 *frame = _pd->graphics->getFrame();
	if (!frame)
		return;

	// The overlay, the software cursor and a screen shake all need the whole
	// frame (they either cover it or shift the whole mapping). Everything else -
	// AGI gameplay - only changes the rows the engine copied this frame, so
	// convert and refresh just those.
	const bool cursorShown = _cursorVisible && (_pointerMode || _overlayVisible);
	const bool full = _forceFullRefresh || _overlayVisible || cursorShown || _shakeOffsetX || _shakeOffsetY;

	int top, bottom;
	if (full) {
		if (_overlayVisible) {
			renderOverlay(frame);
		} else {
			memset(frame, 0x00, LCD_ROWS * LCD_ROWSIZE);
			renderGameScreen(frame, 0, LCD_ROWS - 1);
		}
		if (cursorShown)
			renderCursor(frame);
		top = 0;
		bottom = LCD_ROWS - 1;
		_forceFullRefresh = false;
	} else if (_dirtyBottom >= _dirtyTop) {
		renderGameScreen(frame, _dirtyTop, _dirtyBottom);
		top = _dirtyTop;
		bottom = _dirtyBottom;
	} else {
		top = LCD_ROWS;
		bottom = -1; // nothing changed; the previous frame is still on screen
	}

	if (bottom >= top)
		_pd->graphics->markUpdatedRows(top, bottom);

	// Reset the dirty range for the next frame.
	_dirtyTop = LCD_ROWS;
	_dirtyBottom = -1;

	// Yield only once this resume slice has used its compute budget. A yield
	// ends the OS update callback and costs a whole display frame (~33ms), and
	// the engine calls updateScreen several times per game cycle (sprite blits,
	// end of cycle, event-wait loops); yielding on each call quantized every
	// one of those to a full frame and capped AGI at a fraction of its cycle
	// rate - visibly jerky animation. The rows marked above accumulate in the
	// frame buffer and are presented at whichever yield actually ends the
	// slice (here once the budget is spent, or the engine's next real wait in
	// delayMillis).
	const unsigned now = _pd->system->getCurrentTimeMilliseconds();
	if (Playdate::coroutineSliceBudgetUsed(now))
		Playdate::coroutineYield();
}

void PlaydateGraphicsManager::setShakePos(int shakeXOffset, int shakeYOffset) {
	if (shakeXOffset != _shakeOffsetX || shakeYOffset != _shakeOffsetY)
		_forceFullRefresh = true; // the shake shifts the whole game->frame mapping
	_shakeOffsetX = shakeXOffset;
	_shakeOffsetY = shakeYOffset;
}

void PlaydateGraphicsManager::showOverlay(bool inGUI) {
	_overlayVisible = true;
	_overlayInGUI = inGUI;
	_forceFullRefresh = true;
}

void PlaydateGraphicsManager::hideOverlay() {
	_overlayVisible = false;
	_overlayInGUI = false;
	_forceFullRefresh = true;
}

Graphics::PixelFormat PlaydateGraphicsManager::getOverlayFormat() const {
	return rgb565Format();
}

int16 PlaydateGraphicsManager::getOverlayHeight() const {
	return _overlay.h;
}

int16 PlaydateGraphicsManager::getOverlayWidth() const {
	return _overlay.w;
}

void PlaydateGraphicsManager::clearOverlay() {
	_overlay.fillRect(Common::Rect(0, 0, _overlay.w, _overlay.h), 0);
}

void PlaydateGraphicsManager::grabOverlay(Graphics::Surface &surface) const {
	assert(surface.w >= _overlay.w && surface.h >= _overlay.h &&
	       surface.format.bytesPerPixel == _overlay.format.bytesPerPixel);
	surface.copyRectToSurface(_overlay, 0, 0, Common::Rect(0, 0, _overlay.w, _overlay.h));
}

void PlaydateGraphicsManager::copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) {
	_overlay.copyRectToSurface(buf, pitch, x, y, w, h);
}

bool PlaydateGraphicsManager::showMouse(bool visible) {
	const bool last = _cursorVisible;
	if (visible != last)
		_forceFullRefresh = true; // repaint to draw or erase the cursor
	_cursorVisible = visible;
	return last;
}

void PlaydateGraphicsManager::setPointerMode(bool on) {
	if (on != _pointerMode)
		_forceFullRefresh = true; // the cursor starts/stops being drawn
	_pointerMode = on;
}

void PlaydateGraphicsManager::warpMouse(int x, int y) {
	if ((x != _cursorX || y != _cursorY) && _cursorVisible && _pointerMode)
		_forceFullRefresh = true; // move the cursor: repaint to erase the old one
	_cursorX = x;
	_cursorY = y;
}

void PlaydateGraphicsManager::setMouseCursor(const void *buf, uint w, uint h,
                                             int hotspotX, int hotspotY,
                                             uint32 keycolor,
                                             const Graphics::PixelFormat *format,
                                             const byte *mask,
                                             frac_t scaleX, frac_t scaleY) {
	const Graphics::PixelFormat cursorFormat =
		format ? *format : Graphics::PixelFormat::createFormatCLUT8();

	// Only paletted and RGB565 cursors are supported; that covers the
	// launcher GUI and the targeted engines.
	if (cursorFormat.bytesPerPixel > 2) {
		warning("PlaydateGraphicsManager: unsupported %d bpp cursor",
		        cursorFormat.bytesPerPixel * 8);
		return;
	}

	_cursorHotspotX = hotspotX;
	_cursorHotspotY = hotspotY;
	_cursorKeyColor = keycolor;

	if (_cursor.w != (int16)w || _cursor.h != (int16)h || _cursor.format != cursorFormat) {
		_cursor.free();
		if (w > 0 && h > 0)
			_cursor.create(w, h, cursorFormat);
	}

	if (buf && _cursor.getPixels())
		_cursor.copyRectToSurface(buf, w * cursorFormat.bytesPerPixel, 0, 0, w, h);
}

void PlaydateGraphicsManager::setCursorPalette(const byte *colors, uint start, uint num) {
	assert(start + num <= 256);
	memcpy(_cursorPalette + start * 3, colors, num * 3);
}
