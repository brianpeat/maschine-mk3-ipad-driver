import SwiftUI
import CoreMIDI

@main
struct MK3App: App {
    var body: some Scene {
        WindowGroup { ContentView() }
    }
}

/// Lists the MIDI devices the iPad sees and shows incoming messages, to check the driver.
final class MIDILink: ObservableObject {
    @Published var sources: [String] = []
    @Published var destinations: [String] = []
    @Published var log: [String] = []
    @Published var status = ""

    /// Driver option: in Mixer mode the touch strip and wheel also send the main fader as a
    /// plain controller (23, channel 1) that mixer apps can learn. The driver forgets it when
    /// the Maschine is unplugged, so it is sent again every time the port appears.
    @Published var mainFaderControl = UserDefaults.standard.bool(forKey: "mainFaderControl") {
        didSet {
            UserDefaults.standard.set(mainFaderControl, forKey: "mainFaderControl")
            applySettings()
        }
    }
    /// True once the driver has echoed the current setting back.
    @Published var driverConfirmed = false
    private var hadControls = false

    private var client = MIDIClientRef()
    private var inPort = MIDIPortRef()
    private var outPort = MIDIPortRef()
    private var connected = Set<MIDIEndpointRef>()
    private var sysex: [UInt8] = []       // touched only on the MIDI thread

    init() {
        var err = MIDIClientCreateWithBlock("MK3 Monitor" as CFString, &client) { [weak self] _ in
            DispatchQueue.main.async { self?.refresh() }
        }
        if err != noErr { status = "MIDI client error \(err)" }
        err = MIDIInputPortCreateWithProtocol(client, "In" as CFString, ._1_0, &inPort) { [weak self] list, _ in
            self?.receive(list)
        }
        if err != noErr { status = "MIDI input port error \(err)" }
        MIDIOutputPortCreate(client, "Out" as CFString, &outPort)
        refresh()
    }

    // MARK: Devices

    private static func name(_ endpoint: MIDIEndpointRef) -> String {
        var value: Unmanaged<CFString>?
        MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &value)
        return (value?.takeRetainedValue() as String?) ?? "(unnamed)"
    }

    private var controlDestinations: [MIDIEndpointRef] {
        (0..<MIDIGetNumberOfDestinations()).map { MIDIGetDestination($0) }
            .filter { MIDILink.name($0).contains("Maschine MK3 Controls") }
    }

    var controlsConnected: Bool { destinations.contains { $0.contains("Maschine MK3 Controls") } }

    func refresh() {
        var names: [String] = []
        for i in 0..<MIDIGetNumberOfSources() {
            let source = MIDIGetSource(i)
            names.append(MIDILink.name(source))
            if !connected.contains(source) {
                MIDIPortConnectSource(inPort, source, nil)
                connected.insert(source)
            }
        }
        sources = names
        destinations = (0..<MIDIGetNumberOfDestinations()).map { MIDILink.name(MIDIGetDestination($0)) }

        let hasControls = controlsConnected
        if hasControls && !hadControls { applySettings() }
        if !hasControls { driverConfirmed = false }
        hadControls = hasControls
    }

    /// Sends the options to the driver: controller 119 on channel 16, 127 = on, 0 = off.
    func applySettings() {
        driverConfirmed = false
        _ = send([0xBF, 119, mainFaderControl ? 127 : 0])
    }

    // MARK: Receiving

    private func receive(_ list: UnsafePointer<MIDIEventList>) {
        var lines: [String] = []
        var messages: [[UInt8]] = []
        for packet in list.unsafeSequence() {
            let count = Int(packet.pointee.wordCount)
            let base = UnsafeRawPointer(packet).advanced(by: MemoryLayout<MIDIEventPacket>.offset(of: \.words) ?? 12)
            let words: [UInt32] = (0..<count).map { base.loadUnaligned(fromByteOffset: $0 * 4, as: UInt32.self) }
            var i = 0
            while i < words.count {
                let word = words[i]
                let type = word >> 28
                if type == 3, i + 1 < words.count {          // 7-bit system exclusive, two words
                    let second = words[i + 1]
                    let form = (word >> 20) & 0xF
                    let n = min(Int((word >> 16) & 0xF), 6)
                    let bytes: [UInt8] = [UInt8((word >> 8) & 0x7F), UInt8(word & 0x7F),
                                          UInt8((second >> 24) & 0x7F), UInt8((second >> 16) & 0x7F),
                                          UInt8((second >> 8) & 0x7F), UInt8(second & 0x7F)]
                    if form == 0 || form == 1 { sysex.removeAll() }
                    sysex.append(contentsOf: bytes.prefix(n))
                    if form == 0 || form == 3 {
                        messages.append(sysex)
                        lines.append("SysEx, \(sysex.count) bytes")
                        sysex.removeAll()
                    }
                    i += 2
                    continue
                }
                var text = String(format: "%08X", word)
                if type == 2 {
                    let statusByte = UInt8((word >> 16) & 0xFF)
                    let d1 = UInt8((word >> 8) & 0x7F)
                    let d2 = UInt8(word & 0x7F)
                    let channel = (statusByte & 0x0F) + 1
                    if statusByte == 0xBF && d1 == 119 {
                        DispatchQueue.main.async { self.driverConfirmed = true }
                    }
                    switch statusByte & 0xF0 {
                    case 0x90: text += d2 > 0 ? "  note on  \(d1)  velocity \(d2)  ch \(channel)" : "  note off \(d1)  ch \(channel)"
                    case 0x80: text += "  note off \(d1)  ch \(channel)"
                    case 0xB0: text += "  control \(d1) = \(d2)  ch \(channel)"
                    case 0xC0: text += "  program \(d1)  ch \(channel)"
                    case 0xE0: text += "  pitch bend  ch \(channel)"
                    default: break
                    }
                }
                lines.append(text)
                i += 1
            }
        }
        DispatchQueue.main.async {
            for message in messages { self.handleSysex(message) }
            self.log.insert(contentsOf: lines.reversed(), at: 0)
            if self.log.count > 40 { self.log.removeLast(self.log.count - 40) }
        }
    }

    private func handleSysex(_ m: [UInt8]) {}

    // MARK: Sending

    private func send(_ bytes: [UInt8]) -> Bool {
        let targets = controlDestinations
        for destination in targets {
            var list = MIDIPacketList()
            let packet = MIDIPacketListInit(&list)
            _ = MIDIPacketListAdd(&list, MemoryLayout<MIDIPacketList>.size, packet, 0, bytes.count, bytes)
            MIDISend(outPort, destination, &list)
        }
        return !targets.isEmpty
    }

    func sendTestNote() {
        guard send([0x90, 36, 100]) else { status = "Maschine MK3 Controls port not found"; return }
        status = "Lit the bottom-left pad for a second"
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) { _ = self.send([0x80, 36, 0]) }
    }
}

struct ContentView: View {
    @StateObject private var link = MIDILink()

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text("MK3 Driver").font(.largeTitle).bold()
                Text("This app carries a driver that turns the Maschine MK3's pads, knobs and buttons into MIDI. Switch it on in Settings → MK3 Driver → Drivers, then plug the Maschine into the iPad.")
                Button("Open Settings") {
                    if let url = URL(string: UIApplication.openSettingsURLString) {
                        UIApplication.shared.open(url)
                    }
                }
                .buttonStyle(.bordered)
                if !link.status.isEmpty { Text(link.status).foregroundStyle(.secondary) }

                Divider()
                options
                Divider()
                midiCheck
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(32)
        }
    }

    @ViewBuilder private var options: some View {
        Text("Options").font(.title2).bold()
        Toggle("In Mixer mode, the touch strip and big wheel control the main fader",
               isOn: Binding(get: { link.mainFaderControl }, set: { link.mainFaderControl = $0 }))
        Text("Slide along the strip to move the fader; touching it does not make it jump. The wheel moves it one click at a time. Both send MIDI controller 23 on the \"Maschine MK3 Controls\" port, which a mixer app can learn and assign to its main fader.")
            .font(.footnote).foregroundStyle(.secondary)
        if !link.controlsConnected {
            Text("Maschine not connected. The setting is sent when it is plugged in while this app is open.")
                .font(.footnote).foregroundStyle(.secondary)
        } else if link.driverConfirmed {
            Text("Driver confirmed: \(link.mainFaderControl ? "on" : "off").").font(.footnote).foregroundStyle(.green)
        } else {
            Text("Waiting for the driver to confirm…").font(.footnote).foregroundStyle(.secondary)
        }
    }

    @ViewBuilder private var midiCheck: some View {
        Text("MIDI check").font(.title2).bold()
        HStack {
            Button("Refresh") { link.refresh() }.buttonStyle(.bordered)
            Button("Light the bottom-left pad") { link.sendTestNote() }.buttonStyle(.bordered)
        }

        Text("MIDI inputs the iPad sees").font(.headline)
        if link.sources.isEmpty { Text("None").foregroundStyle(.secondary) }
        ForEach(Array(link.sources.enumerated()), id: \.offset) { _, name in Text("• \(name)") }

        Text("MIDI outputs the iPad sees").font(.headline)
        if link.destinations.isEmpty { Text("None").foregroundStyle(.secondary) }
        ForEach(Array(link.destinations.enumerated()), id: \.offset) { _, name in Text("• \(name)") }

        Text("Incoming messages (newest first)").font(.headline)
        if link.log.isEmpty { Text("Nothing received yet. Hit a pad or turn a knob.").foregroundStyle(.secondary) }
        ForEach(Array(link.log.enumerated()), id: \.offset) { _, line in
            Text(line).font(.system(.body, design: .monospaced))
        }
    }
}
