param(
    [Parameter(Mandatory = $true)][string]$Ffmpeg,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [ValidateSet('av1', 'hevc', 'hevc-bframes', 'hevc-closed-bframes', 'both')][string]$Codec = 'both'
)
$ErrorActionPreference = 'Stop'
$encoder = (Resolve-Path -LiteralPath $Ffmpeg).Path
if (!(Test-Path -LiteralPath $OutputDirectory)) {
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
}
$targetDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
# These original synthetic fixtures exercise the mux without requiring an AV1
# hardware encoder. This script is test tooling, never shipped with the app.
# 12 s, 360 frames, 48 kHz stereo AAC; simultaneous flashes/tones at 1, 3,
# 5, 7, 9, 11 s. The 10 ms audio offset deliberately checks stream timing.
$video = "color=c=black:s=320x192:r=30:d=12,drawbox=color=white:t=fill:enable='gte(mod(t,2),1)*lt(mod(t,2),1.1)'"
$audio = "aevalsrc='0.4*sin(2*PI*1000*t)*gte(mod(t,2),1)*lt(mod(t,2),1.1)':s=48000:d=12:c=stereo"
$codecs = if ($Codec -eq 'both') { @('av1', 'hevc') } else { @($Codec) }
foreach ($selected in $codecs) {
    $bframes = if ($selected -like '*bframes') { 2 } else { 0 }
    $openGop = if ($selected -eq 'hevc-closed-bframes') { 0 } else { 1 }
    $output = Join-Path $targetDirectory "mux-$selected-flash-tone.mp4"
    if (Test-Path -LiteralPath $output) { throw "Refusing to overwrite fixture: $output" }
    $arguments = @('-hide_banner', '-loglevel', 'warning', '-nostdin', '-n',
        '-f', 'lavfi', '-i', $video, '-itsoffset', '0.010', '-f', 'lavfi', '-i', $audio,
        '-map', '0:v:0', '-map', '1:a:0', '-pix_fmt', 'yuv420p', '-g', '30', '-bf', "$bframes")
    if ($selected -eq 'av1') {
        $arguments += @('-c:v', 'libaom-av1', '-cpu-used', '8', '-row-mt', '1',
            '-threads', '2', '-lag-in-frames', '0', '-crf', '35', '-b:v', '0')
    } elseif ($selected -eq 'hevc-closed-bframes') {
        $arguments += @('-c:v', 'hevc_nvenc', '-preset', 'p1', '-cq', '28',
            '-forced-idr', '1', '-no-scenecut', '1', '-strict_gop', '1', '-flags', '+cgop', '-tag:v', 'hvc1')
    } else {
        $arguments += @('-c:v', 'libx265', '-preset', 'ultrafast', '-crf', '28',
            '-x265-params', "pools=1:frame-threads=1:keyint=30:min-keyint=30:bframes=${bframes}:open-gop=${openGop}:scenecut=0:log-level=error",
            '-tag:v', 'hvc1')
    }
    $arguments += @('-c:a', 'aac', '-b:a', '192k', '-ar', '48000', '-ac', '2',
        '-movflags', '+faststart', $output)
    & $encoder @arguments
    if ($LASTEXITCODE -ne 0) { throw "Fixture encode failed ($LASTEXITCODE): $selected" }
    & $encoder -hide_banner -loglevel error -nostdin -i $output -map 0:v:0 -map 0:a:0 -f null -
    if ($LASTEXITCODE -ne 0) { throw "Fixture decode failed ($LASTEXITCODE): $selected" }
    $item = Get-Item -LiteralPath $output
    $hash = Get-FileHash -LiteralPath $output -Algorithm SHA256
    [pscustomobject]@{ Path = $item.FullName; Bytes = $item.Length; SHA256 = $hash.Hash }
}
