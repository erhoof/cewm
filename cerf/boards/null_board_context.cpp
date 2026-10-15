#include "board_context.h"

#include "../core/cerf_emulator.h"

namespace {

class NullBoardContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    bool ShouldRegister() override { return true; }

    std::string_view GetBoardFamilyId() const override { return {}; }
};

}

REGISTER_SERVICE_AS_FALLBACK(NullBoardContext, BoardContext);
