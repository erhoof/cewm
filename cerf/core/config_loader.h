#pragma once
#include "service.h"
#include "device_config.h"

class ConfigLoader : public Service {
public:
    using Service::Service;

    /* Populate the given DeviceConfig from the global cerf.json (next to
       cerf.exe), the per-device cerf.json, and the CLI args (read from the
       emulator). Called by DeviceConfig::OnReady. */
    void LoadInto(DeviceConfig& config);

    void ApplyBoardPanelDefault(DeviceConfig& config);

    /* Persist the shutdown dialog's "Save the state" selection to the global
       cerf.json top-level "last_save_state_mode", preserving every other key.
       Called when the dialog's "Remember choice" is set. */
    void SaveLastSaveStateMode(bool save_state);
};
