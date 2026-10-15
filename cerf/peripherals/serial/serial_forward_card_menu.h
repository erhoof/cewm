#pragma once

#include "../../core/service.h"
#include "../../host/host_widget.h"

#include <functional>
#include <string>
#include <vector>

class SerialForwardCardMenu : public Service {
public:
    using Service::Service;

    std::vector<WidgetMenuItem> BuildInsertMenu(
        std::function<void(std::wstring host_port)> on_insert);
};
