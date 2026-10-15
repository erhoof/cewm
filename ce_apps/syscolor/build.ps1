Set-Location $PSScriptRoot
$build  = "$PSScriptRoot/../../tools/build_ce_app.ps1"
$ce2def = "$PSScriptRoot/../cerf_guest/coredll_ce2.def"
$crt    = "$PSScriptRoot/../cerf_guest/cerf_ce2_crt.cpp"

& $build -Type exe -Target syscolor.exe -Arch arm -ObjDir obj_arm `
    -Sources main.cpp -Entry WinMain -Libs coredll `
    -CoreDllDef $ce2def -WceVersion "100" -SubsystemVersion "1.00"

& $build -Type exe -Target syscolor.exe -Arch mips -MipsIsa mips1 -ObjDir obj_mips1 `
    -Sources @("main.cpp", $crt) -Entry WinMain -Libs coredll `
    -CoreDllDef $ce2def -WceVersion "100" -SubsystemVersion "1.00"
