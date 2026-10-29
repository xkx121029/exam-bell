# ExamBell 构建脚本
# 说明：MinGW-w64 的 gcc 会自动链接 default-manifest.o（含一个默认清单），
# 与本项目自带的可视化样式清单冲突（ld 报 "multiple non-default manifests"）。
# 因此链接期间临时移开该文件，链接结束后立刻还原。

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Mingw = 'C:\msys64\mingw64\bin'
$DefaultManifest = 'C:\msys64\mingw64\x86_64-w64-mingw32\lib\default-manifest.o'
$ResObj = Join-Path $env:TEMP 'eb_res.o'

Push-Location $Root
try {
    & "$Mingw\windres.exe" -I src-native -O coff -o $ResObj src-native\exambell.rc
    if ($LASTEXITCODE -ne 0) { throw 'windres 失败' }

    $moved = $false
    if (Test-Path $DefaultManifest) {
        Rename-Item -Path $DefaultManifest -NewName 'default-manifest.o.bak'
        $moved = $true
    }

    try {
        & "$Mingw\g++.exe" -O2 -mwindows -municode -static -static-libgcc -static-libstdc++ -w `
            -o dist\ExamBell.exe src-native\exambell.cpp $ResObj `
            -lwinhttp -lwinmm -lcomctl32 -lgdi32 -luser32
        if ($LASTEXITCODE -ne 0) { throw 'g++ 链接失败' }
    }
    finally {
        if ($moved) { Rename-Item -Path "$DefaultManifest.bak" -NewName 'default-manifest.o' }
    }

    Get-Item dist\ExamBell.exe | Select-Object FullName, Length, LastWriteTime
}
finally {
    Pop-Location
}
