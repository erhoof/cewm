#pragma once

#include "modal_dialog.h"

enum class CalibWarningChoice { Dismissed, SwitchToStock };

class GuestCalibrationWarningDialog : public ModalDialog {
public:
    using ModalDialog::ModalDialog;

    bool ShouldRegister() override;

    CalibWarningChoice Show();
    void               Dismiss() { PostDismiss(); }

private:
    void BuildControls(HWND hwnd) override;
    void OnPaint(HDC dc) override;
    void OnCommand(int id, int notify) override;

    int  LayoutBody(HDC dc, int width, bool draw);
    void DrawIconPair(HDC dc, const RECT& box, const wchar_t* a,
                      const wchar_t* b);

    int                body_h_ = 0;
    CalibWarningChoice choice_ = CalibWarningChoice::Dismissed;
};
