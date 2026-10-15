Set-Location $PSScriptRoot
$build = "$PSScriptRoot/../../tools/build_ce_app.ps1"
$def   = "$PSScriptRoot/../cerf_guest/coredll_byname.def"
& $build -Type exe -Target sampleapp.exe -Arch arm -ObjDir obj_arm `
    -Sources main.c -Entry WinMain -Libs coredll,commctrl,commdlg -CoreDllDef $def
& $build -Type exe -Target sampleapp.exe -Arch mips -ObjDir obj_mips `
    -Sources main.c -Entry WinMain -Libs coredll,commctrl,commdlg -CoreDllDef $def
