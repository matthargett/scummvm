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

#ifndef BACKENDS_GRAPHICS_PLAYDATE_H
#define BACKENDS_GRAPHICS_PLAYDATE_H

#include "backends/graphics/graphics.h"
#include "common/rect.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

#include "pd_api.h"

/**
 * Graphics manager for Playdate's 1-bit 400x240 monochrome display.
 * Handles conversion from ScummVM's paletted graphics to 1-bit display
 * using Floyd-Steinberg dithering for better visual quality.
 */
class PlaydateGraphicsManager : public GraphicsManager {
public:
	PlaydateGraphicsManager(PlaydateAPI *pd);
	virtual ~PlaydateGraphicsManager();

	// GraphicsManager API
	bool hasFeature(OSystem::Feature f) const override;
	void setFeatureState(OSystem::Feature f, bool enable) override;
	bool getFeatureState(OSystem::Feature f) const override;

	Graphics::PixelFormat getScreenFormat() const override;
	Common::List<Graphics::PixelFormat> getSupportedFormats() const override;

	void initSize(uint width, uint height, const Graphics::PixelFormat *format = NULL) override;
	int getScreenChangeID() const override { return _screenChangeID; }

	void beginGFXTransaction() override;
	OSystem::TransactionError endGFXTransaction() override;

	int16 getHeight() const override { return _screenHeight; }
	int16 getWidth() const override { return _screenWidth; }

	void setPalette(const byte *colors, uint start, uint num) override;
	void grabPalette(byte *colors, uint start, uint num) const override;

	void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) override;
	Graphics::Surface *lockScreen() override;
	void unlockScreen() override;
	void fillScreen(uint32 col) override;
	void fillScreen(const Common::Rect &r, uint32 col) override;
	void updateScreen() override;

	void setShakePos(int shakeXOffset, int shakeYOffset) override;
	void setFocusRectangle(const Common::Rect& rect) override {}
	void clearFocusRectangle() override {}

	// Overlay (for GUI)
	void showOverlay(bool inGUI) override;
	void hideOverlay() override;
	bool isOverlayVisible() const override { return _overlayVisible; }
	Graphics::PixelFormat getOverlayFormat() const override;
	void clearOverlay() override;
	void grabOverlay(Graphics::Surface &surface) const override;
	void copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) override;
	int16 getOverlayHeight() const override { return PLAYDATE_SCREEN_HEIGHT; }
	int16 getOverlayWidth() const override { return PLAYDATE_SCREEN_WIDTH; }

	// Mouse cursor
	bool showMouse(bool visible) override;
	void warpMouse(int x, int y) override;
	void setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY,
	                    uint32 keycolor, bool dontScale = false,
	                    const Graphics::PixelFormat *format = NULL, const byte *mask = NULL) override;
	void setCursorPalette(const byte *colors, uint start, uint num) override;

private:
	static const int PLAYDATE_SCREEN_WIDTH = 400;
	static const int PLAYDATE_SCREEN_HEIGHT = 240;

	PlaydateAPI *_pd;

	// Screen dimensions
	uint _screenWidth;
	uint _screenHeight;
	int _screenChangeID;

	// Game screen buffer (paletted)
	Graphics::Surface _gameScreen;
	byte _palette[256 * 3];  // RGB palette
	bool _paletteDirty;

	// Playdate framebuffer (1-bit)
	uint8_t *_framebuffer;
	int _framebufferRowBytes;

	// Overlay
	bool _overlayVisible;
	Graphics::Surface _overlay;

	// Mouse cursor
	bool _cursorVisible;
	int _cursorX, _cursorY;
	int _cursorHotspotX, _cursorHotspotY;
	Graphics::Surface _cursor;
	uint32 _cursorKeyColor;
	byte _cursorPalette[256 * 3];
	bool _cursorPaletteEnabled;

	// Shake effect
	int _shakeOffsetX;
	int _shakeOffsetY;

	// Transaction state
	bool _inTransaction;
	struct TransactionDetails {
		uint width, height;
		Graphics::PixelFormat format;
		bool sizeChanged;
	};
	TransactionDetails _transactionDetails;

	// Helper methods
	void updateFramebuffer();
	void convertPalettedToMonochrome(const byte *src, int srcPitch,
	                                  int x, int y, int w, int h);
	uint8_t getLuminance(byte r, byte g, byte b) const;
	void drawCursor();
	void clearFramebuffer();
};

#endif
