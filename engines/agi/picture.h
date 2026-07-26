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

#ifndef AGI_PICTURE_H
#define AGI_PICTURE_H

namespace Agi {

#define _DEFAULT_WIDTH      160
#define _DEFAULT_HEIGHT     168

/**
 * AGI picture resource.
 */
struct AgiPicture {
	uint32 flen;            /**< size of raw data */
	uint8 *rdata;           /**< raw vector image data */

	void reset() {
		flen = 0;
		rdata = nullptr;
	}

	AgiPicture() { reset(); }
};

class AgiBase;
class GfxMgr;

class PictureMgr {
public:
	PictureMgr(AgiBase *agi, GfxMgr *gfx);
	virtual ~PictureMgr() { }

	int16 getResourceNr() const { return _resourceNr; };

protected:
	virtual byte getInitialPriorityColor() const { return 4; }

	virtual void putVirtPixel(int16 x, int16 y);
	void xCorner(bool skipOtherCoords = false);
	void yCorner(bool skipOtherCoords = false);
	virtual void plotPattern(byte x, byte y);
	virtual void plotBrush();

	byte getNextByte();
	bool getNextParamByte(byte &b);
	byte getNextNibble();

	virtual bool getNextXCoordinate(byte &x);
	virtual bool getNextYCoordinate(byte &y);
	bool getNextCoordinates(byte &x, byte &y);

	virtual bool getGraphicsCoordinates(int16 &x, int16 &y);

public:
	void decodePicture(int16 resourceNr, bool clearScreen, bool agi256 = false, int16 width = _DEFAULT_WIDTH, int16 height = _DEFAULT_HEIGHT);
	void decodePictureFromBuffer(byte *data, uint32 length, bool clearScreen, int16 width = _DEFAULT_WIDTH, int16 height = _DEFAULT_HEIGHT);

protected:
	virtual void drawPicture();
	void drawPicture_AGI256();

	void draw_SetColor();
	void draw_SetPriority();
	void draw_SetNibbleColor();
	void draw_SetNibblePriority();

	virtual void draw_Line(int16 x1, int16 y1, int16 x2, int16 y2);
	void draw_LineShort();
	void draw_LineAbsolute();

	virtual bool draw_FillCheck(int16 x, int16 y, bool horizontalCheck);
	virtual void draw_Fill(int16 x, int16 y);
	virtual void draw_Fill();

public:
	void showPicture(int16 x = 0, int16 y = 0, int16 width = _DEFAULT_WIDTH, int16 height = _DEFAULT_HEIGHT);
	void showPictureWithTransition();

protected:
	AgiBase *_vm;
	GfxMgr *_gfx;

	int16  _resourceNr;
	uint8 *_data;
	uint32 _dataSize;
	uint32 _dataOffset;
	bool   _dataOffsetNibble;

	uint8 _patCode;
	uint8 _patNum;
	uint8 _priOn;
	uint8 _scrOn;
	uint8 _scrColor;
	uint8 _priColor;

	uint8 _minCommand;

	int16 _width;
	int16 _height;
};

/**
 * Renders an AGI picture directly at the Playdate's display resolution, instead
 * of rasterizing at 160x168 and upscaling. The vector commands are re-run with
 * coordinates scaled to a native buffer, so lines get crisp native-resolution
 * edges and the vertical aspect stretch introduces no duplicated rows (the way
 * Sierra's Mac AGI interpreter fed coordinates to QuickDraw at native res).
 *
 * The 160x168 buffers are left to the base PictureMgr for game logic (priority,
 * sprite occlusion); this only produces the display background, visual-only.
 */
class PictureMgr_Playdate : public PictureMgr {
public:
	PictureMgr_Playdate(AgiBase *agi, GfxMgr *gfx) : PictureMgr(agi, gfx),
		_nbuf(nullptr), _nw(0), _nh(0), _nativePass(kPassStamp), _maskingLine(false) {}

	// Rendering a picture into the native buffer is a three-stage pipeline
	// (see picture.cpp for the rationale):
	//   beginNative     - bind the target buffer, clear the line mask
	//   maskPicture     - record which 160x168 pixels the picture's LINE
	//                     commands own (exact same pixel set as the real
	//                     decode, via the base interpreter)
	//   seedNative      - nearest-neighbour seed of the fills from the decoded
	//                     game screen, with masked (line-owned) pixels
	//                     inpainted from the nearest fill colour so the fat
	//                     upscaled lines do not survive under the crisp ones
	//   stampPicture    - replay the picture, drawing uniform native-
	//                     resolution strokes and brush blocks in command order
	// For overlay pictures, mask BOTH pictures before seeding, then stamp
	// both in order (see GfxMgr::overlayPlaydateNative).
	void beginNative(byte *nbuf, int16 nw, int16 nh);
	void maskPicture(int16 resourceNr);
	void seedNative();
	void stampPicture(int16 resourceNr);

	// Convenience: the full pipeline for a single (whole-screen) picture.
	void decodeToNative(int16 resourceNr, byte *nbuf, int16 nw, int16 nh);

protected:
	void putVirtPixel(int16 x, int16 y) override;
	void draw_Line(int16 x1, int16 y1, int16 x2, int16 y2) override;
	void draw_Fill(int16 x, int16 y) override;
	bool draw_FillCheck(int16 x, int16 y, bool horizontalCheck) override;

private:
	bool setResource(int16 resourceNr);
	void nput(int nx, int ny, byte color);
	byte nget(int nx, int ny) const;
	byte inpaintColor(int16 sx, int16 sy, bool preferRight, bool preferDown) const;

	byte *_nbuf;
	int16 _nw, _nh;

	// Which replay pass drawPicture() is running (stamp is also the mode for
	// one-shot decodes, so it is the default outside maskPicture()).
	enum NativePass { kPassStamp, kPassMask };
	NativePass _nativePass;
	bool _maskingLine; // inside a draw_Line during the mask pass

	// One byte per 160x168 game pixel: nonzero if a LINE command painted it.
	byte _lineMask[_DEFAULT_WIDTH * _DEFAULT_HEIGHT];
};

} // End of namespace Agi

#endif /* AGI_PICTURE_H */
