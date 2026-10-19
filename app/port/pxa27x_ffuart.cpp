#include "pxa27x_uart16550.h"
#include "p525_modem.h"

namespace {

/* Intel PXA27x Developer's Manual 280000-001 Table 28-7 (page 28-12): "UART1 -
   Full Function UART 0x4010_0000". Table 25-2 (page 25-5): "IP[22] FFUART
   Transmit or receive error in FFUART 22". */
class Pxa27xFfuart : public Pxa27xUart16550 {
public:
    using Pxa27xUart16550::Pxa27xUart16550;

    void OnReady() override {
        Pxa27xUart16550::OnReady();
        if (auto* bd = emu_.TryGet<BoardContext>(); bd && bd->GetBoardFamilyId() == "asus_p525") {
            modem_.Attach(emu_, *this);
            SetEndpoint(&modem_);
            modem_attached_ = true;
        }
    }
    uint32_t MmioBase() const override { return 0x40100000u; }
    void SaveState(StateWriter& w) override {
        Pxa27xUart16550::SaveState(w);
        if (modem_attached_) modem_.SaveLinkState(w);
    }
    void RestoreState(StateReader& r) override {
        Pxa27xUart16550::RestoreState(r);
        if (modem_attached_) modem_.RestoreLinkState(r);
    }

protected:
    P525Modem modem_;
    bool modem_attached_ = false;
    uint32_t    IntcBit() const override { return 22u; }
    const char* Name()    const override { return "FFUART"; }
};

}  /* namespace */

REGISTER_SERVICE(Pxa27xFfuart);
