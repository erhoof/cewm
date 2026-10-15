#pragma once

#include "../../core/service.h"
#include "../../host/host_widget.h"
#include "../pcmcia/pcmcia_card_catalog.h"

#include <cstdint>
#include <string>
#include <vector>

/* Builds the CompactFlash "Insert card" submenu: Choose image / Choose
   default (CF.IMG) / Generate FAT32 into CF.IMG. Each action resolves a
   host image path and hands a CompactFlashCard to the slot's inserter. */
class CompactFlashMenu : public Service {
public:
    using Service::Service;

    std::vector<WidgetMenuItem> BuildInsertMenu(
        PcmciaCardCatalog::CardInserter inserter);

private:
    std::wstring CfImgPath() const;                  /* CF.IMG next to cerf.exe */
    std::wstring ChooseImageFile() const;            /* open dialog, one file */
    std::vector<std::wstring> ChooseFilesToGenerate() const;  /* multi-select */

    /* Sum of the on-disk sizes of files, rounded up to whole MiB. */
    uint32_t PayloadMb(const std::vector<std::wstring>& files) const;
    /* Modal "card size in MiB" prompt, pre-filled with and floored to
       min_mb. Returns the chosen size, or 0 if the user cancelled. */
    uint32_t PromptSizeMb(uint32_t min_mb) const;
};
