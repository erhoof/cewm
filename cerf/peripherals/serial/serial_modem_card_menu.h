#pragma once

#include "../../core/service.h"
#include "../../host/host_widget.h"

#include <functional>
#include <vector>

class SerialModemCardMenu : public Service {
public:
    using Service::Service;

    std::vector<WidgetMenuItem> BuildInsertMenu(std::function<void()> on_insert);
};
