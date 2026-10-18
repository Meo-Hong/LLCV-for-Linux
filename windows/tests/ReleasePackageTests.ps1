param(
    [Parameter(Mandatory = $true)][string]$BuildRoot,
    [Parameter(Mandatory = $true)][string]$SourceRoot,
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Configuration = "Release"
)
$ErrorActionPreference = "Stop"
$build = (Resolve-Path -LiteralPath $BuildRoot).Path
$stage = Join-Path $build ("package-test-" + [Guid]::NewGuid().ToString("N"))
$validator = Join-Path $SourceRoot "tools/Test-ReleasePackage.ps1"
try {
    & cmake --install $build --config $Configuration --prefix $stage
    if ($LASTEXITCODE -ne 0) { throw "Release staging failed." }
    & $validator -PackageRoot $stage -Version $Version
    # A missing mux DLL must fail validation, not produce a playback-only release.
    $runtime = Join-Path $stage "avutil-60.dll"
    $backup = Join-Path $stage "avutil-test-backup.bin"
    Move-Item -LiteralPath $runtime -Destination $backup
    try {
        $rejected = $false
        try { & $validator -PackageRoot $stage -Version $Version | Out-Null }
        catch { $rejected = $_.Exception.Message.Contains("avutil-60.dll") }
        if (-not $rejected) { throw "A missing recording runtime was not detected." }
    } finally { Move-Item -LiteralPath $backup -Destination $runtime }
    & $validator -PackageRoot $stage -Version $Version
} finally {
    if (Test-Path -LiteralPath $stage) {
        $resolved = (Resolve-Path -LiteralPath $stage).Path
        if ((Split-Path $resolved -Parent) -ne $build -or
            (Split-Path $resolved -Leaf) -notmatch '^package-test-[0-9a-f]{32}$') {
            throw "Unsafe temporary package path; retained for inspection."
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
