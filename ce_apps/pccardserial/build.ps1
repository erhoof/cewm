Set-Location $PSScriptRoot
$build  = "$PSScriptRoot/../../tools/build_ce_app.ps1"
$ce2def = "$PSScriptRoot/../cerf_guest/coredll_ce2.def"
$def    = "$PSScriptRoot/../cerf_guest/coredll_byname.def"

& $build -Type exe -Target pccardserial.exe -Arch arm -ObjDir obj_arm `
    -Sources main.cpp -Entry WinMain -Libs coredll `
    -CoreDllDef $ce2def -WceVersion "100" -SubsystemVersion "1.00"

& $build -Type exe -Target pccardserial.exe -Arch mips -MipsIsa mips1 -ObjDir obj_mips1 `
    -Sources main.cpp -Entry WinMain -Libs coredll `
    -CoreDllDef $ce2def -WceVersion "100" -SubsystemVersion "1.00"

& $build -Type exe -Target pccardserial.exe -Arch mips -MipsIsa mips4 -ObjDir obj_mips4 `
    -Sources main.cpp -Entry WinMain -Libs coredll -CoreDllDef $def
