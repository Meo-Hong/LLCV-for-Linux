[CmdletBinding()]
param(
    [string]$OutputRoot = (Join-Path $PSScriptRoot '../outputs/dependencies/recording-ffmpeg'),
    [ValidateRange(1, 64)][int]$Jobs = 8,
    [switch]$SkipDownload,
    [switch]$SkipConfigure
)

# A mux-only dependency: Media Foundation still performs all encoding.
# GNU make is a build tool only; it is not part of the shipped runtime.
$ErrorActionPreference = 'Stop'
$ffmpegVersion = '8.1.3'
$sourceName = "ffmpeg-$ffmpegVersion.tar.xz"
$sourceHash = '7138D28C96D9D3E3AF4EE3D8CAD72741F8FFB40DA90C1112235DEA3ECD3178A3'
$makeName = 'make-4.4.1-2-x86_64.pkg.tar.zst'
$makeHash = '2408AF61717DAE87B00C855B132769A125C708907FC94A46BB16DAE076113E5C'
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$downloads = Join-Path $OutputRoot 'downloads'
$localTools = Join-Path $OutputRoot 'tools'
$source = Join-Path $OutputRoot "ffmpeg-$ffmpegVersion"
$build = Join-Path $OutputRoot 'build-msvc'
$prefix = Join-Path $OutputRoot 'install'
New-Item -ItemType Directory -Force $OutputRoot, $downloads, $localTools, $build, $prefix | Out-Null

function Get-VerifiedArchive([string]$Name, [string]$Url, [string]$Hash) {
    $path = Join-Path $downloads $Name
    if (-not (Test-Path -LiteralPath $path)) {
        if ($SkipDownload) { throw "Missing archive: $path" }
        & curl.exe --fail --location --retry 2 $Url --output $path
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $Url" }
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $Hash) {
        throw "Unexpected SHA256: $path"
    }
    return $path
}

$sourceArchive = Get-VerifiedArchive $sourceName "https://ffmpeg.org/releases/$sourceName" $sourceHash
$makeArchive = Get-VerifiedArchive $makeName "https://repo.msys2.org/msys/x86_64/$makeName" $makeHash
if (-not (Test-Path -LiteralPath (Join-Path $source 'configure'))) {
    & tar.exe -xf $sourceArchive -C $OutputRoot
    if ($LASTEXITCODE -ne 0) { throw 'Cannot extract FFmpeg source.' }
}
if (-not (Test-Path -LiteralPath (Join-Path $localTools 'usr/bin/make.exe'))) {
    & tar.exe -xf $makeArchive -C $localTools
    if ($LASTEXITCODE -ne 0) { throw 'Cannot extract local GNU make.' }
}

$gitRoot = Split-Path (Split-Path (Get-Command git.exe).Source)
$bash = Join-Path $gitRoot 'bin/bash.exe'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs -or -not (Test-Path -LiteralPath $bash)) { throw 'VS C++ Build Tools and Git Bash are required.' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$priorEnvironment = @{}
Get-ChildItem Env: | ForEach-Object { $priorEnvironment[$_.Name] = $_.Value }
try {
    $vcEnvironment = & cmd.exe /d /s /c ('call "{0}" >nul && set' -f $vcvars)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize MSVC environment.' }
    foreach ($entry in $vcEnvironment) {
        if ($entry -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
    }
    $env:PATH = (Join-Path $localTools 'usr/bin') + ';' + (Join-Path $gitRoot 'usr/bin') + ';' + $env:PATH
    $env:MSYS2_PATH_TYPE = 'inherit'
    function ConvertTo-MsysPath([string]$Path) {
        $value = $Path.Replace('\', '/')
        if ($value -match '^([A-Za-z]):/(.*)$') { return '/' + $matches[1].ToLowerInvariant() + '/' + $matches[2] }
        return $value
    }
    $sourcePosix = ConvertTo-MsysPath $source
    $prefixPosix = ConvertTo-MsysPath $prefix
    $options = @(
        '--toolchain=msvc', '--arch=x86_64', '--target-os=win64',
        '--disable-everything', '--disable-autodetect', '--disable-programs', '--disable-doc',
        '--disable-network', '--disable-avdevice', '--disable-avfilter', '--disable-swscale',
        '--disable-swresample', '--disable-debug', '--disable-asm',
        '--disable-x86asm', '--disable-static', '--enable-shared', '--enable-small',
        '--enable-avutil', '--enable-avcodec', '--enable-avformat', '--enable-muxer=mp4',
        '--enable-bsf=extract_extradata,av1_metadata',
        '--extra-cflags=-MT', '--extra-ldflags=-OPT:REF', "--prefix='$prefixPosix'"
    )
    Push-Location $build
    try {
        if (-not $SkipConfigure) {
            & $bash -c ("'$sourcePosix/configure' " + ($options -join ' '))
            if ($LASTEXITCODE -ne 0) { throw 'FFmpeg configure failed; see build-msvc/ffbuild/config.log.' }
        }
        & $bash -c "make -j$Jobs"
        if ($LASTEXITCODE -ne 0) { throw 'FFmpeg build failed.' }
        & $bash -c 'make install'
        if ($LASTEXITCODE -ne 0) { throw 'FFmpeg install failed.' }
    } finally { Pop-Location }
    # FFmpeg's MSVC install places import libraries beside DLLs; provide the
    # conventional SDK layout expected by CMake without changing the runtime.
    Get-ChildItem (Join-Path $prefix 'bin/*.lib') |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $prefix 'lib') }
    $licenses = Join-Path $prefix 'licenses'
    New-Item -ItemType Directory -Force $licenses | Out-Null
    Copy-Item -LiteralPath (Join-Path $source 'COPYING.LGPLv2.1'), (Join-Path $source 'LICENSE.md') -Destination $licenses
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'recording-ffmpeg-NOTICE.txt') -Destination $licenses
    Copy-Item -LiteralPath (Join-Path $build 'ffbuild/config.mak') -Destination $licenses
    Get-ChildItem (Join-Path $prefix 'bin/*.dll') | Select-Object Name, Length
    Write-Output "Mux-only FFmpeg $ffmpegVersion installed at $prefix"
} finally {
    Get-ChildItem Env: | Where-Object { -not $priorEnvironment.ContainsKey($_.Name) } |
        ForEach-Object { [Environment]::SetEnvironmentVariable($_.Name, $null, 'Process') }
    foreach ($entry in $priorEnvironment.GetEnumerator()) { [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process') }
}
