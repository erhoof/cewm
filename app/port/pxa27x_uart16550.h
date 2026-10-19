#pragma once

#include "socs/pxa2xx/pxa2xx_uart16550.h"

#include "boards/board_context.h"
#include "socs/pxa27x/pxa270_id.h"
#include "core/cerf_emulator.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

class Pxa27xUart16550 : public Pxa2xxUart16550 {
public:
    explicit Pxa27xUart16550(CerfEmulator& emu)
        : Pxa2xxUart16550(emu, Config{
              /* Intel PXA27x Developer's Manual 280000-001 Table 10-8 (page 10-16)
                 IER: bits 31:8 reserved, bits 7:0 DMAE, UUE, NRZE, RTOIE, MIE,
                 RLSE, TIE, RAVIE. */
              .ier_mask = 0xFFu,
              /* Table 10-18 (page 10-30) MCR bit 3 OUT2: "OUT2 connects the UART's
                 interrupt output to the interrupt controller unit. When LOOP is
                 clear: 0 = UART interrupt is disabled." */
              .irq_gate_mcr = 0x08u,
              /* Table 10-8 IER bit 6 UUE: "0 = The unit is disabled." */
              .irq_gate_ier = 0x40u,
              /* Table 10-12 (page 10-21) FCR bits 7:6 ITL: "0b00 = 1 byte or more
                 in FIFO causes interrupt", 0b01 = 8, 0b10 = 16, 0b11 = 32. */
              .rx_trigger = {1u, 8u, 16u, 32u},
              /* Table 10-8 IER bit 4 RTOIE: "Receiver Time-Out Interrupt Enable
                 (Source IIR[TOD])". */
              .cti_ier_bit = 0x10u,
          }) {}

    void SaveState(StateWriter& w) override {
        Pxa2xxUart16550::SaveState(w);
        w.Write("abr", abr_);
    }
    void RestoreState(StateReader& r) override {
        Pxa2xxUart16550::RestoreState(r);
        r.Read("abr", abr_);
    }

    /* The P525 ms2_serial driver stores four characters per 32-bit write to THR (observed on FFUART:
       "ATZS" "0=0E" ...); a short last word carries the tail in its low bytes, so drop trailing zero bytes. */
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t idx = (addr - MmioBase()) / RegStride();
        if (idx == 3u) dlab_ = (value & 0x80u) != 0;
        if (idx == 0u && !dlab_ && value > 0xFFu) {
            int n = 4;
            while (n > 1 && ((value >> (8 * (n - 1))) & 0xFFu) == 0) n--;
            for (int i = 0; i < n; i++) WriteReg8(0, (uint8_t)(value >> (8 * i)));
            return;
        }
        Pxa2xxUart16550::WriteWord(addr, value);
    }

    /* The same driver reads RBR as 32-bit words carrying four received characters whenever FOR shows at least four
       (observed: one read per four bytes, characters stored at 4-byte stride otherwise); fewer than four are read singly. */
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t idx = (addr - MmioBase()) / RegStride();
        if (idx == 0u && !dlab_ && RxAvailable() >= 4) {
            uint32_t v = 0;
            for (int i = 0; i < 4; i++) v |= (uint32_t)ReadReg8(0) << (8 * i);
            return v;
        }
        return Pxa2xxUart16550::ReadWord(addr);
    }

protected:
    // PXA27x UART extensions: ABR +0x28, ACR +0x2c. With no external
    // serial waveform there is no completed auto-baud measurement.
    uint32_t ReadExtReg(uint32_t idx) override {
        /* PXA27x DM 280000-001: FOR (+0x24) holds the number of bytes in the receive FIFO (6 bits). */
        if (idx == 9u) return (uint32_t)std::min<size_t>(RxAvailable(), 63);
        if (idx == 10u) return abr_;
        if (idx == 11u) return 0;
        return Pxa2xxUart16550::ReadExtReg(idx);
    }
    void WriteExtReg(uint32_t idx, uint32_t value) override {
        if (idx == 10u) { abr_ = value & 3u; return; }
        if (idx == 11u) return;
        Pxa2xxUart16550::WriteExtReg(idx, value);
    }
private:
    uint32_t abr_ = 0;
    bool dlab_ = false;
public:
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Pxa270;
    }
};
