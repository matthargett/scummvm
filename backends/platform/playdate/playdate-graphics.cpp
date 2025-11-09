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

#include "backends/platform/playdate/playdate-graphics.h"
#include "common/util.h"
#include "common/textconsole.h"

PlaydateGraphicsManager::PlaydateGraphicsManager(PlaydateAPI *pd)
	: _pd(pd),
	  _screenWidth(320),
	  _screenHeight(200),
	  _screenChangeID(0),
	  _paletteDirty(false),
	  _framebuffer(nullptr),
	  _framebufferRowBytes(0),
	  _overlayVisible(false),
	  _cursorVisible(false),
	  _cursorX(0), _cursorY(0),
	  _cursorHotspotX(0), _cursorHotspotY(0),
	  _cursorKeyColor(0),
	  _cursorPaletteEnabled(false),
	  _shakeOffsetX(0), _shakeOffsetY(0),
	  _inTransaction(false) {

	memset(_palette, 0, sizeof(_palette));
	memset(_cursorPalette, 0, sizeof(_cursorPalette));

	// Get framebuffer from Playdate
	_framebuffer = _pd->graphics->getFrame();
	_pd->graphics->getDisplayBufferBitmap();

	// Calculate row bytes (400 pixels / 8 bits per byte = 50 bytes per row)
	_framebufferRowBytes = PLAYDATE_SCREEN_WIDTH / 8;
}

PlaydateGraphicsManager::~PlaydateGraphicsManager() {
	_gameScreen.free();
	_overlay.free();
	_cursor.free();
}

bool PlaydateGraphicsManager::hasFeature(OSystem::Feature f) const {
	return (f == OSystem::kFeatureCursorPalette);
}

void PlaydateGraphicsManager::setFeatureState(OSystem::Feature f, bool enable) {
	if (f == OSystem::kFeatureCursorPalette) {
		_cursorPaletteEnabled = enable;
	}
}

bool PlaydateGraphicsManager::getFeatureState(OSystem::Feature f) const {
	if (f == OSystem::kFeatureCursorPalette) {
		return _cursorPaletteEnabled;
	}
	return false;
}

Graphics::PixelFormat PlaydateGraphicsManager::getScreenFormat() const {
	return Graphics::PixelFormat::createFormatCLUT8();
}

Common::List<Graphics::PixelFormat> PlaydateGraphicsManager::getSupportedFormats() const {
	Common::List<Graphics::PixelFormat> list;
	list.push_back(Graphics::PixelFormat::createFormatCLUT8());
	return list;
}

void PlaydateGraphicsManager::initSize(uint width, uint height, const Graphics::PixelFormat *format) {
	if (_inTransaction) {
		_transactionDetails.width = width;
		_transactionDetails.height = height;
		_transactionDetails.format = format ? *format : Graphics::PixelFormat::createFormatCLUT8();
		_transactionDetails.sizeChanged = true;
		return;
	}

	_screenWidth = width;
	_screenHeight = height;

	// Allocate game screen buffer
	_gameScreen.create(width, height, Graphics::PixelFormat::createFormatCLUT8());
	_screenChangeID++;
}

void PlaydateGraphicsManager::beginGFXTransaction() {
	_inTransaction = true;
	_transactionDetails.sizeChanged = false;
}

OSystem::TransactionError PlaydateGraphicsManager::endGFXTransaction() {
	if (!_inTransaction) {
		return OSystem::kTransactionSuccess;
	}

	if (_transactionDetails.sizeChanged) {
		initSize(_transactionDetails.width, _transactionDetails.height, &_transactionDetails.format);
	}

	_inTransaction = false;
	return OSystem::kTransactionSuccess;
}

void PlaydateGraphicsManager::setPalette(const byte *colors, uint start, uint num) {
	assert(start + num <= 256);
	memcpy(&_palette[start * 3], colors, num * 3);
	_paletteDirty = true;
}

void PlaydateGraphicsManager::grabPalette(byte *colors, uint start, uint num) const {
	assert(start + num <= 256);
	memcpy(colors, &_palette[start * 3], num * 3);
}

void PlaydateGraphicsManager::copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) {
	if (!_gameScreen.getPixels()) {
		return;
	}

	const byte *src = (const byte *)buf;
	byte *dst = (byte *)_gameScreen.getBasePtr(x, y);

	for (int cy = 0; cy < h; cy++) {
		memcpy(dst, src, w);
		src += pitch;
		dst += _gameScreen.pitch;
	}
}

Graphics::Surface *PlaydateGraphicsManager::lockScreen() {
	return &_gameScreen;
}

void PlaydateGraphicsManager::unlockScreen() {
	// Nothing to do
}

void PlaydateGraphicsManager::fillScreen(uint32 col) {
	if (_gameScreen.getPixels()) {
		_gameScreen.fillRect(Common::Rect(0, 0, _gameScreen.w, _gameScreen.h), col);
	}
}

void PlaydateGraphicsManager::fillScreen(const Common::Rect &r, uint32 col) {
	if (_gameScreen.getPixels()) {
		_gameScreen.fillRect(r, col);
	}
}

uint8_t PlaydateGraphicsManager::getLuminance(byte r, byte g, byte b) const {
	// Standard luminance calculation (ITU-R BT.601)
	return (uint8_t)((r * 299 + g * 587 + b * 114) / 1000);
}

void PlaydateGraphicsManager::convertPalettedToMonochrome(const byte *src, int srcPitch,
                                                           int x, int y, int w, int h) {
	if (!_framebuffer || !src) {
		return;
	}

	// Use Floyd-Steinberg dithering for better quality
	// Create error buffer
	int errorWidth = w + 2;
	int16_t *errorBuffer[2];
	errorBuffer[0] = new int16_t[errorWidth];
	errorBuffer[1] = new int16_t[errorWidth];
	memset(errorBuffer[0], 0, errorWidth * sizeof(int16_t));
	memset(errorBuffer[1], 0, errorWidth * sizeof(int16_t));

	int currentError = 0;
	int nextError = 1;

	for (int cy = 0; cy < h; cy++) {
		// Clear next error line
		memset(errorBuffer[nextError], 0, errorWidth * sizeof(int16_t));

		for (int cx = 0; cx < w; cx++) {
			byte paletteIndex = src[cy * srcPitch + cx];
			byte r = _palette[paletteIndex * 3 + 0];
			byte g = _palette[paletteIndex * 3 + 1];
			byte b = _palette[paletteIndex * 3 + 2];

			// Get luminance and add error
			int16_t luminance = getLuminance(r, g, b);
			luminance += errorBuffer[currentError][cx + 1];
			luminance = CLIP<int16_t>(luminance, 0, 255);

			// Determine if pixel should be black or white
			bool isWhite = luminance >= 128;
			int16_t error = luminance - (isWhite ? 255 : 0);

			// Distribute error using Floyd-Steinberg weights
			// [  *   7/16 ]
			// [3/16 5/16 1/16]
			errorBuffer[currentError][cx + 2] += error * 7 / 16;
			errorBuffer[nextError][cx + 0] += error * 3 / 16;
			errorBuffer[nextError][cx + 1] += error * 5 / 16;
			errorBuffer[nextError][cx + 2] += error * 1 / 16;

			// Set pixel in framebuffer
			int fbX = x + cx;
			int fbY = y + cy + _shakeOffsetY;

			if (fbX >= 0 && fbX < PLAYDATE_SCREEN_WIDTH && fbY >= 0 && fbY < PLAYDATE_SCREEN_HEIGHT) {
				int byteOffset = fbY * _framebufferRowBytes + (fbX / 8);
				int bitOffset = 7 - (fbX % 8);

				if (isWhite) {
					_framebuffer[byteOffset] |= (1 << bitOffset);
				} else {
					_framebuffer[byteOffset] &= ~(1 << bitOffset);
				}
			}
		}

		// Swap error buffers
		int temp = currentError;
		currentError = nextError;
		nextError = temp;
	}

	delete[] errorBuffer[0];
	delete[] errorBuffer[1];
}

void PlaydateGraphicsManager::clearFramebuffer() {
	if (_framebuffer) {
		memset(_framebuffer, 0xFF, PLAYDATE_SCREEN_HEIGHT * _framebufferRowBytes);
	}
}

void PlaydateGraphicsManager::updateScreen() {
	if (!_framebuffer) {
		return;
	}

	// Clear framebuffer
	clearFramebuffer();

	if (_overlayVisible) {
		// Draw overlay (GUI)
		if (_overlay.getPixels()) {
			// For now, just clear to white for overlay
			// TODO: Implement proper overlay rendering
		}
	} else {
		// Draw game screen
		if (_gameScreen.getPixels()) {
			// Calculate scaling and centering
			int scaleX = PLAYDATE_SCREEN_WIDTH / _screenWidth;
			int scaleY = PLAYDATE_SCREEN_HEIGHT / _screenHeight;
			int scale = MIN(scaleX, scaleY);

			if (scale > 1) {
				// Scale up the game screen
				// For simplicity, use nearest neighbor scaling
				// TODO: Implement proper scaling
				scale = 1;  // For now, no scaling
			}

			int offsetX = (PLAYDATE_SCREEN_WIDTH - _screenWidth * scale) / 2;
			int offsetY = (PLAYDATE_SCREEN_HEIGHT - _screenHeight * scale) / 2;

			// Convert and render
			convertPalettedToMonochrome((const byte *)_gameScreen.getPixels(),
			                            _gameScreen.pitch,
			                            offsetX, offsetY,
			                            _screenWidth, _screenHeight);
		}
	}

	// Draw cursor
	if (_cursorVisible && !_overlayVisible) {
		drawCursor();
	}

	// Mark screen as updated
	_pd->graphics->markUpdatedRows(0, PLAYDATE_SCREEN_HEIGHT - 1);
}

void PlaydateGraphicsManager::drawCursor() {
	if (!_cursor.getPixels() || !_framebuffer) {
		return;
	}

	int cursorScreenX = _cursorX - _cursorHotspotX;
	int cursorScreenY = _cursorY - _cursorHotspotY;

	const byte *palette = _cursorPaletteEnabled ? _cursorPalette : _palette;

	for (int cy = 0; cy < _cursor.h; cy++) {
		for (int cx = 0; cx < _cursor.w; cx++) {
			byte paletteIndex = *((const byte *)_cursor.getBasePtr(cx, cy));

			if (paletteIndex == _cursorKeyColor) {
				continue;  // Transparent pixel
			}

			byte r = palette[paletteIndex * 3 + 0];
			byte g = palette[paletteIndex * 3 + 1];
			byte b = palette[paletteIndex * 3 + 2];
			uint8_t luminance = getLuminance(r, g, b);

			int fbX = cursorScreenX + cx;
			int fbY = cursorScreenY + cy;

			if (fbX >= 0 && fbX < PLAYDATE_SCREEN_WIDTH && fbY >= 0 && fbY < PLAYDATE_SCREEN_HEIGHT) {
				int byteOffset = fbY * _framebufferRowBytes + (fbX / 8);
				int bitOffset = 7 - (fbX % 8);

				if (luminance >= 128) {
					_framebuffer[byteOffset] |= (1 << bitOffset);
				} else {
					_framebuffer[byteOffset] &= ~(1 << bitOffset);
				}
			}
		}
	}
}

void PlaydateGraphicsManager::setShakePos(int shakeXOffset, int shakeYOffset) {
	_shakeOffsetX = shakeXOffset;
	_shakeOffsetY = shakeYOffset;
}

void PlaydateGraphicsManager::showOverlay(bool inGUI) {
	_overlayVisible = true;
	if (!_overlay.getPixels()) {
		_overlay.create(PLAYDATE_SCREEN_WIDTH, PLAYDATE_SCREEN_HEIGHT,
		                Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
	}
}

void PlaydateGraphicsManager::hideOverlay() {
	_overlayVisible = false;
}

Graphics::PixelFormat PlaydateGraphicsManager::getOverlayFormat() const {
	return Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0);
}

void PlaydateGraphicsManager::clearOverlay() {
	if (_overlay.getPixels()) {
		_overlay.fillRect(Common::Rect(0, 0, _overlay.w, _overlay.h), 0);
	}
}

void PlaydateGraphicsManager::grabOverlay(Graphics::Surface &surface) const {
	if (_overlay.getPixels()) {
		surface.copyFrom(_overlay);
	}
}

void PlaydateGraphicsManager::copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) {
	if (!_overlay.getPixels()) {
		return;
	}

	const byte *src = (const byte *)buf;
	byte *dst = (byte *)_overlay.getBasePtr(x, y);

	for (int cy = 0; cy < h; cy++) {
		memcpy(dst, src, w * _overlay.format.bytesPerPixel);
		src += pitch;
		dst += _overlay.pitch;
	}
}

bool PlaydateGraphicsManager::showMouse(bool visible) {
	bool last = _cursorVisible;
	_cursorVisible = visible;
	return last;
}

void PlaydateGraphicsManager::warpMouse(int x, int y) {
	_cursorX = x;
	_cursorY = y;
}

void PlaydateGraphicsManager::setMouseCursor(const void *buf, uint w, uint h,
                                              int hotspotX, int hotspotY,
                                              uint32 keycolor, bool dontScale,
                                              const Graphics::PixelFormat *format,
                                              const byte *mask) {
	_cursorHotspotX = hotspotX;
	_cursorHotspotY = hotspotY;
	_cursorKeyColor = keycolor;

	if (_cursor.w != w || _cursor.h != h) {
		_cursor.free();
		_cursor.create(w, h, Graphics::PixelFormat::createFormatCLUT8());
	}

	if (buf) {
		memcpy(_cursor.getPixels(), buf, w * h);
	}
}

void PlaydateGraphicsManager::setCursorPalette(const byte *colors, uint start, uint num) {
	assert(start + num <= 256);
	memcpy(&_cursorPalette[start * 3], colors, num * 3);
}
