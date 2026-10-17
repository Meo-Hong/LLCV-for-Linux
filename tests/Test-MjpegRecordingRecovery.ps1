param(
    [Parameter(Mandatory = $true)][string]$Recorder,
    [Parameter(Mandatory = $true)][string]$Probe,
    [ValidateSet('Mjpeg', 'Nv12', 'Yuy2')][string]$InputFormat = 'Mjpeg'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Recorder = (Resolve-Path -LiteralPath $Recorder).Path
$Probe = (Resolve-Path -LiteralPath $Probe).Path
$previousTemp = $env:TEMP
$previousTmp = $env:TMP
$testDirectory = Join-Path ([IO.Path]::GetTempPath()) ('mr-' + [Guid]::NewGuid().ToString('N').Substring(0, 10))
$ownedProcesses = [Collections.Generic.List[Diagnostics.Process]]::new()
$ownsDirectory = $false
$result = 1

function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Start-OwnedProcess([string]$Executable, [string[]]$Arguments, [string]$Name) {
    # These arguments are fixed switches and file names, never shell commands.
    # Windows file names cannot contain a quote; quote all paths with spaces.
    $quoted = @($Arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' })
    $stdout = Join-Path $testDirectory ($Name + '.out')
    $stderr = Join-Path $testDirectory ($Name + '.err')
    $startParameters = @{
        FilePath = $Executable
        PassThru = $true
        WindowStyle = 'Hidden'
        WorkingDirectory = $testDirectory
        RedirectStandardOutput = $stdout
        RedirectStandardError = $stderr
    }
    if ($quoted.Count -gt 0) { $startParameters.ArgumentList = $quoted }
    $process = Start-Process @startParameters
    $ownedProcesses.Add($process)
    return [pscustomobject]@{ Process = $process; Output = $stdout; Error = $stderr }
}

function Run-OwnedTool([string]$Executable, [string[]]$Arguments, [string]$Name) {
    $run = Start-OwnedProcess $Executable $Arguments $Name
    if (-not $run.Process.WaitForExit(20000)) {
        $run.Process.Kill()
        [void]$run.Process.WaitForExit(5000)
        throw "$Name exceeded its 20-second limit"
    }
    if ($run.Process.ExitCode -ne 0) {
        $detail = Get-Content -LiteralPath $run.Error -Raw
        throw "$Name failed ($($run.Process.ExitCode)): $detail"
    }
    return $run
}

function Number([object]$Value) {
    return [Convert]::ToDouble([string]$Value, [Globalization.CultureInfo]::InvariantCulture)
}

function Frame-Time([object]$Frame) {
    $property = $Frame.PSObject.Properties['best_effort_timestamp_time']
    if ($null -eq $property) { $property = $Frame.PSObject.Properties['pts_time'] }
    Require ($null -ne $property) 'Decoded frame is missing its presentation timestamp'
    return Number $property.Value
}

function Verify-Frames([object[]]$Frames, [bool]$Video) {
    Require ($Frames.Count -gt 0) 'Recovered file must decode both video and audio'
    $previous = [double]::NegativeInfinity
    $previousEnd = [double]::NegativeInfinity
    $maximumGap = 0.0
    foreach ($frame in $Frames) {
        $time = Frame-Time $frame
        Require ($time -gt $previous) 'Decoded presentation timestamps must increase'
        if (-not [double]::IsNegativeInfinity($previousEnd)) {
            $gap = $time - $previousEnd
            $maximumGap = [Math]::Max($maximumGap, $gap)
            Require ($gap -lt 0.050) 'Unexpected internal gap in recovered video/audio'
        }
        $duration = if ($Video) { 1.0 / 30.0 } else { (Number $frame.nb_samples) / 48000.0 }
        $previous = $time
        $previousEnd = $time + $duration
    }
    return [pscustomobject]@{ Count = $Frames.Count; MaximumGapMs = 1000.0 * $maximumGap }
}

try {
    [void](New-Item -ItemType Directory -Path $testDirectory -ErrorAction Stop)
    $ownsDirectory = $true
    $env:TEMP = $testDirectory
    $env:TMP = $testDirectory
    $recordingArguments = @()
    if ($InputFormat -eq 'Mjpeg') { $recordingArguments = @('--mjpeg') }
    elseif ($InputFormat -eq 'Yuy2') { $recordingArguments = @('--yuy2') }
    $recording = Start-OwnedProcess -Executable $Recorder -Arguments $recordingArguments -Name 'recorder'
    $startup = [Diagnostics.Stopwatch]::StartNew()
    $files = @()
    while ($startup.ElapsedMilliseconds -lt 10000) {
        $recording.Process.Refresh()
        Require (-not $recording.Process.HasExited) 'Recorder exited before the forced-termination test'
        $files = @(Get-ChildItem -LiteralPath $testDirectory -Filter '*.mp4' -File)
        if ($files.Count -eq 1) { break }
        Start-Sleep -Milliseconds 25
    }
    Require ($files.Count -eq 1) 'Recorder did not create a fresh MP4 in its private TEMP'
    $path = $files[0].FullName

    # Each input records for six seconds. Terminate our own child at four
    # seconds after file creation (not the first buffered disk write), while
    # recording is active, without Stop/Finalize/Shutdown.
    # Completed fragments survive; the unfinished trailing fragment may not.
    $recordingTime = [Diagnostics.Stopwatch]::StartNew()
    while ($recordingTime.ElapsedMilliseconds -lt 4000) {
        $recording.Process.Refresh()
        Require (-not $recording.Process.HasExited) 'Recorder completed or failed before the intended forced exit'
        Start-Sleep -Milliseconds 25
    }
    $recording.Process.Refresh()
    Require (-not $recording.Process.HasExited) 'Recorder must still be alive when killed'
    $recording.Process.Kill()
    Require ($recording.Process.WaitForExit(5000)) 'Owned recorder did not terminate promptly'
    Require ($recording.Process.ExitCode -ne 0) 'Recorder must not finish normally during the recovery test'
    Require ((Get-Item -LiteralPath $path).Length -gt 0) 'Forced exit left no recording bytes'

    $probeRun = Run-OwnedTool $Probe @('-v', 'error', '-count_frames', '-show_streams',
        '-show_frames', '-show_entries',
        'stream=index,codec_type,codec_name,sample_rate,channels,nb_read_frames:frame=media_type,pts_time,best_effort_timestamp_time,nb_samples',
        '-of', 'json', $path) 'probe'
    $probeErrors = Get-Content -LiteralPath $probeRun.Error -Raw
    Require ([string]::IsNullOrWhiteSpace($probeErrors)) "Recovered fragments contain decode errors: $probeErrors"
    $decoded = Get-Content -LiteralPath $probeRun.Output -Raw -Encoding UTF8 | ConvertFrom-Json
    $videoStreams = @($decoded.streams | Where-Object { $_.codec_type -eq 'video' })
    $audioStreams = @($decoded.streams | Where-Object { $_.codec_type -eq 'audio' })
    Require ($videoStreams.Count -eq 1 -and $videoStreams[0].codec_name -eq 'h264') 'Expected recovered H.264 track'
    Require ($audioStreams.Count -eq 1 -and $audioStreams[0].codec_name -eq 'aac' -and
        $audioStreams[0].sample_rate -eq '48000' -and $audioStreams[0].channels -eq 2) 'Expected recovered AAC stereo track'
    $video = @($decoded.frames | Where-Object { $_.media_type -eq 'video' })
    $audio = @($decoded.frames | Where-Object { $_.media_type -eq 'audio' })
    $videoResult = Verify-Frames $video $true
    $audioResult = Verify-Frames $audio $false
    Require ($video.Count -ge 30 -and $audio.Count -ge 20) 'At least one completed second must remain decodable'
    Write-Host ("Recovered {0} recording after forced exit: video={1}, audio={2}; maximum internal gaps {3:F3}/{4:F3} ms." -f
        $InputFormat, $videoResult.Count, $audioResult.Count, $videoResult.MaximumGapMs, $audioResult.MaximumGapMs)

    # ffprobe really decodes frames above, but cannot expose their pixel/PCM
    # values. A matching FFmpeg beside it enables the independent content sync
    # oracle below, including the common one-pulse recovered-prefix case.
    $ffmpeg = Join-Path (Split-Path -Parent $Probe) 'ffmpeg.exe'
    if (Test-Path -LiteralPath $ffmpeg -PathType Leaf) {
        $grayPath = Join-Path $testDirectory 'video.gray'
        $pcmPath = Join-Path $testDirectory 'audio.pcm'
        [void](Run-OwnedTool $ffmpeg @('-nostdin', '-hide_banner', '-loglevel', 'error', '-xerror',
            '-i', $path, '-map', '0:v:0', '-an', '-sn', '-dn', '-vf', 'scale=1:1:flags=area,format=gray',
            '-vsync', '0', '-threads', '1', '-f', 'rawvideo', $grayPath) 'video-decode')
        [void](Run-OwnedTool $ffmpeg @('-nostdin', '-hide_banner', '-loglevel', 'error', '-xerror',
            '-i', $path, '-map', '0:a:0', '-vn', '-sn', '-dn', '-ac', '1', '-ar', '48000',
            '-c:a', 'pcm_s16le', '-f', 's16le', $pcmPath) 'audio-decode')
        $gray = [IO.File]::ReadAllBytes($grayPath)
        $pcm = [IO.File]::ReadAllBytes($pcmPath)
        Require ($gray.Length -eq $video.Count) 'Independent decoded video frame count must match ffprobe'
        $audioSamples = 0
        foreach ($frame in $audio) { $audioSamples += [int]$frame.nb_samples }
        Require ($pcm.Length -eq $audioSamples * 2) 'Independent decoded PCM length must match ffprobe'
        $low = 255
        $high = 0
        foreach ($value in $gray) { $low = [Math]::Min($low, $value); $high = [Math]::Max($high, $value) }
        Require ($high - $low -gt 80) 'Recovered prefix must contain a visible flash'
        $threshold = ($low + $high) / 2.0
        $flashes = [Collections.Generic.List[double]]::new()
        $active = $false
        for ($index = 0; $index -lt $gray.Length; ++$index) {
            $next = $gray[$index] -ge $threshold
            if ($next -and -not $active) { $flashes.Add((Frame-Time $video[$index])) }
            $active = $next
        }

        # One-millisecond PCM RMS windows avoid sine zero crossings. Map each
        # sample offset through ffprobe's decoded AAC frame PTS, so encoder
        # priming or an edit list cannot be hidden by assuming audio starts at 0.
        $rms = [Collections.Generic.List[double]]::new()
        $peak = 0.0
        for ($offset = 0; $offset + 48 -le $audioSamples; $offset += 48) {
            $power = 0.0
            for ($sampleIndex = $offset; $sampleIndex -lt $offset + 48; ++$sampleIndex) {
                $sampleValue = [BitConverter]::ToInt16($pcm, $sampleIndex * 2)
                $power += [double]$sampleValue * $sampleValue
            }
            $value = [Math]::Sqrt($power / 48.0)
            $rms.Add($value)
            $peak = [Math]::Max($peak, $value)
        }
        Require ($peak -gt 100) 'Recovered prefix must contain an audible tone'
        $tones = [Collections.Generic.List[double]]::new()
        $lastActive = [double]::NegativeInfinity
        $frameIndex = 0
        $frameOffset = 0
        for ($index = 0; $index -lt $rms.Count; ++$index) {
            if ($rms[$index] -lt [Math]::Max(64.0, $peak * 0.10)) { continue }
            $offset = $index * 48
            while ($frameIndex + 1 -lt $audio.Count -and
                $offset -ge $frameOffset + [int]$audio[$frameIndex].nb_samples) {
                $frameOffset += [int]$audio[$frameIndex].nb_samples
                ++$frameIndex
            }
            $time = (Frame-Time $audio[$frameIndex]) + ($offset - $frameOffset) / 48000.0
            if ($time - $lastActive -gt 0.040) { $tones.Add($time) }
            $lastActive = $time
        }
        $pairs = [Math]::Min($flashes.Count, $tones.Count)
        Require ($pairs -ge 1) 'Recovered prefix needs at least one flash/tone pair'
        for ($index = 0; $index -lt $pairs; ++$index) {
            $skew = ($tones[$index] - $flashes[$index]) * 1000.0
            Require ([Math]::Abs($skew) -le 40.0) "Recovered content A/V skew exceeds 40 ms: $skew"
            Write-Host ("Recovered flash/tone {0}: audio minus video {1:F3} ms." -f $index, $skew)
        }
    } else {
        Write-Host 'Content sync not measured: ffmpeg.exe is not beside ffprobe. Decoded frame/stream recovery and timing checks passed.'
    }
    $result = 0
} catch {
    Write-Host ('FAIL: ' + $_.Exception.Message)
    if (Test-Path -LiteralPath (Join-Path $testDirectory 'recorder.err')) {
        Get-Content -LiteralPath (Join-Path $testDirectory 'recorder.err') | Write-Host
    }
} finally {
    foreach ($process in $ownedProcesses) {
        try {
            if (-not $process.HasExited) {
                $process.Kill()
                [void]$process.WaitForExit(5000)
            }
        } catch { Write-Host ('Owned child cleanup: ' + $_.Exception.Message) }
        $process.Dispose()
    }
    $env:TEMP = $previousTemp
    $env:TMP = $previousTmp
    if ($ownsDirectory -and (Test-Path -LiteralPath $testDirectory -PathType Container)) {
        # Do not recurse. Only remove files in the exact unique test directory.
        Get-ChildItem -LiteralPath $testDirectory -File -Force | ForEach-Object {
            if ([IO.Path]::GetFullPath($_.DirectoryName) -eq [IO.Path]::GetFullPath($testDirectory)) {
                Remove-Item -LiteralPath $_.FullName -Force
            }
        }
        try { [IO.Directory]::Delete($testDirectory, $false) }
        catch { Write-Host ('Test directory retained: ' + $testDirectory) }
    }
}
exit $result
