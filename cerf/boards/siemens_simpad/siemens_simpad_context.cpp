#include "../board_context.h"

#include "siemens_simpad_family_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class SiemensSimpadContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::SiemensSimpad; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SiemensSimpadContext, BoardContext);
