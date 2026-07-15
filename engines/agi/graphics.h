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

#ifndef AGI_GRAPHICS_H
#define AGI_GRAPHICS_H

#include "agi/font.h"

namespace Agi {

#define SCRIPT_WIDTH 160
#define SCRIPT_HEIGHT 168
#define VISUAL_WIDTH 160
#define VISUAL_HEIGHT 200
#define DISPLAY_DEFAULT_WIDTH 320
#define DISPLAY_DEFAULT_HEIGHT 200

enum GfxScreenUpscaledMode {
	DISPLAY_UPSCALED_DISABLED = 0,
	DISPLAY_UPSCALED_640x400 = 1
};

class AgiBase;

enum GfxScreenMasks {
	GFX_SCREEN_MASK_VISUAL = 1,
	GFX_SCREEN_MASK_PRIORITY = 2,
	GFX_SCREEN_MASK_ALL = GFX_SCREEN_MASK_VISUAL | GFX_SCREEN_MASK_PRIORITY
};

struct MouseCursorData {
	const byte *bitmapData;
	byte *bitmapDataAllocated;
	uint16 width;
	uint16 height;
	int hotspotX;
	int hotspotY;
};

class GfxMgr {
private:
	AgiBase *_vm;
	GfxFont *_font;

	uint8 _paletteGfxMode[256 * 3];
	uint8 _paletteTextMode[256 * 3];

	uint8 _agipalPalette[16 * 3];
	int _agipalFileNum;

public:
	GfxMgr(AgiBase *vm, GfxFont *font);

	void initVideo();
	void deinitVideo();
	static void initPalette(uint8 *destPalette, const uint8 *paletteData, uint colorCount = 16, uint fromBits = 6, uint toBits = 8);
	static void initPaletteCLUT(uint8 *destPalette, const uint16 *paletteCLUTData, uint colorCount = 16);
	void setAGIPal(int);
	int getAGIPalFileNum() const;
	void setPalette(bool GfxModePalette);

	void initMouseCursor(MouseCursorData *mouseCursor, const byte *bitmapData, uint16 width, uint16 height, int hotspotX, int hotspotY);
	void setMouseCursor(bool busy = false);

	void setRenderStartOffset(uint16 offsetY);
	uint16 getRenderStartDisplayOffsetY() const;

	void translateGamePosToDisplayScreen(int16 &x, int16 &y) const;
	void translateVisualPosToDisplayScreen(int16 &x, int16 &y) const;
	void translateDisplayPosToGameScreen(int16 &x, int16 &y) const;

	void translateVisualDimensionToDisplayScreen(int16 &width, int16 &height) const;
	void translateDisplayDimensionToVisualScreen(int16 &width, int16 &height) const;

	void translateGameRectToDisplayScreen(int16 &x, int16 &y, int16 &width, int16 &height) const;
	void translateVisualRectToDisplayScreen(int16 &x, int16 &y, int16 &width, int16 &height) const;

	uint32 getDisplayOffsetToGameScreenPos(int16 x, int16 y) const;
	uint32 getDisplayOffsetToVisualScreenPos(int16 x, int16 y) const;

	void copyDisplayRectToScreen(int16 x, int16 y, int16 width, int16 height);
	void copyDisplayRectToScreen(int16 x, int16 adjX, int16 y, int16 adjY, int16 width, int16 adjWidth, int16 height, int16 adjHeight);
	void copyDisplayRectToScreenUsingGamePos(int16 x, int16 y, int16 width, int16 height);
	void copyDisplayRectToScreenUsingVisualPos(int16 x, int16 y, int16 width, int16 height);
	void copyDisplayToScreen();

	void translateFontPosToDisplayScreen(int16 &x, int16 &y) const;
	void translateDisplayPosToFontScreen(int16 &x, int16 &y) const;
	void translateFontDimensionToDisplayScreen(int16 &width, int16 &height) const;
	void translateFontRectToDisplayScreen(int16 &x, int16 &y, int16 &width, int16 &height) const;
	Common::Rect getFontRectForDisplayScreen(int16 column, int16 row, int16 width, int16 height) const;

private:
	uint _pixels;
	uint _displayPixels;

	byte *_activeScreen;
	byte *_gameScreen;     // 160x168 - screen, where the actual game content is drawn to (actual graphics, not including status line, prompt, etc.)
	byte *_priorityScreen; // 160x168 - screen contains priority information of the game screen
	// the term "visual screen" is effectively the display screen, but at 160x200 resolution. Used for coordinate translation
	byte *_displayScreen; // 320x200 or 640x400 - screen, that the game is rendered to and which is then copied to framebuffer

	uint16 _displayScreenWidth;
	uint16 _displayScreenHeight;

	// kRenderPlaydate layout. The game is always rendered 320 pixels wide
	// so its pixel aspect matches the original (AGI's 160 doubled to 320,
	// 200 lines scaled to 240 = 1.2x) rather than being stretched across
	// the full 400-pixel display. Parser (AGI) games sit at the left with
	// offset 0 and leave the right 80 pixels for the word picker; games
	// with no word list are centered (offset 40) and letterboxed with a
	// black bar on each side.
	uint16 _playdateGameWidth;
	uint16 _playdateGameOffsetX;

	// Native-resolution background for Playdate: the picture re-rasterized at
	// display resolution (crisp lines, no upscale row-doubling) instead of
	// scaling up the 160x168 buffer. Null until the first picture is decoded.
	byte *_playdatePicture;
	int16 _playdatePicW, _playdatePicH;
	class PictureMgr_Playdate *_playdatePictureMgr;

	// Native-resolution sprite layer, same dimensions as _playdatePicture. One
	// byte per native pixel: 0 = no sprite (show background), 1 = sprite black,
	// 2 = sprite white. Cels are dithered into this at native resolution with a
	// sprite-local pattern phase so their interior is stable as they move, and
	// composited over the crisp background instead of upscaling the mixed
	// 160x168 game screen. Null until the first picture is decoded.
	byte *_playdateSprite;
	int16 _nativeSpriteOriginNX, _nativeSpriteOriginNY; // sprite-local dither anchor

	uint16 _displayFontWidth;
	uint16 _displayFontHeight;

	uint16 _displayWidthMulAdjust;
	uint16 _displayHeightMulAdjust;

	/**
	 * This variable defines, if upscaled hires is active and what upscaled mode
	 * is used.
	 */
	GfxScreenUpscaledMode _upscaledHires;

	bool _priorityTableSet;
	uint8 _priorityTable[SCRIPT_HEIGHT]; /**< priority table */

	MouseCursorData _mouseCursor;
	MouseCursorData _mouseCursorBusy;

	uint16 _renderStartVisualOffsetY;
	uint16 _renderStartDisplayOffsetY;

public:
	uint16 getDisplayScreenWidth() const {
		return _displayScreenWidth;
	}
	uint16 getDisplayFontWidth() const {
		return _displayFontWidth;
	}
	uint16 getDisplayFontHeight() const {
		return _displayFontHeight;
	}

	GfxScreenUpscaledMode getUpscaledHires() const {
		return _upscaledHires;
	}

	void debugShowMap(int mapNr);

	void clear(byte color, byte priority);
	void clearDisplay(byte color, bool copyToScreen = true);
	void putPixel(int16 x, int16 y, byte drawMask, byte color, byte priority);
	void putPixelOnDisplay(int16 x, int16 y, byte color);
	void putPixelOnDisplay(int16 x, int16 adjX, int16 y, int16 adjY, byte color);
	void putFontPixelOnDisplay(int16 baseX, int16 baseY, int16 addX, int16 addY, byte color, bool isHires);

	byte getColor(int16 x, int16 y) const;
	byte getPriority(int16 x, int16 y) const;
	bool checkControlPixel(int16 x, int16 y, byte newPriority) const;

	byte getCGAMixtureColor(byte color) const;

	void render_Block(int16 x, int16 y, int16 width, int16 height, bool copyToScreen = true);

private:
	static bool render_Clip(int16 &x, int16 &y, int16 &width, int16 &height, const int16 minY, const int16 clipAgainstWidth, const int16 clipAgainstHeight);
	void render_BlockEGA(int16 x, int16 y, int16 width, int16 height);
	void render_BlockCGA(int16 x, int16 y, int16 width, int16 height);
	void render_BlockHercules(int16 x, int16 y, int16 width, int16 height);
	void render_BlockPlaydate(int16 x, int16 y, int16 width, int16 height);
	void renderNativePicture();

public:
	// Re-rasterize the given picture into the Playdate native-resolution
	// background buffer. Called by PictureMgr::decodePicture after the normal
	// 160x168 decode when running in Playdate render mode.
	void decodePlaydateNative(int16 resourceNr);

	// --- Playdate native sprite layer ---
	bool hasNativeBackground() const { return _playdatePicture != nullptr; }
	// Anchor subsequent putNativeSpritePixel calls to this cel's top-left game
	// position, so the interior dither phase moves with the sprite (no shimmer).
	void beginNativeSprite(int16 originGameX, int16 originGameY);
	// Composite one visible cel pixel (game coords) into the native sprite layer.
	void putNativeSpritePixel(int16 gameX, int16 gameY, byte color);
	// Mark one game pixel as a black sprite outline in the native sprite layer.
	void putNativeOutlinePixel(int16 gameX, int16 gameY);
	// True if the native sprite layer holds a sprite pixel for this game pixel.
	bool hasNativeSpriteAt(int16 gameX, int16 gameY) const;
	// Clear the native sprite layer for a game-space rectangle (top-left origin).
	void clearNativeSpriteRegion(int16 gameX, int16 gameY, int16 gameW, int16 gameH);
	// Dither background+sprites for a game-space rectangle to the display and
	// push it to the backend. Used in place of render_Block for sprite updates.
	void renderNativeSpriteRegion(int16 gameX, int16 gameY, int16 gameW, int16 gameH);
	// Bake the current game-screen contents of a rectangle into the native
	// background (for add.to.pic, whose views become part of the scenery).
	void bakeNativeBackgroundRegion(int16 gameX, int16 gameY, int16 gameW, int16 gameH);

	void transition_Amiga();
	void transition_AtariSt();

	void block_save(int16 x, int16 y, int16 width, int16 height, byte *bufferPtr) const;
	void block_restore(int16 x, int16 y, int16 width, int16 height, byte *bufferPtr);

	void drawBox(int16 x, int16 y, int16 width, int16 height, byte backgroundColor, byte lineColor);
	void drawDisplayRect(int16 x, int16 y, int16 width, int16 height, byte color, bool copyToScreen = true);
	void drawDisplayRect(int16 x, int16 adjX, int16 y, int16 adjY, int16 width, int16 adjWidth, int16 height, int16 adjHeight, byte color, bool copyToScreen = true);

private:
	void drawDisplayRectEGA(int16 x, int16 y, int16 width, int16 height, byte color);
	void drawDisplayRectCGA(int16 x, int16 y, int16 width, int16 height, byte color);

public:
	void drawDisplayRectPlaydate(int16 x, int16 y, int16 width, int16 height, byte color);
	void drawPlaydateText(int16 x, int16 y, const Common::String &text, bool inverted = false);
	void drawCharacter(int16 row, int16 column, byte character, byte foreground, byte background, bool disabledLook);
	void drawStringOnDisplay(int16 x, int16 y, const char *text, byte foreground, byte background);
	void drawStringOnDisplay(int16 x, int16 adjX, int16 y, int16 adjY, const char *text, byte foregroundColor, byte backgroundColor);
	void drawCharacterOnDisplay(int16 x, int16 y, byte character, byte foreground, byte background, byte transformXOR = 0, byte transformOR = 0);

	void shakeScreen(int16 repeatCount);
	void updateScreen();

	void initPriorityTable();
	static void createDefaultPriorityTable(uint8 *priorityTable);
	void setPriorityTable(int16 priorityBase);
	bool saveLoadWasPriorityTableModified() const;
	int16 saveLoadGetPriority(int16 yPos) const;
	void saveLoadSetPriorityTableModifiedBool(bool wasModified);
	void saveLoadSetPriority(int16 yPos, int16 priority);
	void saveLoadFigureOutPriorityTableModifiedBool();

	int16 priorityToY(int16 priority) const;
	int16 priorityFromY(int16 yPos) const;
};

} // End of namespace Agi

#endif /* AGI_GRAPHICS_H */
