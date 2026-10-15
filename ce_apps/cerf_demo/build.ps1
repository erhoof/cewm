Set-Location $PSScriptRoot
$build = "$PSScriptRoot/../../tools/build_ce_app.ps1"
$def   = "$PSScriptRoot/../cerf_guest/coredll_byname.def"

& $build -Type exe -Target CerfDemo.exe -Arch arm -ObjDir obj_arm `
    -Sources main.c,desktop.c,process_count.c,guest_stats.c,roms_dialog.c,icon_bitmap.c,tools_list.c,commctrl_init.c -Entry WinMain `
    -Libs coredll -CoreDllDef $def -Rc cerf_demo.rc `
    -LinkExtras "/FIXED:NO"

& $build -Type exe -Target CerfDemo.exe -Arch mips -ObjDir obj_mips `
    -Sources main.c,desktop.c,process_count.c,guest_stats.c,roms_dialog.c,icon_bitmap.c,tools_list.c,commctrl_init.c -Entry WinMain `
    -Libs coredll -CoreDllDef $def -Rc cerf_demo.rc `
    -LinkExtras "/FIXED:NO"
