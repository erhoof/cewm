#define NOMINMAX
#include "about_transaction.h"

#include "../boards/board_context.h"
#include "../core/cerf_emulator.h"
#include "launcher_transaction.h"

REGISTER_SERVICE(AboutTransaction);

bool AboutTransaction::Open(HWND owner) {
    auto& board = emu_.Get<BoardContext>();

    nlohmann::json query;
    if (!board.GetBoardId().empty()) {
        query["board_name"] = board.BoardName();
        query["soc_name"]   = board.GetSocId().empty() ? "" : board.SocName();
    }

    nlohmann::json response;
    return emu_.Get<LauncherTransaction>().Run(owner, "about", query, response);
}
