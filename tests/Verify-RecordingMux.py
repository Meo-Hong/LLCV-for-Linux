"""Run modern-container recovery tests with an independent absolute-time oracle.

The native C++ test also uses MF to read files, whose fragmented MP4 source can
normalize starting timestamps. This wrapper compares the actual mux input
manifest with FFprobe's saved-file PTS/DTS, then fully decodes flash/tone content.
No optional Windows HEVC/AV1 decoder or actual GPU encoder is required.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile


sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    "recording_media_measure", Path(__file__).with_name("Measure-RecordingMuxMedia.py"))
media = importlib.util.module_from_spec(spec)
spec.loader.exec_module(media)


def verify(args, output: Path) -> bool:
    output.mkdir(parents=True, exist_ok=True)
    command = [str(args.test_exe.resolve()), "--fixture", str(args.fixture.resolve()),
               "--output-directory", str(output.resolve())]
    for name, number in (("audio", args.audio_offset_ms), ("video", args.video_offset_ms)):
        if number:
            command += [f"--{name}-offset-ms", str(number)]
    generated = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               timeout=45, check=False)
    log = generated.stdout.decode("utf-8", errors="replace")
    (output / "native-test.log").write_text(log, encoding="utf-8")
    print(log)
    if generated.returncode:
        raise RuntimeError(f"native recovery test failed: {generated.returncode}")
    manifests = list(output.rglob("input-timestamps.json"))
    if len(manifests) != 1:
        raise RuntimeError(f"expected one fresh timestamp manifest, found {len(manifests)}")
    manifest_path = manifests[0]
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))

    # MF's fixture SourceReader presents compressed data with MP4 edit/priming
    # trimming removed. Decode that same physical boundary for content sync.
    # This does NOT claim lossless preservation of an arbitrary imported MP4's
    # edit list. Production records fresh MF encoder output, not imported MP4.
    reference = media.measure(args.ffmpeg, args.ffprobe, args.fixture, ignore_editlist=True)
    if not reference["passed"]:
        raise RuntimeError("independent reference decode failed")
    report = {
        "scope": "Mux input-to-file preservation, including absolute PTS/DTS, decoder integrity, actual flash/tone sync, and forced-exit recovery.",
        "reference_scope": "Physical encoded fixture with edit lists ignored, matching the native MF packets delivered to the sink; not original edited MP4 presentation.",
        "reference": reference, "input_manifest": str(manifest_path), "files": [],
    }
    for name in ("complete.mp4", "terminated.mp4"):
        path = manifest_path.parent / name
        measured = media.measure(args.ffmpeg, args.ffprobe, path)
        if measured["passed"]:
            packets = media.verify_native_packets(args.ffprobe, path, manifest, measured["streams"])
            measured["native_packet_timestamps"] = packets
            measured["passed"] &= packets["passed"]
            baseline = reference["audio_minus_video_ms"]
            observed = measured["audio_minus_video_ms"]
            count = min(len(baseline), len(observed))
            if count < 2:
                raise RuntimeError("insufficient decoded events to verify drift")
            expected_change = args.audio_offset_ms - args.video_offset_ms
            changes = [observed[i] - baseline[i] - expected_change for i in range(count)]
            measured["expected_relative_offset_ms"] = expected_change
            measured["relative_offset_error_ms"] = changes
            measured["maximum_relative_offset_error_ms"] = max(abs(value) for value in changes)
            measured["passed"] &= measured["maximum_relative_offset_error_ms"] <= 2.0
            measured["passed"] &= abs(measured["first_to_last_drift_ms"]) <= 2.0
            if name == "complete.mp4":
                measured["passed"] &= packets["tracks"]["video"]["actual_count"] == len(manifest["video"])
                measured["passed"] &= packets["tracks"]["audio"]["actual_count"] == len(manifest["audio"])
        report["files"].append(measured)
        print(json.dumps({key: measured.get(key) for key in (
            "path", "passed", "strict_decode_error_lines", "decoded_video_frames",
            "median_offset_ms", "first_to_last_drift_ms", "maximum_relative_offset_error_ms",
            "native_packet_timestamps")}, ensure_ascii=False))
    report["passed"] = all(result["passed"] for result in report["files"])
    (output / "independent-media-verification.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return report["passed"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-exe", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--ffprobe", required=True)
    parser.add_argument("--audio-offset-ms", type=int, default=0)
    parser.add_argument("--video-offset-ms", type=int, default=0)
    parser.add_argument("--output-directory", type=Path,
                        help="Optional empty test-owned directory for retaining evidence.")
    args = parser.parse_args()
    if args.output_directory:
        if args.output_directory.exists() and any(args.output_directory.iterdir()):
            parser.error("--output-directory must be new or empty; never reuse stale evidence")
        return 0 if verify(args, args.output_directory) else 1
    with tempfile.TemporaryDirectory(prefix="llcv-independent-mux-") as directory:
        return 0 if verify(args, Path(directory)) else 1


if __name__ == "__main__":
    raise SystemExit(main())
