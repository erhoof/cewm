#pragma once

#include "../../core/service.h"

class PcmciaSlot;

class PcmciaAutoInsert : public Service {
public:
    using Service::Service;

    void InsertDefaultNetworkCard(PcmciaSlot& slot);
    void InsertLaunchCompactFlash(PcmciaSlot& slot);
};
