//
//  MixerPort.h
//  The "Maschine MK3 Mixer" MIDI port (Mackie Control).
//
//  The port is published by MK3Display, so that it is a MIDI device of its own with its own
//  name: one driver instance publishes exactly one MIDI device. The mixer logic lives in
//  MK3Driver, which owns the knobs and buttons. Both run in the same process and meet here.
//

#pragma once
#include <stdint.h>
#include <stddef.h>

class IOUserMIDISource;

namespace MixerPort
{
	typedef void (*Receiver)(void* context, const uint32_t* words, size_t numWords);

	// Set by MK3Display while its port exists.
	void SetSource(IOUserMIDISource* source);
	// Set by MK3Driver while it is running.
	void SetReceiver(Receiver receiver, void* context);

	// MK3Driver -> app.
	void Send(uint8_t status, uint8_t d1, uint8_t d2);
	// App -> MK3Driver (called by MK3Display with Universal MIDI Packet words).
	void Deliver(const uint32_t* words, size_t numWords);
}
