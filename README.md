# Maschine MK3 iPad Driver

An unofficial DriverKit driver that makes the pads, knobs, buttons, touch strip, lights and screens of a Native Instruments Maschine MK3 work as a MIDI controller and a Mackie Control mixer surface on an iPad.

Plugged into an iPad, the MK3's audio interface and its 5-pin MIDI jacks already work, because those parts are standard USB audio and USB MIDI. The controller itself does nothing: it speaks a private protocol that Native Instruments' desktop software translates into MIDI, and there is no such software on iPadOS. This project is a small iPad app carrying a driver extension that does that translation.

This is not affiliated with, endorsed by, or supported by Native Instruments. "Native Instruments" and "Maschine" are trademarks of Native Instruments GmbH, used here only to say which hardware the driver is for.

## What it does

The driver adds a MIDI port called "Maschine MK3 Controls". On channel 1 unless noted:

| Control | MIDI |
|---|---|
| 16 pads | notes 36-51 (bottom-left pad is 36), velocity from the strike |
| Pad pressure while held | polyphonic aftertouch on the pad's note |
| Knobs 1-8 | controllers 14-21 |
| Big wheel | controller 22, relative: 65 = one click right, 63 = one click left |
| Touch strip, Mod mode (default) | controller 1; keeps its value when released |
| Touch strip, Pitch mode | pitch bend; returns to centre when released |
| Every button | notes 0-70 on channel 16, one per button |

Press the Mod or Pitch button to choose the touch strip mode. The two screens show a column per knob with its controller number, value and a level bar.

### Mixer mode (Mackie Control)

Press the Mixer button to switch Mixer mode on and off. The driver adds a second MIDI port, "Maschine MK3 Mixer"; set it as both input and output of a Mackie Control (MCU) device in the app you want to control.

| Control | Mixer mode |
|---|---|
| Knobs 1-8 | the eight channel faders of the current bank |
| Big wheel | the master fader |
| Screens | channel names and a second line of text sent by the app, with a fader bar per channel and a MAIN bar |
| Play, Stop, Rec | transport, lit to match the app; Restart is rewind |
| Arrow buttons | bank left and right; with Shift, one channel left and right |
| Buttons above the screens | select, solo or mute per channel; press Select, Solo or Mute to choose which |

The pads and the touch strip keep working on the Controls port. Level meters and dB read-outs are not implemented.

Some apps ignore the Mackie Control master fader; Mixing Station appears to. For those there is an option in the MK3 Driver app: in Mixer mode the touch strip and the wheel also send the main fader as controller 23 on the Controls port, which the app can learn. The strip works relatively, so touching it never makes the fader jump; one full swipe covers about three quarters of the range. The driver forgets the option when the Maschine is unplugged, so open the MK3 Driver app after plugging in.

In Mixing Station: Menu → Setup → Midi → New device, protocol Generic Midi, input and output "Maschine MK3 Controls". Select the device, Add Control → Fader, press Learn and slide the strip. Then, on that control, Add action → Fixed channel → Main → LR → Main→Fader.

Lights:

- Buttons with a glowing label are lit at rest and brighten while held.
- Group A-H, the buttons above the screens, the wheel arrows and the pads are dark until pressed.
- Sending notes 36-51 to the port lights the matching pad; the colour follows the velocity.
- Every button flashes once when the driver starts.

## Status

- Confirmed on one MK3 (firmware 1.45) and an M5 iPad Pro on iPadOS 26: pads, knobs, wheel, buttons, touch strip, pad lights, button lights and both screens, with pads and the touch strip playing instruments in Logic Pro for iPad.
- Mixer mode confirmed with Mixing Station and a Behringer X32: knobs move channel faders, the bank buttons page through layers, and the touch strip moves the main fader through the learned controller. Not yet tried with other apps.
- New and not yet tested on hardware: the strip lights following the wheel, and the MAIN bar following the main fader position sent back by the app.
- Installing a new build while the Maschine is connected once crashed the iPad's MIDI service (it restarts by itself). Unplug the Maschine before installing.
- Not handled: the three small volume knobs, level meters, and pad-light colours for anything beyond note on and off.
- Buttons send notes, so in apps that listen to every MIDI channel they will also trigger sounds.
- Play, Stop and Record cannot drive Logic Pro for iPad from here. Logic does not appear to take transport commands over MIDI, and iPadOS does not support the kind of driver that could act as a keyboard. Apps with MIDI learn for transport work.

## Warnings

- This is a hobby project with very little testing. If the iPad misbehaves, unplug the Maschine; the driver only runs while it is connected.
- You can only run it as a development build on your own iPad. Distributing a DriverKit USB driver needs an entitlement from Apple for the device's USB vendor ID, which is Native Instruments'.

## Requirements

- An iPad with an M-series chip, with Developer Mode on.
- A Mac with Xcode 26 or later and [XcodeGen](https://github.com/yonaskolb/XcodeGen).
- A paid Apple Developer Program membership. Development signing for DriverKit works with automatic signing.

## Building

1. Copy `Local.xcconfig.example` to `Local.xcconfig` and put in your team ID and a bundle ID prefix you own.
2. Run `xcodegen generate`.
3. Open `MK3.xcodeproj`, pick the MK3 scheme and your iPad, and run.
4. On the iPad, go to Settings → MK3 Driver → Drivers and switch on the driver.
5. Plug the Maschine into the iPad. After installing a new build, unplug and replug it.

## How it works

The MK3 (USB `17cc:1600`) is a composite device. Interface 4 is a HID interface with one interrupt endpoint in each direction, carrying 64-byte reports. The driver matches that interface by vendor, product and interface number, which outranks the system's generic HID driver, reads the reports with USBDriverKit, and publishes a MIDI device with MIDIDriverKit. HIDDriverKit is not used; it is not available on iPadOS.

Interface 5 carries the two 480 x 272 screens over a bulk endpoint. A second driver class, `MK3Display`, matches it, sends frame buffers kept in `Screen.cpp`, and publishes the Mixer MIDI port. Both classes run in one process (`IOUserServerOneProcess`) and share state directly.

One thing that cost time: a driver instance should publish exactly one MIDI device. Two devices from one instance crashed the system's MIDI service as soon as an app sent to the second one, and two entities in one device show up in some apps under the same name. That is why the Mixer port lives in the display driver.

[NOTES.md](NOTES.md) has the USB layout and report formats. `tools/` has small Mac programs used to work them out: one prints every report the controller sends, one blinks a pad, one draws a test pattern on the screens, and one generates the bitmap font.

## Credits

- Button numbering and LED positions come from [MK3Kit](https://github.com/chaoyi-ai/MK3Kit) (MIT).
- The MIDI driver's start-up and shutdown sequence follows the approach in [ASFireWire](https://github.com/mrmidi/ASFireWire).

## License

Public domain, under [The Unlicense](LICENSE). Take it, change it, ship something better; no credit needed.
