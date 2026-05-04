"""
rename_and_index_sounds.py
==========================
Renames every .wav under SIME/Sounds/ into a clean, user-friendly format and
emits a single master metadata CSV (CSV/sound_library.csv) that the application
reads to populate its sound picker UI.

Naming scheme
-------------
    <Instrument>_<Note-or-Variant>_<Duration>_<Dynamic>_<Articulation>.wav

Examples
    violin_A3_025_forte_arco-normal.wav        -> Violin_A3_0.25s_Forte_Arco.wav
    bass-drum__1_fortissimo_struck-singly.wav  -> BassDrum_1s_Fortissimo_StruckSingly.wav
    sleigh-bells__long_forte_shaken.wav        -> SleighBells_Long_Forte_Shaken.wav
    drum_kick_2seconds.wav                     -> Kick_2s.wav
    drum_snare_rightmost_0_5s.wav              -> Snare_Rightmost_0.5s.wav
    AC finger hi.wav                           -> ElectricGuitar_Acoustic_Finger_Hi.wav
    tele chorus hi.wav                         -> ElectricGuitar_Telecaster_Chorus_Hi.wav
    Aether.wav                                 -> ElectricGuitar_Sauce_Aether.wav
    GS_GTR Xmass_Guitar One Shots_ 1_Emaj79    -> Guitar_OneShot_001_Emaj79.wav
    GS_GTR Xmass_Guitar Loops_ 10_100bpm_Gmaj  -> Guitar_Loop_010_Gmaj_100bpm.wav

Usage
-----
    python scripts/rename_and_index_sounds.py --dry-run
    python scripts/rename_and_index_sounds.py
"""

from __future__ import annotations

import argparse
import csv
import re
import shutil
import sys
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

ROOT       = Path(__file__).resolve().parent.parent
SOUNDS_DIR = ROOT / "Sounds"
CSV_DIR    = ROOT / "CSV"
LIBRARY_CSV = CSV_DIR / "sound_library.csv"

# ---------------------------------------------------------------------------
# Vocabulary lookups
# ---------------------------------------------------------------------------

DYNAMIC_MAP = {
    "pianissimo":   "Pianissimo",
    "piano":        "Piano",
    "mezzo-piano":  "MezzoPiano",
    "mezzo-forte":  "MezzoForte",
    "forte":        "Forte",
    "fortissimo":   "Fortissimo",
    "crescendo":    "Crescendo",
    "decrescendo":  "Decrescendo",
}

DURATION_MAP = {
    "025":       "0.25s",
    "05":        "0.5s",
    "075":       "0.75s",
    "1":         "1s",
    "15":        "1.5s",
    "2":         "2s",
    "long":      "Long",
    "very-long": "VeryLong",
    "phrase":    "Phrase",
}

# Folder name -> Block instrument identity
# Maps the source subfolder under Sounds/ to a normalized instrument name that
# becomes the leading token in the renamed file.
INSTRUMENT_FOLDER_MAP = {
    "violin":           "Violin",
    "viola":            "Viola",
    "cello":            "Cello",
    "double bass":      "DoubleBass",
    "banjo":            "Banjo",
    "mandolin":         "Mandolin",
    "guitar":           "Guitar",
    "flute":            "Flute",
    "oboe":             "Oboe",
    "clarinet":         "Clarinet",
    "bass clarinet":    "BassClarinet",
    "bassoon":          "Bassoon",
    "contrabassoon":    "Contrabassoon",
    "cor anglais":      "CorAnglais",
    "french horn":      "FrenchHorn",
    "trumpet":          "Trumpet",
    "trombone":         "Trombone",
    "tuba":             "Tuba",
    "saxophone":        "Saxophone",
}

# Special prefix for a few percussion items where the leading token must stay
# distinct from the parent "Percussion" group.
PERCUSSION_NAME_FIX = {
    "tom-toms":             "TomToms",
    "tam-tam":              "TamTam",
    "snare-drum":           "SnareDrum",
    "bass-drum":            "BassDrum",
    "agogo-bells":          "AgogoBells",
    "banana-shaker":        "BananaShaker",
    "bell-tree":            "BellTree",
    "Chinese-cymbal":       "ChineseCymbal",
    "Chinese-hand-cymbals": "ChineseHandCymbals",
    "clash-cymbals":        "ClashCymbals",
    "cowbell":              "Cowbell",
    "djembe":               "Djembe",
    "djundjun":             "Djundjun",
    "flexatone":            "Flexatone",
    "guiro":                "Guiro",
    "lemon-shaker":         "LemonShaker",
    "motor-horn":           "MotorHorn",
    "ratchet":              "Ratchet",
    "sheeps-toenails":      "SheepsToenails",
    "sizzle-cymbal":        "SizzleCymbal",
    "sleigh-bells":         "SleighBells",
    "spring-coil":          "SpringCoil",
    "squeaker":             "Squeaker",
    "strawberry-shaker":    "StrawberryShaker",
    "surdo":                "Surdo",
    "suspended-cymbal":     "SuspendedCymbal",
    "swanee-whistle":       "SwaneeWhistle",
    "tambourine":           "Tambourine",
    "tenor-drum":           "TenorDrum",
    "Thai-gong":            "ThaiGong",
    "train-whistle":        "TrainWhistle",
    "triangle":             "Triangle",
    "vibraslap":            "Vibraslap",
    "washboard":            "Washboard",
    "whip":                 "Whip",
    "wind-chimes":          "WindChimes",
    "woodblock":            "Woodblock",
    "cabasa":               "Cabasa",
    "castanets":            "Castanets",
}

# Articulation -> friendly title (drops "-normal" / "normal")
def title_articulation(s: str) -> str:
    if not s or s == "normal":
        return ""
    s = s.replace("-normal", "")
    s = s.replace("'", "")     # arco-punta-d'arco -> arco-punta-darco
    parts = re.split(r"[-_ ]+", s)
    return "".join(p[:1].upper() + p[1:] for p in parts if p)

def title_word(s: str) -> str:
    s = s.replace("'", "")
    parts = re.split(r"[-_ ]+", s)
    return "".join(p[:1].upper() + p[1:] for p in parts if p)

NOTE_RE = re.compile(r"^([A-Ga-g])(s)?(\d)$")    # A3, As3, cs4 etc.

def normalize_note(token: str) -> Optional[str]:
    m = NOTE_RE.match(token)
    if not m: return None
    letter, sharp, octave = m.groups()
    return f"{letter.upper()}{'#' if sharp else ''}{octave}"

# ---------------------------------------------------------------------------
# Pattern matchers
# ---------------------------------------------------------------------------

# "violin_A3_025_forte_arco-normal.wav"  -> [instr, note, dur, dyn, art]
# Articulation may be empty (trailing _) or contain apostrophes (arco-punta-d'arco)
PITCHED_RE = re.compile(
    r"^(?P<instr>[a-z][a-z\- ]+?)_(?P<note>[A-Ga-g]s?\d)_(?P<dur>[a-z0-9\-]+)_"
    r"(?P<dyn>[a-z\-]+)_(?P<art>[a-z\-']*)\.wav$"
)

# "bass-drum__1_fortissimo_struck-singly.wav" -> double-underscore means "no note"
PERC_RE = re.compile(
    r"^(?P<instr>[A-Za-z][A-Za-z\- ]*?)__(?P<dur>[a-z0-9\-]+)_"
    r"(?P<dyn>[a-z\-]+)_(?P<tech>[a-zA-Z\-']*)\.wav$"
)

# Drum folder simple names: drum_kick_2seconds.wav, drum_snare_leftmost_2s.wav
DRUM_RE = re.compile(
    r"^drum_(?P<part>[a-z]+)(?:_(?P<extra>[a-z0-9_]+?))?_(?P<dur>\d+(?:_\d+)?(?:s|seconds))\.wav$"
)

# GhostHack One Shot:  "GS_GTR Xmass_Guitar One Shots_ 1_Emaj79.wav"
# GhostHack Loop:      "GS_GTR Xmass_Guitar Loops_ 10_100bpm_Gmaj.wav"
GHOST_ONE_RE  = re.compile(r"^GS_GTR\s+Xmass_Guitar\s+One\s+Shots_\s*(\d+)_(.+)\.wav$", re.IGNORECASE)
GHOST_LOOP_RE = re.compile(r"^GS_GTR\s+Xmass_Guitar\s+Loops_\s*(\d+)_(\d+)bpm_(.+)\.wav$", re.IGNORECASE)

# Marker: file already in our new naming convention (PascalCase first token).
ALREADY_NORMALIZED_RE = re.compile(r"^[A-Z][A-Za-z]+(_[A-Za-z0-9.#]+)+\.wav$")

# ---------------------------------------------------------------------------
# Per-folder naming
# ---------------------------------------------------------------------------

def make_pitched_name(instr_label: str, note: str, dur: str, dyn: str, art: str) -> str:
    parts = [instr_label]
    note_norm = normalize_note(note)
    if note_norm: parts.append(note_norm)
    if dur in DURATION_MAP: parts.append(DURATION_MAP[dur])
    elif dur: parts.append(title_word(dur))
    if dyn in DYNAMIC_MAP: parts.append(DYNAMIC_MAP[dyn])
    art_t = title_articulation(art)
    if art_t: parts.append(art_t)
    return "_".join(parts) + ".wav"

def make_perc_name(instr_label: str, dur: str, dyn: str, tech: str) -> str:
    parts = [instr_label]
    if dur in DURATION_MAP: parts.append(DURATION_MAP[dur])
    elif dur: parts.append(title_word(dur))
    if dyn in DYNAMIC_MAP: parts.append(DYNAMIC_MAP[dyn])
    art_t = title_articulation(tech)
    if art_t: parts.append(art_t)
    return "_".join(parts) + ".wav"

def normalize_dur_token(s: str) -> str:
    # "2seconds" or "2s" or "0_5s" -> "2s" / "0.5s"
    s = s.replace("seconds", "s")
    if "_" in s:
        # "0_5s" -> "0.5s"
        s = s.replace("_", ".")
    return s

# ---------------------------------------------------------------------------
# CSV cache
# ---------------------------------------------------------------------------

class CsvIndex:
    """Look up CSV row by original filename (any extension)."""

    def __init__(self):
        self._rows: dict[str, dict[str, str]] = {}   # key: original filename (lowercase, no ext)
        self._csv_files: list[str] = []

    def load_dir(self, csv_dir: Path) -> None:
        for csv_path in csv_dir.glob("*.csv"):
            if csv_path.name == "sound_library.csv":  # skip our own output
                continue
            self._csv_files.append(csv_path.name)
            with csv_path.open("r", encoding="utf-8", errors="replace", newline="") as fh:
                reader = csv.DictReader(fh)
                for row in reader:
                    fname = (row.get("File") or "").strip()
                    if not fname:
                        continue
                    key = Path(fname).stem.lower()
                    self._rows[key] = {
                        "Artist":  (row.get("Artist") or "").strip(),
                        "Title":   (row.get("Title") or "").strip(),
                        "BPM":     (row.get("BPM") or "").strip(),
                        "Key":     (row.get("Key") or "").strip(),
                        "Camelot": (row.get("Camelot") or "").strip(),
                        "Source":  csv_path.stem,
                    }

    def get(self, original_filename: str) -> dict[str, str]:
        return self._rows.get(Path(original_filename).stem.lower(), {})

# ---------------------------------------------------------------------------
# Block type / instrument category determination
# ---------------------------------------------------------------------------

def classify(rel: Path) -> tuple[str, str]:
    """
    Given a path relative to Sounds/, return (block_type, instrument_label).

    block_type     : enum-like name used by the C++ BlockType
    instrument_label: leading token used in the renamed filename
    """
    parts = rel.parts
    p = "/".join(parts).lower()

    # 1SHOT KIT  ---> ElectricGuitar
    if parts[0].lower().startswith("@rjpasin"):
        sub = parts[1] if len(parts) > 1 else ""
        return "ElectricGuitar", "ElectricGuitar"

    # GhostHack ---> Guitar (acoustic-flavored chord shots / loops)
    if parts[0].lower().startswith("ghosthack"):
        return "Guitar", "Guitar"

    # Instruments/<name>/...
    if parts[0].lower() == "instruments" and len(parts) > 1:
        instr_folder = parts[1].lower()

        # Drum kit
        if instr_folder == "drum":
            return "Drum", "Drum"   # subdir handled in name builder

        # Percussion (the orchestral percussion umbrella)
        if instr_folder == "percussion":
            return "Percussion", "Percussion"   # specific item picked from filename

        if instr_folder in INSTRUMENT_FOLDER_MAP:
            label = INSTRUMENT_FOLDER_MAP[instr_folder]
            return label, label

    return "Unknown", "Unknown"


# ---------------------------------------------------------------------------
# Renaming entry points
# ---------------------------------------------------------------------------

def rename_pitched(path: Path, instr_label: str, csv_idx: CsvIndex) -> Optional[tuple[Path, dict]]:
    name = path.name
    m = PITCHED_RE.match(name)
    if not m:
        return None
    note = m.group("note")
    dur  = m.group("dur")
    dyn  = m.group("dyn")
    art  = m.group("art")
    new_name = make_pitched_name(instr_label, note, dur, dyn, art)
    meta = {
        "instrument":   instr_label,
        "note":         normalize_note(note) or "",
        "duration":     DURATION_MAP.get(dur, title_word(dur)),
        "dynamic":      DYNAMIC_MAP.get(dyn, title_word(dyn)),
        "articulation": title_articulation(art),
    }
    extra = csv_idx.get(name)
    if extra:
        meta["csv_key"]    = extra.get("Key", "")
        meta["csv_bpm"]    = extra.get("BPM", "")
        meta["csv_source"] = extra.get("Source", "")
    return (path.with_name(new_name), meta)


def rename_perc(path: Path, parent_folder_name: str, csv_idx: CsvIndex) -> Optional[tuple[Path, dict]]:
    name = path.name
    m = PERC_RE.match(name)
    if not m:
        return None
    raw_instr = m.group("instr")
    instr_label = PERCUSSION_NAME_FIX.get(raw_instr, title_word(raw_instr))
    dur  = m.group("dur")
    dyn  = m.group("dyn")
    tech = m.group("tech")
    new_name = make_perc_name(instr_label, dur, dyn, tech)
    meta = {
        "instrument":   instr_label,
        "note":         "",
        "duration":     DURATION_MAP.get(dur, title_word(dur)),
        "dynamic":      DYNAMIC_MAP.get(dyn, title_word(dyn)),
        "articulation": title_articulation(tech),
    }
    extra = csv_idx.get(name)
    if extra:
        meta["csv_key"]    = extra.get("Key", "")
        meta["csv_bpm"]    = extra.get("BPM", "")
        meta["csv_source"] = extra.get("Source", "")
    return (path.with_name(new_name), meta)


def rename_drum(path: Path) -> Optional[tuple[Path, dict]]:
    name = path.name
    m = DRUM_RE.match(name)
    if not m:
        return None
    part_raw  = m.group("part")
    extra_raw = m.group("extra") or ""
    dur_raw   = m.group("dur")
    part_pretty = title_word(part_raw)
    extra_pretty = title_word(extra_raw) if extra_raw else ""
    dur_pretty = normalize_dur_token(dur_raw)
    parts = [part_pretty]
    if extra_pretty: parts.append(extra_pretty)
    parts.append(dur_pretty)
    new_name = "_".join(parts) + ".wav"
    meta = {
        "instrument":   part_pretty,
        "note":         "",
        "duration":     dur_pretty,
        "dynamic":      "",
        "articulation": extra_pretty,
    }
    return (path.with_name(new_name), meta)


def rename_ghost(path: Path) -> Optional[tuple[Path, dict]]:
    name = path.name
    m = GHOST_LOOP_RE.match(name)
    if m:
        idx = int(m.group(1))
        bpm = int(m.group(2))
        chord = m.group(3).strip().replace(" ", "")
        new_name = f"Guitar_Loop_{idx:03d}_{chord}_{bpm}bpm.wav"
        return (path.with_name(new_name), {
            "instrument":   "Guitar",
            "note":         "",
            "duration":     "",
            "dynamic":      "",
            "articulation": f"Loop {idx} {chord}",
            "csv_bpm":      str(bpm),
        })
    m = GHOST_ONE_RE.match(name)
    if m:
        idx = int(m.group(1))
        chord = m.group(2).strip().replace(" ", "").replace("?", "Dim")
        new_name = f"Guitar_OneShot_{idx:03d}_{chord}.wav"
        return (path.with_name(new_name), {
            "instrument":   "Guitar",
            "note":         "",
            "duration":     "",
            "dynamic":      "",
            "articulation": f"OneShot {idx} {chord}",
        })
    return None


def rename_1shot(path: Path, sub_folder: str) -> Optional[tuple[Path, dict]]:
    """Free-form 1SHOT KIT names: <descriptors>.wav -> ElectricGuitar_<Sub>_<Descriptors>.wav"""
    name = path.stem
    sub  = title_word(sub_folder.lower())   # lowercase first so "SAUCE" -> "Sauce"
    # Use the entire filename as descriptors
    desc = title_word(name) or "Sample"
    new_name = f"ElectricGuitar_{sub}_{desc}.wav"
    return (path.with_name(new_name), {
        "instrument":   "ElectricGuitar",
        "note":         "",
        "duration":     "",
        "dynamic":      "",
        "articulation": f"{sub} {desc}",
    })


# ---------------------------------------------------------------------------
# Main walk
# ---------------------------------------------------------------------------

def safe_target(target: Path, used: set[str]) -> Path:
    """If target name already taken (by an earlier rename in this run, or a
    pre-existing file), append _2, _3, ..."""
    base = target.stem
    suf  = target.suffix
    out = target
    n = 2
    while str(out).lower() in used or out.exists():
        out = target.with_name(f"{base}_{n}{suf}")
        n += 1
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if not SOUNDS_DIR.is_dir():
        sys.exit(f"Sounds folder not found: {SOUNDS_DIR}")

    csv_idx = CsvIndex()
    csv_idx.load_dir(CSV_DIR)
    print(f"Loaded metadata from {len(csv_idx._csv_files)} CSV files "
          f"({len(csv_idx._rows)} rows).\n")

    wavs = list(SOUNDS_DIR.rglob("*.wav"))
    print(f"Found {len(wavs)} .wav files. dry_run={args.dry_run}\n")

    renamed = skipped = unmatched = 0
    used: set[str] = set()
    library_rows: list[dict] = []

    for src in wavs:
        rel  = src.relative_to(SOUNDS_DIR)
        name = src.name

        if ALREADY_NORMALIZED_RE.match(name):
            skipped += 1
            continue

        block_type, instr_label = classify(rel)
        result: Optional[tuple[Path, dict]] = None

        # 1SHOT KIT — sub-folder defines the variant
        if rel.parts[0].lower().startswith("@rjpasin"):
            sub = rel.parts[1] if len(rel.parts) > 1 else "Misc"
            result = rename_1shot(src, sub)

        # GhostHack
        elif rel.parts[0].lower().startswith("ghosthack"):
            result = rename_ghost(src)

        # Drum kit
        elif rel.parts[0].lower() == "instruments" and len(rel.parts) > 1 and rel.parts[1].lower() == "drum":
            result = rename_drum(src)

        # Percussion (the umbrella with subfolders)
        elif rel.parts[0].lower() == "instruments" and len(rel.parts) > 1 and rel.parts[1].lower() == "percussion":
            result = rename_perc(src, rel.parts[2] if len(rel.parts) > 2 else "", csv_idx)

        # Pitched orchestral (or fallback percussion at root of instrument folder)
        else:
            result = rename_pitched(src, instr_label, csv_idx)
            if not result and instr_label != "Unknown":
                # Some orchestral folders use double-underscore "no note" rows
                result = rename_perc(src, instr_label, csv_idx)

        if not result:
            unmatched += 1
            print(f"  [no-match] {rel}")
            continue

        target_path, meta = result
        target_path = safe_target(target_path, used)
        used.add(str(target_path).lower())

        rel_old = rel.as_posix()
        rel_new = target_path.relative_to(SOUNDS_DIR).as_posix()

        library_rows.append({
            "old_path":     rel_old,
            "new_path":     rel_new,
            "block_type":   block_type,
            "instrument":   meta.get("instrument", ""),
            "note":         meta.get("note", ""),
            "duration":     meta.get("duration", ""),
            "dynamic":      meta.get("dynamic", ""),
            "articulation": meta.get("articulation", ""),
            "key":          meta.get("csv_key", ""),
            "bpm":          meta.get("csv_bpm", ""),
            "csv_source":   meta.get("csv_source", ""),
        })

        if not args.dry_run:
            target_path.parent.mkdir(parents=True, exist_ok=True)
            try:
                src.rename(target_path)
            except OSError:
                shutil.move(str(src), str(target_path))
        renamed += 1

    print(f"\n==== Summary ====")
    print(f"Renamed   : {renamed}")
    print(f"Skipped   : {skipped} (already in new format)")
    print(f"Unmatched : {unmatched}")

    if not args.dry_run:
        CSV_DIR.mkdir(exist_ok=True)
        with LIBRARY_CSV.open("w", encoding="utf-8", newline="") as fh:
            fields = ["new_path", "block_type", "instrument", "note", "duration",
                      "dynamic", "articulation", "key", "bpm", "csv_source", "old_path"]
            w = csv.DictWriter(fh, fieldnames=fields)
            w.writeheader()
            for r in library_rows:
                w.writerow(r)
        print(f"\nLibrary index: {LIBRARY_CSV}  ({len(library_rows)} rows)")
    else:
        # Dry-run preview: every N-th rename so we see variety across folders
        print("\n---- preview (sample across folders) ----")
        step = max(1, len(library_rows) // 60)
        for r in library_rows[::step]:
            print(f"  {r['old_path']:80}  ->  {r['new_path']}")

    return 0 if unmatched == 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
