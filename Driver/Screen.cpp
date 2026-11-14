//
//  Screen.cpp
//  Frame buffers and drawing for the Maschine MK3's two 480 x 272 displays.
//
//  Transfer format (interface 5, bulk OUT 0x04), one transfer per update:
//    84 00 <display> 60 00 00 00 00, then x, y, width, height (16-bit big-endian)
//    00 <half the pixel count, 24-bit big-endian>, then RGB565 pixels, high byte first
//    03 00 00 00 (show), 40 00 00 00 (end)
//

#include <DriverKit/DriverKit.h>
#include <DriverKit/IOLib.h>
#include <DriverKit/IOBufferMemoryDescriptor.h>
#include <USBDriverKit/USBDriverKit.h>

#include "Screen.h"
#include "Font8x16.h"

namespace Screen
{
	struct Rect { int x0, y0, x1, y1; bool dirty; };

	static uint8_t  gFrame[2][kWidth * kHeight * 2];
	static Rect     gDirty[2];
	static IOLock*  gLock;

	static IOUSBHostPipe*            gPipe;
	static IOBufferMemoryDescriptor* gBuffer;
	static uint8_t*                  gBufferPtr;
	static OSAction*                 gAction;
	static bool                      gBusy;

	void Init()
	{
		if (gLock == nullptr) gLock = IOLockAlloc();
	}

	static void MarkDirty(int display, int x0, int y0, int x1, int y1)
	{
		Rect& r = gDirty[display];
		if (!r.dirty) { r = { x0, y0, x1, y1, true }; return; }
		if (x0 < r.x0) r.x0 = x0;
		if (y0 < r.y0) r.y0 = y0;
		if (x1 > r.x1) r.x1 = x1;
		if (y1 > r.y1) r.y1 = y1;
	}

	// Call with the lock held.
	static void FlushLocked()
	{
		if (gPipe == nullptr || gBusy) return;

		for (int display = 0; display < 2; display++)
		{
			Rect& r = gDirty[display];
			if (!r.dirty) continue;

			// The pixel count is sent halved, so keep the width even.
			int x0 = r.x0 & ~1, x1 = (r.x1 + 1) & ~1, y0 = r.y0, y1 = r.y1;
			if (x1 > kWidth) x1 = kWidth;
			const int w = x1 - x0, h = y1 - y0;
			r.dirty = false;
			if (w <= 0 || h <= 0) continue;

			uint8_t* p = gBufferPtr;
			const uint8_t header[16] = { 0x84, 0, (uint8_t)display, 0x60, 0, 0, 0, 0,
				(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(y0 >> 8), (uint8_t)y0,
				(uint8_t)(w >> 8), (uint8_t)w, (uint8_t)(h >> 8), (uint8_t)h };
			memcpy(p, header, 16);
			const uint32_t half = (uint32_t)(w * h) / 2;
			p[16] = 0; p[17] = (uint8_t)(half >> 16); p[18] = (uint8_t)(half >> 8); p[19] = (uint8_t)half;
			p += 20;
			for (int y = y0; y < y1; y++)
			{
				memcpy(p, &gFrame[display][(y * kWidth + x0) * 2], (size_t)w * 2);
				p += w * 2;
			}
			const uint8_t tail[8] = { 0x03, 0, 0, 0, 0x40, 0, 0, 0 };
			memcpy(p, tail, 8);
			p += 8;

			gBusy = true;
			if (gPipe->AsyncIO(gBuffer, (uint32_t)(p - gBufferPtr), gAction, 0) != kIOReturnSuccess) gBusy = false;
			return;   // one transfer at a time; TransferDone sends the next
		}
	}

	void Attach(IOUSBHostPipe* pipe, IOBufferMemoryDescriptor* buffer, uint8_t* bufferPtr, OSAction* action)
	{
		Init();
		IOLockLock(gLock);
		gPipe = pipe; gBuffer = buffer; gBufferPtr = bufferPtr; gAction = action; gBusy = false;
		// Send everything that has been drawn so far.
		for (int display = 0; display < 2; display++) gDirty[display] = { 0, 0, kWidth, kHeight, true };
		FlushLocked();
		IOLockUnlock(gLock);
	}

	void Detach()
	{
		if (gLock == nullptr) return;
		IOLockLock(gLock);
		gPipe = nullptr; gBuffer = nullptr; gBufferPtr = nullptr; gAction = nullptr; gBusy = false;
		IOLockUnlock(gLock);
	}

	void TransferDone()
	{
		if (gLock == nullptr) return;
		IOLockLock(gLock);
		gBusy = false;
		FlushLocked();
		IOLockUnlock(gLock);
	}

	void Flush()
	{
		if (gLock == nullptr) return;
		IOLockLock(gLock);
		FlushLocked();
		IOLockUnlock(gLock);
	}

	void Fill(int display, int x, int y, int w, int h, uint16_t colour)
	{
		if (gLock == nullptr || display < 0 || display > 1) return;
		int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
		int x1 = x + w > kWidth ? kWidth : x + w, y1 = y + h > kHeight ? kHeight : y + h;
		if (x1 <= x0 || y1 <= y0) return;
		const uint8_t hi = (uint8_t)(colour >> 8), lo = (uint8_t)colour;
		IOLockLock(gLock);
		for (int yy = y0; yy < y1; yy++)
		{
			uint8_t* p = &gFrame[display][(yy * kWidth + x0) * 2];
			for (int xx = x0; xx < x1; xx++) { *p++ = hi; *p++ = lo; }
		}
		MarkDirty(display, x0, y0, x1, y1);
		IOLockUnlock(gLock);
	}

	int TextWidth(const char* text, int scale)
	{
		int n = 0;
		while (text[n] != 0) n++;
		return n * 8 * scale;
	}

	void Text(int display, int x, int y, const char* text, uint16_t fg, uint16_t bg, int scale)
	{
		if (gLock == nullptr || display < 0 || display > 1 || scale < 1) return;
		IOLockLock(gLock);
		int cx = x;
		for (const char* c = text; *c != 0; c++, cx += 8 * scale)
		{
			const uint8_t code = (uint8_t)*c;
			const uint8_t* glyph = kFont8x16[(code >= 32 && code <= 126) ? code - 32 : 0];
			for (int gy = 0; gy < 16 * scale; gy++)
			{
				const int py = y + gy;
				if (py < 0 || py >= kHeight) continue;
				const uint8_t row = glyph[gy / scale];
				for (int gx = 0; gx < 8 * scale; gx++)
				{
					const int px = cx + gx;
					if (px < 0 || px >= kWidth) continue;
					const uint16_t colour = (row & (0x80 >> (gx / scale))) ? fg : bg;
					uint8_t* p = &gFrame[display][(py * kWidth + px) * 2];
					p[0] = (uint8_t)(colour >> 8); p[1] = (uint8_t)colour;
				}
			}
		}
		int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
		int x1 = cx > kWidth ? kWidth : cx, y1 = y + 16 * scale > kHeight ? kHeight : y + 16 * scale;
		if (x1 > x0 && y1 > y0) MarkDirty(display, x0, y0, x1, y1);
		IOLockUnlock(gLock);
	}
}
