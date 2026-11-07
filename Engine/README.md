# Engine

Initial C++ engine module. Currently exposes a small C-compatible function for the SwiftUI shell.

Proposed implementation order:
1. Guest memory map and safe memory access
2. ARM64 instruction decoder
3. Interpreter for a small instruction subset
4. Virtual exception/interrupt model
5. MMU and Linux boot protocol
6. JIT only after the interpreter is reliable

The current `EngineInfo.cpp` is a placeholder, not an emulator.
