// Uart.cpp -- minimal PL011 model, see Uart.h.
#include "Uart.h"

namespace lop {

uint32_t Uart::read(uint64_t off, unsigned size) {
    (void)size;
    switch (off) {
    case DR: {
        if (rx_.empty()) return 0;
        uint8_t b = rx_.front();
        rx_.pop_front();
        return b; // error bits in [11:8] stay 0: no line errors in the model
    }
    case FR:
        return FR_TXFE | (rx_.empty() ? FR_RXFE : 0);
    case RSR_ECR: return 0;
    case ILPR: return 0;
    case IBRD: return ibrd_;
    case FBRD: return fbrd_;
    case LCR_H: return lcrH_;
    case CR: return cr_;
    case IFLS: return ifls_;
    case IMSC: return imsc_;
    case RIS: return 0; // no interrupt sources yet (M2: RX interrupt)
    case MIS: return 0;
    case DMACR: return dmacr_;
    default: return 0;
    }
}

void Uart::write(uint64_t off, uint32_t value, unsigned size) {
    (void)size;
    switch (off) {
    case DR:
        if (tx_) tx_(uint8_t(value & 0xFF)); // only the low byte is transmitted
        break;
    case RSR_ECR: break; // writing ECR clears errors: nothing to clear
    case IBRD: ibrd_ = value; break;
    case FBRD: fbrd_ = value; break;
    case LCR_H: lcrH_ = value; break;
    case CR: cr_ = value; break;
    case IFLS: ifls_ = value; break;
    case IMSC: imsc_ = value; break;
    case ICR: break; // interrupt clear: no pending interrupts yet
    case DMACR: dmacr_ = value; break;
    default: break; // unknown register: ignore (keeps Linux probe code happy)
    }
}

void Uart::pushRx(uint8_t byte) {
    if (rx_.size() < 64) rx_.push_back(byte);
}

} // namespace lop
