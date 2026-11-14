//
//  MixerPort.cpp
//  The "Maschine MK3 Mixer" MIDI port (Mackie Control). See MixerPort.h.
//

#include <DriverKit/DriverKit.h>
#include <DriverKit/IOLib.h>
#include <MIDIDriverKit/MIDIDriverKit.h>

#include "MixerPort.h"

using namespace MIDIDriverKit;

namespace MixerPort
{
	static IOUserMIDISource* gSource;
	static Receiver          gReceiver;
	static void*             gContext;
	static IOLock*           gLock;

	static void Lock()
	{
		if (gLock == nullptr) gLock = IOLockAlloc();
		IOLockLock(gLock);
	}

	void SetSource(IOUserMIDISource* source)
	{
		Lock();
		gSource = source;
		IOLockUnlock(gLock);
	}

	void SetReceiver(Receiver receiver, void* context)
	{
		Lock();
		gReceiver = receiver;
		gContext = context;
		IOLockUnlock(gLock);
	}

	void Send(uint8_t status, uint8_t d1, uint8_t d2)
	{
		Lock();
		if (gSource != nullptr)
		{
			IOUserMIDIUMPWord word = 0x20000000u | ((uint32_t)status << 16) | ((uint32_t)(d1 & 0x7F) << 8) | (d2 & 0x7F);
			gSource->Send(&word, 1);
		}
		IOLockUnlock(gLock);
	}

	void Deliver(const uint32_t* words, size_t numWords)
	{
		Lock();
		const Receiver receiver = gReceiver;
		void* const context = gContext;
		if (receiver != nullptr) receiver(context, words, numWords);
		IOLockUnlock(gLock);
	}
}
