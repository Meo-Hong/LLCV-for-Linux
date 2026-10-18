"""Independently decode recording fixtures and measure visible/audible pulse sync.

This test tool never uses Media Foundation packet timestamps as the sync oracle.
FFmpeg decodes both tracks (including AAC priming/edit-list handling). Video
flashes are measured from decoded luminance, tones from 1 ms PCM RMS windows.
It uses only the Python standard library and an explicitly supplied FFmpeg.
"""
from __future__ import annotations

import argparse
import array
import bisect
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys


def run(command: list[str], timeout: int = 60) -> subprocess.CompletedProcess:
    return subprocess.run(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=timeout, check=False)


def text(data: bytes) -> str:
    return data.decode("utf-8", errors="replace")


def decoded_events(ffmpeg: str, path: Path, ignore_editlist: bool = False) -> dict:
    common = [ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "info", "-copyts",
              "-threads", "1"]
    if ignore_editlist:
        common += ["-ignore_editlist", "1"]
    common += ["-i", str(path), "-filter_threads", "1"]
    video = run(common + ["-map", "0:v:0", "-an", "-sn", "-dn", "-vf",
                          "scale=1:1:flags=area,format=gray,showinfo", "-fps_mode",
                          "passthrough", "-c:v", "rawvideo", "-threads", "1",
                          "-f", "rawvideo", "pipe:1"])
    if video.returncode:
        raise RuntimeError("video decode failed: " + text(video.stderr)[-4000:])
    video_times = [float(match.group(1)) for match in re.finditer(
        r"\[Parsed_showinfo_\d+[^\]]*\]\s+n:\s*\d+\s+pts:\s*-?\d+\s+pts_time:([-+\deE.]+)",
        text(video.stderr))]
    if len(video_times) != len(video.stdout) or len(video_times) < 2:
        raise RuntimeError(f"decoded video count mismatch: timestamps={len(video_times)}, pixels={len(video.stdout)}")
    luma = list(video.stdout)
    low, high = min(luma), max(luma)
    if high - low < 80:
        raise RuntimeError("fixture has no sufficiently distinct full-frame flash")
    threshold = low + 0.5 * (high - low)
    flashes = []
    active = False
    for pts, value in zip(video_times, luma):
        now = value >= threshold
        if now and not active:
            flashes.append(pts)
        active = now

    audio = run(common + ["-map", "0:a:0", "-vn", "-sn", "-dn", "-af",
                          "aformat=sample_fmts=s16:channel_layouts=mono,ashowinfo",
                          "-c:a", "pcm_s16le", "-f", "s16le", "pipe:1"])
    if audio.returncode:
        raise RuntimeError("audio decode failed: " + text(audio.stderr)[-4000:])
    frames = []
    sample_count = 0
    for match in re.finditer(
        r"\[Parsed_ashowinfo_\d+[^\]]*\]\s+n:\d+\s+pts:-?\d+\s+pts_time:([-+\deE.]+).*?rate:(\d+)\s+nb_samples:(\d+)",
        text(audio.stderr)):
        pts, rate, count = float(match.group(1)), int(match.group(2)), int(match.group(3))
        frames.append((sample_count, pts, rate, count))
        sample_count += count
    pcm = array.array("h")
    pcm.frombytes(audio.stdout)
    if sys.byteorder != "little":
        pcm.byteswap()
    if not frames or sample_count != len(pcm):
        raise RuntimeError(f"decoded audio count mismatch: timestamps={sample_count}, samples={len(pcm)}")
    rates = {frame[2] for frame in frames}
    if len(rates) != 1:
        raise RuntimeError("sample-rate change inside fixture")
    rate = rates.pop()
    starts = [frame[0] for frame in frames]

    def sample_time(index: int) -> float:
        frame = frames[bisect.bisect_right(starts, index) - 1]
        return frame[1] + (index - frame[0]) / frame[2]

    window = max(1, rate // 1000)
    rms = []
    for offset in range(0, len(pcm) - window + 1, window):
        power = sum(int(value) ** 2 for value in pcm[offset:offset + window]) / window
        rms.append((offset, math.sqrt(power)))
    peak = max(value for _, value in rms)
    if peak < 100:
        raise RuntimeError("fixture has no audible tone above the noise floor")
    tone_threshold = max(64.0, peak * 0.10)
    tones = []
    last_active = -math.inf
    for offset, value in rms:
        if value >= tone_threshold:
            pts = sample_time(offset)
            if pts - last_active > 0.04:
                tones.append(pts)
            last_active = pts
    if len(flashes) < 2 or len(tones) < 2:
        raise RuntimeError(f"too few pulse events: flashes={len(flashes)}, tones={len(tones)}")
    pairs = min(len(flashes), len(tones))
    residuals = [(tones[i] - flashes[i]) * 1000 for i in range(pairs)]
    if any(abs(value) > 250 for value in residuals):
        raise RuntimeError("flash/tone correspondence is ambiguous (>250 ms)")
    gaps = []
    for previous, current in zip(frames, frames[1:]):
        gap_ms = (current[1] - (previous[1] + previous[3] / previous[2])) * 1000
        if abs(gap_ms) > 0.1:
            gaps.append(gap_ms)
    video_steps = [(b - a) * 1000 for a, b in zip(video_times, video_times[1:])]
    return {
        "decoded_video_frames": len(video_times), "decoded_audio_samples": len(pcm),
        "audio_rate": rate, "video_first_pts_seconds": video_times[0],
        "video_last_pts_seconds": video_times[-1], "audio_first_pts_seconds": frames[0][1],
        "audio_last_end_seconds": frames[-1][1] + frames[-1][3] / rate,
        "video_step_min_ms": min(video_steps), "video_step_max_ms": max(video_steps),
        "audio_discontinuities_ms": gaps,
        "video_flash_onsets_seconds": flashes, "audio_tone_onsets_seconds": tones,
        "audio_minus_video_ms": residuals,
        "median_offset_ms": statistics.median(residuals),
        "offset_span_ms": max(residuals) - min(residuals),
        "first_to_last_drift_ms": residuals[-1] - residuals[0],
        "analysis_resolution_ms": window / rate * 1000,
        "audio_rms_threshold": tone_threshold, "video_luma_threshold": threshold,
    }


def measure(ffmpeg: str, ffprobe: str, path: Path, ignore_editlist: bool = False) -> dict:
    # A separate strict decoder pass fails on encoded corruption. Stream-copy
    # packet reads alone would not detect an invalid hvcC/av1C/payload mapping.
    strict_command = [ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-xerror",
                      "-err_detect", "explode", "-threads", "1"]
    if ignore_editlist:
        strict_command += ["-ignore_editlist", "1"]
    strict = run(strict_command + ["-i", str(path), "-map", "0:v:0", "-map", "0:a:0", "-f", "null", os.devnull])
    errors = [line for line in text(strict.stderr).splitlines() if line.strip()]
    probe = run([ffprobe, "-v", "error", "-show_entries",
                 "stream=index,codec_name,start_time,duration,width,height,sample_rate,channels",
                 "-of", "json", str(path)])
    if probe.returncode:
        raise RuntimeError("ffprobe failed: " + text(probe.stderr))
    result = {"path": str(path.resolve()), "reference_edit_list_ignored": ignore_editlist,
              "strict_decode_returncode": strict.returncode,
              "strict_decode_error_lines": len(errors), "strict_decode_errors": errors,
              "streams": json.loads(probe.stdout)["streams"]}
    if strict.returncode or errors:
        result["passed"] = False
        return result
    result.update(decoded_events(ffmpeg, path, ignore_editlist))
    result["passed"] = not result["audio_discontinuities_ms"]
    return result


def verify_native_packets(ffprobe: str, path: Path, manifest: dict, streams: list[dict]) -> dict:
    probe = run([ffprobe, "-v", "error", "-show_packets", "-show_entries",
                 "packet=stream_index,pts_time,dts_time", "-of", "json", str(path)])
    if probe.returncode:
        raise RuntimeError("packet timestamp inspection failed: " + text(probe.stderr))
    packets = json.loads(probe.stdout)["packets"]
    time_base = manifest["timeBase"]
    if not isinstance(time_base, int) or time_base <= 0:
        raise RuntimeError("invalid native timestamp manifest time base")
    result = {"passed": True, "tracks": {}}
    for name, codec_names, tolerance_ms in (("video", {"hevc", "av1"}, 0.001), ("audio", {"aac"}, 0.025)):
        index = next(stream["index"] for stream in streams if stream["codec_name"] in codec_names)
        observed = [packet for packet in packets if packet["stream_index"] == index]
        expected = manifest[name]
        if not observed or len(observed) > len(expected):
            result["tracks"][name] = {"passed": False, "actual_count": len(observed), "expected_count": len(expected)}
            result["passed"] = False
            continue
        maximum_pts = maximum_dts = 0.0
        for source, target in zip(expected, observed):
            maximum_pts = max(maximum_pts, abs(float(target["pts_time"]) - source["pts"] / time_base) * 1000)
            maximum_dts = max(maximum_dts, abs(float(target["dts_time"]) - source["dts"] / time_base) * 1000)
        passed = max(maximum_pts, maximum_dts) <= tolerance_ms
        result["tracks"][name] = {"passed": passed, "actual_count": len(observed), "expected_count": len(expected),
                                  "maximum_pts_error_ms": maximum_pts, "maximum_dts_error_ms": maximum_dts,
                                  "tolerance_ms": tolerance_ms}
        result["passed"] &= passed
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--ffprobe", required=True)
    parser.add_argument("--reference", type=Path,
                        help="Compare actual decoded tone/flash residuals with this original fixture.")
    parser.add_argument("--reference-ignore-editlist", action="store_true",
                        help="Explicitly decode reference without MP4 edits, matching MF's native-packet boundary; not original-file presentation.")
    parser.add_argument("--native-manifest", type=Path,
                        help="Verify actual file PTS/DTS against MF samples delivered to the muxer, including B-frame reordering.")
    parser.add_argument("--expected-offset-ms", type=float,
                        help="Known source audio-minus-video offset, e.g. zero for simultaneous generated flashes/tones.")
    parser.add_argument("--input", action="append", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-relative-offset-ms", type=float, default=2.0)
    parser.add_argument("--max-drift-ms", type=float, default=2.0)
    args = parser.parse_args()
    report = {
        "method": "Independent FFmpeg decode; full-frame luminance flashes; 1 ms PCM RMS tone onset; includes decoder priming/edit handling.",
        "scope": "Synthetic encoded fixture/recording integrity and sync only; not live capture latency or AV1 hardware-encoder validation.",
        "max_relative_offset_ms": args.max_relative_offset_ms,
        "max_drift_ms": args.max_drift_ms,
        "reference_edit_list_ignored": args.reference_ignore_editlist,
        "expected_offset_ms": args.expected_offset_ms,
        "reference": None, "files": [],
    }
    if args.reference:
        report["reference"] = measure(args.ffmpeg, args.ffprobe, args.reference, args.reference_ignore_editlist)
        if not report["reference"]["passed"]:
            raise RuntimeError("reference fixture failed independent decode")
    for path in args.input:
        try:
            result = measure(args.ffmpeg, args.ffprobe, path)
            if result["passed"]:
                result["passed"] = abs(result["first_to_last_drift_ms"]) <= args.max_drift_ms
                result["absolute_sync_checked"] = bool(report["reference"] or args.expected_offset_ms is not None)
                if args.expected_offset_ms is not None:
                    result["maximum_expected_offset_error_ms"] = max(abs(value - args.expected_offset_ms) for value in result["audio_minus_video_ms"])
                    result["passed"] &= result["maximum_expected_offset_error_ms"] <= args.max_relative_offset_ms
                if report["reference"]:
                    baseline = report["reference"]["audio_minus_video_ms"]
                    observed = result["audio_minus_video_ms"]
                    count = min(len(baseline), len(observed))
                    delta = [observed[i] - baseline[i] for i in range(count)]
                    result["relative_offset_ms"] = delta
                    result["maximum_relative_offset_ms"] = max(abs(value) for value in delta)
                    result["passed"] &= result["maximum_relative_offset_ms"] <= args.max_relative_offset_ms
                if args.native_manifest:
                    manifest = json.loads(args.native_manifest.read_text(encoding="utf-8-sig"))
                    result["native_packet_timestamps"] = verify_native_packets(args.ffprobe, path, manifest, result["streams"])
                    result["passed"] &= result["native_packet_timestamps"]["passed"]
            report["files"].append(result)
        except Exception as error:
            report["files"].append({"path": str(path), "passed": False, "error": str(error)})
    report["passed"] = all(result["passed"] for result in report["files"])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for result in report["files"]:
        print(json.dumps({key: result.get(key) for key in (
            "path", "passed", "strict_decode_error_lines", "decoded_video_frames",
            "median_offset_ms", "first_to_last_drift_ms", "maximum_relative_offset_ms", "error")},
            ensure_ascii=False))
    print(f"report: {args.output}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
