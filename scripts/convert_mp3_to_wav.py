"""
convert_mp3_to_wav.py
=====================
Converts every .mp3 under SIME/Sounds/ into a .wav file (16-bit PCM, 44.1kHz,
stereo) using ffmpeg, then deletes the original .mp3 on successful conversion.

Usage (from C:\\sime):
    python scripts/convert_mp3_to_wav.py
    python scripts/convert_mp3_to_wav.py --no-delete         # keep MP3s
    python scripts/convert_mp3_to_wav.py --workers 16        # adjust parallelism
    python scripts/convert_mp3_to_wav.py --dry-run           # preview only

Notes
-----
* Idempotent: if a target .wav already exists with non-zero size, that file is
  skipped (and the .mp3 is deleted, since the conversion has already happened).
* If the conversion fails or produces a 0-byte file, the .mp3 is left in place
  and the failure is logged.
* Uses Python 3 standard library only (subprocess + concurrent.futures).
"""

from __future__ import annotations

import argparse
import concurrent.futures as cf
import os
import subprocess
import sys
from pathlib import Path
from typing import Optional

# --------------------------------------------------------------------------
# Config
# --------------------------------------------------------------------------

SOUNDS_DIR = Path(__file__).resolve().parent.parent / "Sounds"

# Try winget install location first, then PATH
_FFMPEG_CANDIDATES = [
    Path(os.environ.get("LOCALAPPDATA", "")) /
        "Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe/"
        "ffmpeg-8.1-full_build/bin/ffmpeg.exe",
    Path("C:/Program Files/ffmpeg/bin/ffmpeg.exe"),
]


def find_ffmpeg() -> str:
    for candidate in _FFMPEG_CANDIDATES:
        if candidate.is_file():
            return str(candidate)
    # Fall back to PATH lookup
    from shutil import which
    found = which("ffmpeg")
    if found:
        return found
    sys.exit("ERROR: ffmpeg not found. Install via 'winget install Gyan.FFmpeg' "
             "or place ffmpeg.exe on PATH.")


# --------------------------------------------------------------------------
# Worker
# --------------------------------------------------------------------------

def convert_one(args: tuple[str, str, bool, bool]) -> tuple[str, str, Optional[str]]:
    """
    Returns (mp3_path, status, error_msg).
    status is one of: "converted", "skipped", "failed".
    """
    mp3_path, ffmpeg_exe, delete_after, dry_run = args
    src = Path(mp3_path)
    dst = src.with_suffix(".wav")

    if dst.exists() and dst.stat().st_size > 0:
        if delete_after and not dry_run:
            try:
                src.unlink()
            except OSError:
                pass
        return (mp3_path, "skipped", None)

    if dry_run:
        return (mp3_path, "would-convert", None)

    cmd = [
        ffmpeg_exe,
        "-loglevel", "error",
        "-y",
        "-i", str(src),
        "-ar", "44100",
        "-ac", "2",
        "-acodec", "pcm_s16le",
        str(dst),
    ]
    try:
        result = subprocess.run(
            cmd,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            timeout=60,
        )
    except subprocess.TimeoutExpired:
        return (mp3_path, "failed", "ffmpeg timed out")
    except OSError as e:
        return (mp3_path, "failed", f"OSError: {e}")

    if result.returncode != 0 or not dst.exists() or dst.stat().st_size == 0:
        if dst.exists() and dst.stat().st_size == 0:
            try: dst.unlink()
            except OSError: pass
        err = result.stderr.decode("utf-8", errors="replace").strip()[:300] if result.stderr else "unknown"
        return (mp3_path, "failed", err)

    if delete_after:
        try:
            src.unlink()
        except OSError as e:
            return (mp3_path, "converted", f"warning: could not delete mp3: {e}")

    return (mp3_path, "converted", None)


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def collect_mp3s(root: Path) -> list[Path]:
    return [p for p in root.rglob("*.mp3")]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-delete", action="store_true",
                    help="Keep .mp3 files after conversion (default deletes on success)")
    ap.add_argument("--workers", type=int, default=8,
                    help="Number of parallel ffmpeg workers (default 8)")
    ap.add_argument("--dry-run", action="store_true",
                    help="List what would be converted without doing it")
    args = ap.parse_args()

    if not SOUNDS_DIR.is_dir():
        sys.exit(f"ERROR: Sounds folder not found: {SOUNDS_DIR}")

    ffmpeg_exe = find_ffmpeg()
    print(f"ffmpeg: {ffmpeg_exe}")
    print(f"root  : {SOUNDS_DIR}")
    print(f"delete after: {not args.no_delete}")
    print(f"workers     : {args.workers}")
    print(f"dry run     : {args.dry_run}")
    print()

    mp3s = collect_mp3s(SOUNDS_DIR)
    print(f"Found {len(mp3s)} .mp3 files to process.\n")
    if not mp3s:
        return 0

    delete_after = not args.no_delete

    converted = skipped = failed = 0
    failures: list[tuple[str, str]] = []

    work = [(str(p), ffmpeg_exe, delete_after, args.dry_run) for p in mp3s]

    total = len(work)
    progress_step = max(1, total // 50)

    with cf.ProcessPoolExecutor(max_workers=args.workers) as pool:
        for i, (mp3, status, err) in enumerate(pool.map(convert_one, work, chunksize=8), start=1):
            if status in ("converted", "would-convert"):
                converted += 1
            elif status == "skipped":
                skipped += 1
            else:
                failed += 1
                failures.append((mp3, err or ""))

            if i % progress_step == 0 or i == total:
                pct = i * 100 // total
                sys.stdout.write(
                    f"\r[{pct:3d}%] {i}/{total}  "
                    f"converted={converted} skipped={skipped} failed={failed}"
                )
                sys.stdout.flush()

    print("\n")
    print("==== Summary ====")
    print(f"converted: {converted}")
    print(f"skipped  : {skipped}")
    print(f"failed   : {failed}")

    if failures:
        log = SOUNDS_DIR.parent / "conversion_failures.log"
        with log.open("w", encoding="utf-8") as fh:
            for mp3, err in failures:
                fh.write(f"{mp3}\t{err}\n")
        print(f"\nFailure details: {log}")
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
