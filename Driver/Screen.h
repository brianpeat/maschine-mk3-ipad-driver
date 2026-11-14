//
//  Screen.h
//  Frame buffers and drawing for the Maschine MK3's two 480 x 272 displays.
//
//  The MIDI driver draws into the frame buffers; the display driver (MK3Display) owns the
//  USB pipe. Both live in the same process, so they share this state directly.
//

#pragma once
#include <stdint.h>

class IOUSBHostPipe;
class IOBufferMemoryDescriptor;
class OSAction;

namespace Screen
{
	static const int kWidth  = 480;
	static const int kHeight = 272;
	static const uint32_t kTransferCapacity = 16 + 4 + kWidth * kHeight * 2 + 8;

	static inline uint16_t RGB(int r, int g, int b)
	{
		return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
	}

	void Init();

	// Called by MK3Display when the display interface appears / goes away.
	void Attach(IOUSBHostPipe* pipe, IOBufferMemoryDescriptor* buffer, uint8_t* bufferPtr, OSAction* action);
	void Detach();
	void TransferDone();

	// Drawing. display 0 = left, 1 = right; (0, 0) is the top-left corner.
	void Fill(int display, int x, int y, int w, int h, uint16_t colour);
	void Text(int display, int x, int y, const char* text, uint16_t fg, uint16_t bg, int scale);
	int  TextWidth(const char* text, int scale);

	// Sends whatever has changed since the last flush.
	void Flush();
}
