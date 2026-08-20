$ErrorActionPreference = 'Stop'

$toolPaths = @(
    (Join-Path $PSScriptRoot 'tmp\win-bin'),
    'D:\tmp\xv6-toolchains\riscv-gcc\xpack-riscv-none-elf-gcc-15.2.0-1\bin',
    'D:\tmp\xv6-toolchains\qemu\xpack-qemu-riscv-9.2.4-1\bin',
    'D:\Git\usr\bin',
    'D:\Git\bin'
)

foreach ($path in $toolPaths) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "xv6 tool path not found: $path"
    }
}

$env:Path = ($toolPaths -join ';') + ';' + $env:Path
$env:SHELL = 'D:\Git\bin\sh.exe'
$env:TOOLPREFIX = 'riscv-none-elf-'

Write-Host 'xv6 Windows environment is ready.' -ForegroundColor Green
Write-Host 'Next: git switch <lab>, then make qemu'
