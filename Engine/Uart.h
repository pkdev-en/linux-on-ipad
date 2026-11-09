// Uart.h -- PL011 UART device model (ARM PrimeCell UART at 0x09000000).
// Linux on iPad -- Apache-2.0.
//
// Only what earlycon and a simple driver need today: data register,
// flag register, and the ignore-able control registers. Bytes written by
// the guest go to the tx sink (SwiftUI console / stdout). Bytes pushed by
// the host become guest keyboard input later (M5).
#ifndef LOP_UART_H
#define LOP_UART_H

#include <cstdint>
#include <deque>
#include <functional>

namespace lop {

class Uart {
public:
    using TxSink = std::function<void(uint8_t)>;

    void setTxSink(TxSink sink) { tx_ = std::move(sink); }

    // MMIO interface used by Bus. addr is the offset inside the device page.
    uint32_t read(uint64_t off, unsigned size);
    void write(uint64_t off, uint32_t value, unsigned size);

    // Host pushes a byte into the guest RX FIFO.
    void pushRx(uint8_t byte);

private:
    static constexpr uint64_t kRegionSize = 0x1000;

    // PL011 register offsets.
    enum : uint64_t {
        DR = 0x000, RSR_ECR = 0x004, FR = 0x018, ILPR = 0x020,
        IBRD = 0x024, FBRD = 0x028, LCR_H = 0x02C, CR = 0x030,
        IFLS = 0x034, IMSC = 0x038, RIS = 0x03C, MIS = 0x040,
        ICR = 0x044, DMACR = 0x048,
    };

    // FR bits.
    static constexpr uint32_t FR_BUSY = 1u << 3, FR_RXFE = 1u << 4,
                              FR_TXFF = 1u << 5, FR_RXFF = 1u << 6, FR_TXFE = 1u << 7;

    TxSink tx_;
    std::deque<uint8_t> rx_;

    uint32_t ibrd_ = 0, fbrd_ = 0, lcrH_ = 0, cr_ = 0;
    uint32_t ifls_ = 0, imsc_ = 0, dmacr_ = 0;
};

} // namespace lop

#endif // LOP_UART_H
