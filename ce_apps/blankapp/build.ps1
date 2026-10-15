Set-Location $PSScriptRoot
# Genuine Windows CE 2.11 (H/PC Pro): _WIN32_WCE=211 + subsystem 2.11. Built for
# both StrongARM SA-11x0 (ARM) and the MIPS R-series H/PC Pro devices.
$build  = "$PSScriptRoot/../../tools/build_ce_app.ps1"
$ce2def = "$PSScriptRoot/../cerf_guest/coredll_ce2.def"
$def    = "$PSScriptRoot/../cerf_guest/coredll_byname.def"
$crt    = "$PSScriptRoot/../cerf_guest/cerf_ce2_crt.cpp"

& $build -Type exe -Target blankapp.exe -Arch arm -ObjDir obj_arm `
    -Sources main.cpp -Entry WinMain -Libs coredll `
    -CoreDllDef $ce2def -WceVersion "211" -SubsystemVersion "2.11"

& $build -Type exe -Target blankapp.exe -Arch mips -MipsIsa mips2 -ObjDir obj_mips2 `
    -Sources @("main.cpp", $crt) -Entry WinMain -Libs coredll `
    -CoreDllDef $ce2def -WceVersion "211" -SubsystemVersion "2.11"

& $build -Type exe -Target blankapp.exe -Arch mips -MipsIsa mips4 -ObjDir obj_mips4 `
    -Sources main.cpp -Entry WinMain -Libs coredll -CoreDllDef $def
