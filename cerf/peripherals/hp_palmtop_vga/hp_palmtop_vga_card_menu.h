#pragma once

#include "../../core/service.h"
#include "../../host/host_widget.h"
#include "../pcmcia/pcmcia_card_catalog.h"

#include <vector>

/* Builds the "HP Palmtop VGA" insert submenu: an Insert action plus inline
   (disabled) notes describing what the card does and where it is verified,
   so the information is visible on menu open without a dialog. */
class HpPalmtopVgaCardMenu : public Service {
public:
    using Service::Service;

    std::vector<WidgetMenuItem> BuildInsertMenu(
        PcmciaCardCatalog::CardInserter inserter);
};
