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

typedef struct PlaydateAPI PlaydateAPI;

/**
 * Graphics manager for the Playdate's 1-bit 400x240 display.
 *
 * Game graphics (8-bit paletted) and the GUI overlay (RGB565) are
 * converted to 1-bit with 8x8 ordered (Bayer) dithering. Ordered
 * dithering is used instead of error diffusion because it is stable
 * across frames: moving game graphics do not shimmer.
 *
 * The standard 320x200 screen of AGI and SCI games is centered on the
 * display; 640x400 (hires AGI) is downscaled by 2.
 */
class PlaydateGraphicsManager final : public GraphicsManager {
public:
	explicit PlaydateGraphicsManager(PlaydateAPI *pd);
	~PlaydateGraphicsManager() override;

	bool hasFeature(OSystem::Feature f) const override;
	void setFeatureState(OSystem::Feature f, bool enable) override;
	bool getFeatureState(OSystem::Feature f) const override;

#ifdef USE_RGB_COLOR
	Graphics::PixelFormat getScreenFormat() const override;
	Common::List<Graphics::PixelFormat> getSupportedFormats() const override;
#endif

	void initSize(uint width, uint height, const Graphics::PixelFormat *format = nullptr) override;
	int getScreenChangeID() const override { return _screenChangeID; }

	void beginGFXTransaction() override;
	OSystem::TransactionError endGFXTransaction() override;

	int16 getHeight() const override { return _gameScreen.h; }
	int16 getWidth() const override { return _gameScreen.w; }

	void setPalette(const byte *colors, uint start, uint num) override;
	void grabPalette(byte *colors, uint start, uint num) const override;

	void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) override;
	Graphics::Surface *lockScreen() override;
	void unlockScreen() override;
	void fillScreen(uint32 col) override;
	void fillScreen(const Common::Rect &r, uint32 col) override;
	void updateScreen() override;

	void setShakePos(int shakeXOffset, int shakeYOffset) override;
	void setFocusRectangle(const Common::Rect &rect) override {}
	void clearFocusRectangle() override {}

	void showOverlay(bool inGUI) override;
	void hideOverlay() override;
	bool isOverlayVisible() const override { return _overlayVisible; }
	Graphics::PixelFormat getOverlayFormat() const override;
	void clearOverlay() override;
	void grabOverlay(Graphics::Surface &surface) const override;
	void copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) override;
	int16 getOverlayHeight() const override;
	int16 getOverlayWidth() const override;

	bool showMouse(bool visible) override;
	void warpMouse(int x, int y) override;
	void setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY,
	                    uint32 keycolor, const Graphics::PixelFormat *format,
	                    const byte *mask, frac_t scaleX, frac_t scaleY) override;
	void setCursorPalette(const byte *colors, uint start, uint num) override;

private:
	void renderGameScreen(uint8 *frame) const;
	void renderOverlay(uint8 *frame) const;
	void renderCursor(uint8 *frame) const;
	void updatePaletteLuminance(uint start, uint num);

	static uint8 luminanceRGB(byte r, byte g, byte b);
	static uint8 luminanceRGB565(uint16 color);
	static void putPixel(uint8 *frame, int x, int y, bool white);

	PlaydateAPI *_pd;

	int _screenChangeID;

	// Game screen (8-bit paletted)
	Graphics::Surface _gameScreen;
	byte _palette[256 * 3];
	uint8 _paletteLum[256];

	// GUI overlay (RGB565, display sized)
	Graphics::Surface _overlay;
	bool _overlayVisible;
	bool _overlayInGUI;

	// Mouse cursor, kept in its source format (CLUT8 or RGB565) and
	// converted on the fly while drawing (cursors are tiny)
	Graphics::Surface _cursor;
	uint32 _cursorKeyColor;
	bool _cursorVisible;
	int _cursorX, _cursorY;
	int _cursorHotspotX, _cursorHotspotY;
	byte _cursorPalette[256 * 3];
	bool _cursorPaletteEnabled;

	int _shakeOffsetX;
	int _shakeOffsetY;

	bool _inTransaction;
};

#endif
