#include "audio_activity_widget.h"

#include "../core/cerf_emulator.h"
#include "host_icon_cache.h"
#include "host_widget_registry.h"

void AudioActivityWidget::NotePresent() {
    if (registered_) return;
    registered_ = true;
    emu_.Get<HostWidgetRegistry>().Register(this);
}

void AudioActivityWidget::DrawIcon(HDC dc, const RECT& box) const {
    emu_.Get<HostIconCache>().DrawCentered(dc, box, L"ICON_SPEAKER");
}

REGISTER_SERVICE(AudioActivityWidget);
