import SwiftUI

struct ContentView: View {
    @State private var engineStatus = "Engine not started"

    var body: some View {
        NavigationStack {
            VStack(spacing: 20) {
                Image(systemName: "desktopcomputer")
                    .font(.system(size: 54))
                Text("LinuxOnPad")
                    .font(.largeTitle.bold())
                Text("Linux ARM64 emulator for iPadOS")
                    .foregroundStyle(.secondary)
                GroupBox("Engine status") {
                    VStack(alignment: .leading, spacing: 10) {
                        Text(engineStatus)
                        Button("Inspect engine") {
                            engineStatus = String(cString: linuxonpad_engine_info())
                        }
                        .buttonStyle(.borderedProminent)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
                GroupBox("Boot") {
                    Text("Linux boot support is not implemented yet.")
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                Spacer()
            }
            .padding()
            .navigationTitle("Home")
        }
    }
}
