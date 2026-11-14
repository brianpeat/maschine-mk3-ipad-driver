//
//  MK3Display.cpp
//  Owns the Maschine MK3's display interface (interface 5 of 17cc:1600): sends the frame
//  buffers kept in Screen.cpp to the two screens, and publishes the "Maschine MK3 Mixer"
//  MIDI port used in Mixer mode (see MixerPort.h for why it lives here).
//

#include <os/log.h>
#include <DriverKit/DriverKit.h>
#include <DriverKit/IOLib.h>
#include <DriverKit/IOBufferMemoryDescriptor.h>
#include <DriverKit/OSCollections.h>
#include <USBDriverKit/USBDriverKit.h>
#include <MIDIDriverKit/MIDIDriverKit.h>

#include "MK3Display.h"
#include "Screen.h"
#include "MixerPort.h"

#define LOG(fmt, ...) os_log(OS_LOG_DEFAULT, "MK3Display: " fmt, ##__VA_ARGS__)

static const uint8_t kDisplayEndpoint = 0x04;

struct MK3Display_IVars
{
	IOUSBHostInterface*       interface;
	IOUSBHostPipe*            pipe;
	IOBufferMemoryDescriptor* buffer;
	OSAction*                 action;
	bool                      stopping;
	bool                      ioRunning;

	OSSharedPtr<IOUserMIDIDevice>      device;
	OSSharedPtr<IOUserMIDIEntity>      entity;
	OSSharedPtr<IOUserMIDISource>      source;
	OSSharedPtr<IOUserMIDIDestination> destination;
};

bool MK3Display::init()
{
	if (!super::init()) return false;
	ivars = IONewZero(MK3Display_IVars, 1);
	return ivars != nullptr;
}

void MK3Display::free()
{
	if (ivars != nullptr)
	{
		OSSafeReleaseNULL(ivars->action);
		OSSafeReleaseNULL(ivars->buffer);
		OSSafeReleaseNULL(ivars->pipe);
		OSSafeReleaseNULL(ivars->interface);
		ivars->source.reset();
		ivars->destination.reset();
		ivars->entity.reset();
		ivars->device.reset();
	}
	IOSafeDeleteNULL(ivars, MK3Display_IVars, 1);
	super::free();
}

kern_return_t IMPL(MK3Display, Start)
{
	kern_return_t ret = Start(provider, SUPERDISPATCH);
	if (ret != kIOReturnSuccess) { LOG("super Start failed 0x%x", ret); return ret; }

	ivars->interface = OSDynamicCast(IOUSBHostInterface, provider);
	if (ivars->interface == nullptr) { LOG("provider is not a USB interface"); return kIOReturnNoDevice; }
	ivars->interface->retain();

	ret = ivars->interface->Open(this, 0, nullptr);
	if (ret != kIOReturnSuccess) { LOG("interface Open failed 0x%x", ret); return ret; }

	ret = ivars->interface->CopyPipe(kDisplayEndpoint, &ivars->pipe);
	if (ret != kIOReturnSuccess) { LOG("CopyPipe failed 0x%x", ret); return ret; }

	// InOut so the driver can fill it (an Out-only buffer is mapped read-only here).
	IOAddressSegment seg = {};
	ret = ivars->interface->CreateIOBuffer(kIOMemoryDirectionInOut, Screen::kTransferCapacity, &ivars->buffer);
	if (ret == kIOReturnSuccess) ret = ivars->buffer->GetAddressRange(&seg);
	if (ret == kIOReturnSuccess) ret = CreateActionWriteComplete(0, &ivars->action);
	if (ret != kIOReturnSuccess) { LOG("buffer setup failed 0x%x", ret); return ret; }

	Screen::Attach(ivars->pipe, ivars->buffer, (uint8_t*)seg.address, ivars->action);

	// The Mixer MIDI port. If any of this fails the screens still work; only Mixer mode is lost.
	auto deviceUID    = OSSharedPtr(OSString::withCString("MaschineMK3-Mixer"), OSNoRetain);
	auto modelUID     = OSSharedPtr(OSString::withCString("Maschine MK3"), OSNoRetain);
	auto manufacturer = OSSharedPtr(OSString::withCString("Native Instruments"), OSNoRetain);
	auto name         = OSSharedPtr(OSString::withCString("Maschine MK3 Mixer"), OSNoRetain);
	ivars->device = IOUserMIDIDevice::Create(this, deviceUID.get(), modelUID.get(), manufacturer.get());
	if (ivars->device.get() != nullptr)
	{
		ivars->device->SetName(name.get());
		ivars->entity = IOUserMIDIEntity::Create(this, ivars->device.get(), name.get(),
												 IOUserMIDIProtocolID::MIDIProtocol_1_0, 1, 1);
	}
	if (ivars->entity.get() != nullptr)
	{
		ivars->source = ivars->entity->GetSource(0);
		ivars->destination = ivars->entity->GetDestination(0);
	}
	if (ivars->source.get() != nullptr && ivars->destination.get() != nullptr)
	{
		ivars->device->AddEntity(ivars->entity.get());
		ivars->source->SetName(name.get());
		ivars->destination->SetName(name.get());
		ivars->source->SetProperty(IOUserMIDIProperty::DisplayName, name.get());
		ivars->destination->SetProperty(IOUserMIDIProperty::DisplayName, name.get());
		auto deviceID = OSSharedPtr(OSNumber::withNumber((uint64_t)0x4D4B3337, 32), OSNoRetain);
		auto entityID = OSSharedPtr(OSNumber::withNumber((uint64_t)0x4D4B3334, 32), OSNoRetain);
		auto sourceID = OSSharedPtr(OSNumber::withNumber((uint64_t)0x4D4B3335, 32), OSNoRetain);
		auto destID   = OSSharedPtr(OSNumber::withNumber((uint64_t)0x4D4B3336, 32), OSNoRetain);
		ivars->device->SetProperty(IOUserMIDIProperty::UniqueID, deviceID.get());
		ivars->entity->SetProperty(IOUserMIDIProperty::UniqueID, entityID.get());
		ivars->source->SetProperty(IOUserMIDIProperty::UniqueID, sourceID.get());
		ivars->destination->SetProperty(IOUserMIDIProperty::UniqueID, destID.get());
		auto offline = OSSharedPtr(OSNumber::withNumber((uint64_t)1, 32), OSNoRetain);
		ivars->device->SetProperty(IOUserMIDIProperty::Offline, offline.get());

		ret = AddObject(ivars->device.get());
		if (ret == kIOReturnSuccess)
		{
			ivars->destination->SetIOBlock(^kern_return_t(IOUserMIDIUMPWord const* umpWords, size_t numWords) {
				MixerPort::Deliver(umpWords, numWords);
				return kIOReturnSuccess;
			});
			MixerPort::SetSource(ivars->source.get());
		}
		else
		{
			LOG("AddObject (mixer port) failed 0x%x", ret);
		}
	}
	else
	{
		LOG("mixer port not created");
	}

	ret = RegisterService();
	if (ret != kIOReturnSuccess) { LOG("RegisterService failed 0x%x", ret); return ret; }

	LOG("started");
	return kIOReturnSuccess;
}

kern_return_t IMPL(MK3Display, Stop)
{
	LOG("stopping");
	ivars->stopping = true;
	Screen::Detach();
	MixerPort::SetSource(nullptr);
	if (ivars->device.get() != nullptr)
	{
		if (ivars->ioRunning) { ivars->ioRunning = false; ivars->device->StopIO(); }
		RemoveObject(ivars->device.get());
	}
	if (ivars->pipe != nullptr) ivars->pipe->Abort(0, kIOReturnAborted, this);
	// The action holds a reference to this object; release it so the driver can be freed.
	OSSafeReleaseNULL(ivars->action);
	OSSafeReleaseNULL(ivars->pipe);
	if (ivars->interface != nullptr) ivars->interface->Close(this, 0);
	kern_return_t ret = Stop(provider, SUPERDISPATCH);
	// The MIDI objects hold a reference back to this driver; drop them here, not in free().
	ivars->source.reset();
	ivars->destination.reset();
	ivars->entity.reset();
	ivars->device.reset();
	return ret;
}

kern_return_t MK3Display::StartIO(OSArray* deviceList)
{
	kern_return_t ret = super::StartIO(deviceList);
	if (ret != kIOReturnSuccess) { LOG("super StartIO failed 0x%x", ret); return ret; }
	if (ivars->device.get() == nullptr) return kIOReturnSuccess;

	ret = ivars->device->StartIO();
	if (ret != kIOReturnSuccess) { LOG("device StartIO failed 0x%x", ret); return ret; }
	auto online = OSSharedPtr(OSNumber::withNumber((uint64_t)0, 32), OSNoRetain);
	ivars->device->SetProperty(IOUserMIDIProperty::Offline, online.get());
	ivars->ioRunning = true;
	return kIOReturnSuccess;
}

kern_return_t MK3Display::StopIO()
{
	if (ivars->device.get() != nullptr && ivars->ioRunning)
	{
		ivars->ioRunning = false;
		auto offline = OSSharedPtr(OSNumber::withNumber((uint64_t)1, 32), OSNoRetain);
		ivars->device->SetProperty(IOUserMIDIProperty::Offline, offline.get());
		ivars->device->StopIO();
	}
	return super::StopIO();
}

void IMPL(MK3Display, WriteComplete)
{
	if (ivars->stopping) return;
	if (status != kIOReturnSuccess && status != kIOReturnAborted) LOG("write status 0x%x", status);
	Screen::TransferDone();
}
