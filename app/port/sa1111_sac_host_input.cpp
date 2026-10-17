// Replaces cerf/cerf/peripherals/intel_sa1111/sa1111_sac_host_input.cpp: no host microphone in this port, the guest records silence.
#include "sa1111_sac_host_input.h"
#include "boards/board_context.h"
#include "boards/hp_jornada_700/hp_jornada_700_family_id.h"
#include "core/cerf_emulator.h"

#include <cstring>

bool Sa1111SacHostInput::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
}
void Sa1111SacHostInput::OnReady() {}
void Sa1111SacHostInput::OnShutdown() {}
void Sa1111SacHostInput::SetRate(uint64_t, uint64_t) {}
void Sa1111SacHostInput::Take(uint8_t* dst, uint32_t words) { std::memset(dst, 0, 4u * words); }
void Sa1111SacHostInput::Clear() {}
REGISTER_SERVICE(Sa1111SacHostInput);
