param(
    [string]$Version = "2.0.3",
    [string]$BuildDir = "..\build-v2030-release",
    [string]$OutputDir = "outputs\v2.0.3"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot ".." )).Path
$build = (Resolve-Path (Join-Path $PSScriptRoot $BuildDir)).Path
$output = Join-Path $root $OutputDir
$portable = Join-Path $output "LowLatencyCaptureViewer_v$Version`_x64"
$zip = Join-Path $output "LowLatencyCaptureViewer_v$Version`_x64.zip"
$exe = Join-Path $build "LowLatencyCaptureViewer.exe"

if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Release executable not found: $exe"
}
$exeVersion = (Get-Item -LiteralPath $exe).VersionInfo.FileVersion
if ($exeVersion -ne $Version -and $exeVersion -ne "$Version.0") {
    throw "Version mismatch: requested $Version but executable reports $exeVersion"
}

New-Item -ItemType Directory -Force -Path $output | Out-Null
if (Test-Path -LiteralPath $portable) {
    $resolvedPortable = (Resolve-Path -LiteralPath $portable).Path
    $resolvedOutput = (Resolve-Path -LiteralPath $output).Path
    if (-not $resolvedPortable.StartsWith($resolvedOutput + [IO.Path]::DirectorySeparatorChar,
                                           [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a path outside the generated output directory."
    }
    Remove-Item -LiteralPath $resolvedPortable -Recurse -Force
}
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
New-Item -ItemType Directory -Force -Path $portable | Out-Null

Copy-Item -LiteralPath $exe -Destination $portable
& (Join-Path $PSScriptRoot "Copy-PackageDocs.ps1") -SourceRoot $root -DestinationRoot $portable
Copy-Item -LiteralPath (Join-Path $root "third_party\asio\LICENSE.txt") `
    -Destination (Join-Path $portable "ASIO-SDK-LICENSE.txt")
Copy-Item -LiteralPath (Join-Path $root "third_party\asio\HOST-LICENSE.txt") `
    -Destination (Join-Path $portable "ASIO-HOST-LICENSE.txt")
Copy-Item -LiteralPath (Join-Path $root "third_party\pretendard\LICENSE.txt") `
    -Destination (Join-Path $portable "PRETENDARD-LICENSE.txt")
& (Join-Path $PSScriptRoot "Test-PackageDocs.ps1") -PackageRoot $portable | Out-Null
Compress-Archive -Path (Join-Path $portable "*") -DestinationPath $zip -CompressionLevel Optimal
Write-Host "Portable package: $zip"
