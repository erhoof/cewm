#include "../board_context.h"

#include "nokia_sea_ray_family_id.h"
#include "../../core/cerf_emulator.h"

namespace {

class NokiaSeaRayContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::NokiaSeaRay; }
};

}

REGISTER_SERVICE_AS(NokiaSeaRayContext, BoardContext);
