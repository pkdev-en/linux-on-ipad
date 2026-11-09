import SwiftUI

/// M1: the engine runs bare-metal ARM64 code and talks through a PL011 UART.
/// The selftest button boots the embedded payload (tests/selftest.S) and shows
/// the guest's UART output. Picking a distro (M2/M4) will download an arm64
/// Image + rootfs and boot it the same way -- the picker below is the entry
/// point for that flow.
struct ContentView: View {
    @State private var engineStatus = "Engine ready (M1: ARM64 interpreter + PL011 UART)"
    @State private var consoleText = ""
    @State private var isRunning = false
    @State private var lastRunOK: Bool?
    @State private var selectedDistro = "Alpine"

    private let distros = ["Alpine", "Ubuntu", "Debian", "Arch"]

    var body: some View {
        NavigationStack {
            List {
                Section("Engine") {
                    Label(engineStatus, systemImage: "cpu")
                        .font(.footnote)
                    Button {
                        runSelftest()
                    } label: {
                        Label(isRunning ? "Running…" : "Run engine selftest (bare-metal hello)",
                              systemImage: "play.fill")
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(isRunning)
                    if let ok = lastRunOK {
                        Label(ok ? "SELFTEST PASS" : "SELFTEST FAIL",
                              systemImage: ok ? "checkmark.seal.fill" : "xmark.seal.fill")
                            .foregroundStyle(ok ? .green : .red)
                            .font(.footnote.bold())
                    }
                }

                Section("Guest console (PL011 @ 0x09000000)") {
                    ScrollView {
                        Text(consoleText.isEmpty ? "— no output yet —" : consoleText)
                            .font(.system(.caption, design: .monospaced))
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .textSelection(.enabled)
                    }
                    .frame(minHeight: 160)
                    .listRowBackground(Color(.systemBackground))
                }

                Section {
                    Picker("Distro", selection: $selectedDistro) {
                        ForEach(distros, id: \.self) { Text($0) }
                    }
                    .pickerStyle(.segmented)
                    Button {
                        // M2/M4: download the arm64 kernel Image + rootfs for the
                        // selected distro, then boot it with the same engine.
                        engineStatus = "Booting \(selectedDistro) needs the M2/M4 boot pipeline (kernel Image + rootfs download)"
                    } label: {
                        Label("Boot \(selectedDistro)", systemImage: "arrow.down.circle")
                    }
                    .disabled(true)
                } header: {
                    Text("Choose a Linux")
                } footer: {
                    Text("Alpine (~60 MB) lands first with the M2 kernel boot; Ubuntu/Debian desktop images follow with virtio + framebuffer (M4/M5).")
                }

                Section("Roadmap") {
                    Label("M1 · interpreter + UART — done", systemImage: "checkmark.circle.fill")
                        .foregroundStyle(.green)
                    Label("M2 · boot Linux kernel (earlycon)", systemImage: "circle")
                    Label("M3 · MMU + interrupts", systemImage: "circle")
                    Label("M4 · virtio-blk/console, real shell", systemImage: "circle")
                    Label("M5 · framebuffer → Metal, GUI desktop", systemImage: "circle")
                    Label("M6 · JIT translation", systemImage: "circle")
                }
                .font(.footnote)
            }
            .navigationTitle("LinuxOnPad")
        }
    }

    private func runSelftest() {
        isRunning = true
        consoleText = ""
        lastRunOK = nil
        DispatchQueue.global(qos: .userInitiated).async {
            let cap = 4096
            let buf = UnsafeMutablePointer<CChar>.allocate(capacity: cap)
            defer { buf.deallocate() }
            let rc = lop_selftest(buf, cap)
            let out = String(cString: buf)
            DispatchQueue.main.async {
                consoleText = out
                lastRunOK = (rc == 0)
                engineStatus = rc == 0
                    ? "Selftest passed — interpreter, UART and memory model verified"
                    : "Selftest FAILED — see console output"
                isRunning = false
            }
        }
    }
}
