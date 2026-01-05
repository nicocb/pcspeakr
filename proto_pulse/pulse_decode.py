#!/usr/bin/env python3
"""
Pulse decoder for PC speaker audio files.

This script analyzes raw PCM audio captured from PC speaker output (square waves)
and extracts frequency/duration pairs suitable for playback on Arduino or ESP32.

Algorithm (from the original Perl script):
1. Binarize the waveform (threshold at 40 for signed 8-bit)
2. Detect rising edges (0 -> 1 transitions)
3. Measure period between consecutive edges = frequency
4. Convert frequency to musical note (semitones from 55 Hz base)
5. Group consecutive cycles with same note value
6. Output frequency (Hz) and duration (ms) arrays

Input: Raw PCM file (44.1 kHz, signed 8-bit, mono)
       To create from WAV: sox input.wav -r 44100 -c 1 -b 8 -e signed-integer output.pcm
       Or with ffmpeg: ffmpeg -i input.wav -f s8 -ar 44100 -ac 1 output.pcm

Output: C arrays for Arduino/ESP32 (frequencies and durations)
"""

from __future__ import annotations

import argparse
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Note:
    """A single note with frequency and duration."""
    freq_hz: int
    dur_ms: int


def load_pcm_signed8(path: Path) -> list[int]:
    """
    Load raw PCM file as signed 8-bit samples.

    Returns list of sample values in range [-128, 127].
    """
    data = path.read_bytes()
    # Unpack as signed 8-bit ('b' = signed char)
    samples = list(struct.unpack(f'{len(data)}b', data))
    return samples


def binarize_threshold(samples: list[int], threshold: int = 40) -> list[int]:
    """
    Binarize using absolute threshold (original Perl method).

    Good for clean PC speaker captures with consistent amplitude.
    Values < threshold become 0, else 1.

    Args:
        samples: Signed 8-bit samples
        threshold: Absolute level (default 40, original Perl used 96)
    """
    return [0 if s < threshold else 1 for s in samples]


def binarize_edge_detection(
    samples: list[int],
    window_size: int = 5,
    min_delta: int = 30,
) -> list[int]:
    """
    Binarize using slope-based edge detection (for re-recorded/degraded square waves).

    Detects transitions based on amplitude range in window:
    - delta = max - min in window
    - If delta >= min_delta, there's an edge
    - Rising edge: max comes after min
    - Falling edge: min comes after max

    Edge position = sample with most distant neighbors (max |s[j+1] - s[j-1]|),
    searched only between min and max positions.

    Output is 1 after rising edge, 0 after falling edge.

    Args:
        samples: Signed 8-bit samples
        window_size: Samples to analyze for edge detection (default 5)
        min_delta: Minimum amplitude range to count as edge (default 30)
    """
    if len(samples) < window_size:
        return [0] * len(samples)

    binary = [0] * len(samples)
    state = 0  # Current output state (0 or 1)

    i = 0
    while i <= len(samples) - window_size:
        window = samples[i:i + window_size]
        min_val = min(window)
        max_val = max(window)
        delta = max_val - min_val

        if delta >= min_delta:
            # Find positions of min and max in window
            min_idx = i + window.index(min_val)
            max_idx = i + window.index(max_val)

            # Determine edge type based on order
            if max_idx > min_idx:
                # Rising edge: min comes first, then max
                new_state = 1
                search_start = min_idx
                search_end = max_idx
            else:
                # Falling edge: max comes first, then min
                new_state = 0
                search_start = max_idx
                search_end = min_idx

            # Find sample with most distant neighbors between min and max
            best_idx = search_start
            best_slope = 0
            for j in range(search_start, search_end + 1):
                if j > 0 and j < len(samples) - 1:
                    slope = abs(samples[j + 1] - samples[j - 1])
                    if slope > best_slope:
                        best_slope = slope
                        best_idx = j

            # Fill binary from current position to edge
            for k in range(i, best_idx):
                binary[k] = state

            state = new_state
            print(f"debug : Edge {new_state} detected at {best_idx}")

            # Fill dead zone with new state to avoid detecting same edge twice
            dead_zone_end = min(best_idx + window_size, len(samples))
            for k in range(best_idx, dead_zone_end):
                binary[k] = state
            i = dead_zone_end
        else:
            binary[i] = state
            i += 1

    # Fill remaining with final state
    for j in range(i, len(binary)):
        binary[j] = state

    # Backfill from first edge
    first_edge = next((idx for idx, b in enumerate(binary) if b == 1), len(binary))
    for j in range(first_edge):
        binary[j] = 0

    return binary


def binarize_samples(
    samples: list[int],
    threshold: int = 40,
    method: str = "edge",
    window_size: int = 5,
) -> list[int]:
    """
    Step 1: Convert samples to binary (0 or 1).

    Args:
        samples: Signed 8-bit samples
        threshold: For 'threshold' method: absolute level
                   For 'edge' method: minimum delta to detect edge
        method: 'threshold' (original) or 'edge' (for degraded signals)
        window_size: Window size for edge detection (default 5)

    Returns:
        Binary samples (0 or 1)
    """
    if method == "threshold":
        return binarize_threshold(samples, threshold)
    elif method == "edge":
        return binarize_edge_detection(samples, window_size, threshold)
    else:
        raise ValueError(f"Unknown binarization method: {method}")


def find_rising_edges(binary_samples: list[int]) -> list[int]:
    """
    Step 2: Find all rising edge locations (0 -> 1 transitions).

    A rising edge marks the start of a new cycle in the square wave.
    The distance between consecutive rising edges = one period.

    Returns list of sample indices where rising edges occur.
    """
    edges = [0]  # Start with position 0 as reference
    last_value = 0

    for i, sample in enumerate(binary_samples):
        if last_value == 0 and sample == 1:
            edges.append(i)
            print(f"debug : Rising edge found at {i}")
        last_value = sample

    return edges


def edges_to_note_values(
    edges: list[int],
    sample_rate: int = 44100,
    speed_multiplier: float = 1.0,
    base_freq: float = 55.0,
) -> list[int]:
    """
    Step 3: Convert each cycle period to a musical note value.

    For each pair of consecutive edges:
    - period = edge[i+1] - edge[i] (in samples)
    - frequency = sample_rate / period
    - note = 12 * log2(frequency / base_freq)

    The note value is in semitones relative to base_freq (55 Hz = A1).
    This quantizes to the chromatic scale, so small frequency variations
    within the same note get grouped together.

    Args:
        edges: Rising edge positions
        sample_rate: PCM sample rate (usually 44100)
        speed_multiplier: Adjust if Arduino runs at different speed
                         (e.g., 0.8 for 20MHz instead of 16MHz)
        base_freq: Reference frequency for note calculation (55 Hz = A1)

    Returns list of note values (semitones from base_freq).
    """
    note_values = []

    for i in range(len(edges) - 1):
        period = edges[i + 1] - edges[i]
        if period <= 0:
            continue

        # Calculate frequency from period
        frequency = (sample_rate * speed_multiplier) / period

        # Convert to semitones from base frequency
        # note = 12 * log2(freq / base_freq)
        if frequency > 0:
            note = round(12 * math.log2(frequency / base_freq))
            note_values.append(note)
            print(f"debug : Note {note} found between {edges[i]} and {edges[i + 1]}")

    return note_values


def group_notes_to_freq_duration(
    note_values: list[int],
    edges: list[int],
    sample_rate: int = 44100,
    speed_multiplier: float = 1.0,
    base_freq: float = 55.0,
    min_cycles: int = 1,
) -> list[Note]:
    """
    Step 4: Group consecutive cycles with the same note value.

    When the note changes, we record:
    - The frequency (converted back from note value)
    - The duration (time from first edge of this note to current edge)

    If a note has only 1 cycle, it's treated as noise (freq=0).
    This filters out spurious single-cycle glitches.

    Args:
        note_values: List of note values for each cycle
        edges: Rising edge positions
        sample_rate: PCM sample rate
        speed_multiplier: Speed adjustment factor
        base_freq: Reference frequency
        min_cycles: Minimum cycles for a valid note (else treated as silence)

    Returns list of Note objects with freq_hz and dur_ms.
    """
    if not note_values:
        return []

    notes = []
    last_note = -999  # Impossible value to force first note detection
    cycle_count = 0
    initial_edge_idx = 0

    for i, note_val in enumerate(note_values):
        cycle_count += 1

        if note_val != last_note:
            # Note changed - record the previous note
            if i > 0:
                # Calculate frequency from note value
                if cycle_count > min_cycles:
                    freq_hz = round(base_freq * (2 ** (last_note / 12)))
                else:
                    freq_hz = 0  # Too few cycles = noise/silence

                # Calculate duration in ms
                edge_distance = edges[i] - edges[initial_edge_idx]
                dur_ms = round(1000 * edge_distance / (sample_rate * speed_multiplier))

                if dur_ms > 0:
                    notes.append(Note(freq_hz=freq_hz, dur_ms=dur_ms))

            # Start new note
            initial_edge_idx = i
            cycle_count = 0

        last_note = note_val

    # Don't forget the last note
    if cycle_count > 0 and len(edges) > 1:
        if cycle_count > min_cycles:
            freq_hz = round(base_freq * (2 ** (last_note / 12)))
        else:
            freq_hz = 0

        edge_distance = edges[-1] - edges[initial_edge_idx]
        dur_ms = round(1000 * edge_distance / (sample_rate * speed_multiplier))

        if dur_ms > 0:
            notes.append(Note(freq_hz=freq_hz, dur_ms=dur_ms))

    return notes


def merge_consecutive_notes(notes: list[Note]) -> list[Note]:
    """
    Step 5a: Merge consecutive notes .

    This merges them into single notes.
    """
    if not notes:
        return []

    merged = []
    current = Note(freq_hz=notes[0].freq_hz, dur_ms=notes[0].dur_ms)

    for note in notes[1:]:
        # Merge if both are silence
        if current.freq_hz == note.freq_hz :
            current.dur_ms += note.dur_ms
        else:
            merged.append(current)
            current = Note(freq_hz=note.freq_hz, dur_ms=note.dur_ms)

    merged.append(current)
    return merged

def smooth_parasites(notes: list[Note], smooth: int) -> list[Note]:
    """
        TODO
    """
    if not notes:
        return []

    smoothed = []
    smoothed.append(notes[0])

    for i in range(1, len(notes)-1):
        # remove if short between two same notes 
        if notes[i].dur_ms <= smooth and notes[i-1].freq_hz == notes[i+1].freq_hz :
            notes[i+1].dur_ms += notes[i].dur_ms
            print(f"debug : Removing note {i} ({str(notes[i])})")
            continue
        smoothed.append(notes[i])
    
    smoothed.append(notes[len(notes)-1])

    return smoothed

def remove_leading_trailing_silence(notes: list[Note]) -> list[Note]:
    """
    Step 5b: Remove leading and trailing silence notes.

    The beginning/end of the file often has silence.
    """
    while notes and notes[0].freq_hz == 0:
        notes.pop(0)
    while notes and notes[-1].freq_hz == 0:
        notes.pop()
    return notes


def output_arduino_format(notes: list[Note]) -> str:
    """
    Format notes as C arrays for Arduino/ESP32.

    Produces:
    - int melody[] = { freq1, freq2, ... };
    - int durations[] = { dur1, dur2, ... };
    """
    freqs = [str(n.freq_hz) for n in notes]
    durs = [str(n.dur_ms) for n in notes]

    lines = [
        f"int melody[] = {{",
        f"  {', '.join(freqs)}",
        "};",
        "",
        f"int durations[] = {{",
        f"  {', '.join(durs)}",
        "};",
    ]

    return '\n'.join(lines)


def output_binary_format(notes: list[Note]) -> bytes:
    """
    Output as binary frames (compatible with proto_py format).

    Each frame is 4 bytes: uint16 freq_hz + uint16 dur_ms (little-endian).
    """
    data = b''
    for note in notes:
        data += struct.pack('<HH', note.freq_hz, note.dur_ms)
    return data


def generate_preview_wav(notes: list[Note], output_path: Path, sample_rate: int = 44100, amplitude: float = 0.3) -> float:
    """
    Generate a square wave WAV preview from notes.

    Args:
        notes: List of Note objects (freq_hz, dur_ms)
        output_path: Output WAV file path
        sample_rate: Output sample rate (default 44100)
        amplitude: Volume 0.0-1.0 (default 0.3 to avoid clipping)

    Returns:
        Duration in seconds
    """
    import wave
    import array

    samples = []

    for note in notes:
        num_samples = int(sample_rate * note.dur_ms / 1000)

        if note.freq_hz == 0 or num_samples == 0:
            # Silence
            samples.extend([0] * num_samples)
        else:
            # Generate square wave for this note's duration
            period_samples = sample_rate / note.freq_hz
            half_period = period_samples / 2
            high_val = int(32767 * amplitude)
            low_val = -high_val

            for i in range(num_samples):
                phase = (i % period_samples)
                if phase < half_period:
                    samples.append(high_val)
                else:
                    samples.append(low_val)

    # Write WAV file
    with wave.open(str(output_path), 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(array.array('h', samples).tobytes())

    return len(samples) / sample_rate


def decode_pcm(
    pcm_path: Path,
    sample_rate: int = 44100,
    speed_multiplier: float = 1.0,
    base_freq: float = 55.0,
    threshold: int = 30,
    smooth: int = 0, 
    merge: int = 1, 
    method: str = "edge",
    window_size: int = 5,
) -> list[Note]:
    """
    Full pipeline: PCM file -> list of Notes.

    Args:
        pcm_path: Input PCM file
        sample_rate: Sample rate (default 44100)
        speed_multiplier: Speed adjustment (default 1.0)
        base_freq: Base frequency for note calc (default 55 Hz)
        threshold: For 'threshold': absolute level, for 'edge': min delta
        method: 'edge' (default, for degraded signals) or 'threshold' (original)
        window_size: Window for edge detection (default 5)
    """
    print(f"Loading {pcm_path}...")
    samples = load_pcm_signed8(pcm_path)
    print(f"  {len(samples)} samples ({len(samples)/sample_rate:.2f}s at {sample_rate} Hz)")

    print(f"Binarizing samples (method={method}, threshold={threshold})...")
    binary = binarize_samples(samples, threshold=threshold, method=method, window_size=window_size)

    print("Finding rising edges...")
    edges = find_rising_edges(binary)
    print(f"  Found {len(edges)} rising edges")

    print("Converting to note values...")
    note_values = edges_to_note_values(
        edges,
        sample_rate=sample_rate,
        speed_multiplier=speed_multiplier,
        base_freq=base_freq,
    )
    print(f"  {len(note_values)} cycles analyzed")

    print("Grouping into notes...")
    notes = group_notes_to_freq_duration(
        note_values,
        edges,
        sample_rate=sample_rate,
        speed_multiplier=speed_multiplier,
        base_freq=base_freq,
    )
    print(f"  {len(notes)} notes before cleanup")

    if merge >0 :
        print("Merging consecutive notes...")
        notes = merge_consecutive_notes(notes)
        print(f"  {len(notes)} notes after merge")

    if smooth > 0 :
        print("Smoothing parasites...")
        notes = smooth_parasites(notes, smooth)
        ## re-merging might be necessary
        if merge >0 :
            notes = merge_consecutive_notes(notes)
        print(f"  {len(notes)} notes after merge")




    print("Removing leading/trailing silence...")
    notes = remove_leading_trailing_silence(notes)
    print(f"  {len(notes)} notes final")

    return notes


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Decode PC speaker PCM audio to frequency/duration arrays",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Basic usage (outputs Arduino C arrays)
  python pulse_decode.py circus_1.pcm

  # Output to binary file (compatible with proto_py)
  python pulse_decode.py circus_1.pcm --output frames.bin --format binary

  # Adjust for 20MHz Arduino (vs 16MHz standard)
  python pulse_decode.py circus_1.pcm --speed 0.8

Creating PCM input:
  # From WAV using sox:
  sox input.wav -r 44100 -c 1 -b 8 -e signed-integer output.pcm

  # From any audio using ffmpeg:
  ffmpeg -i input.mp3 -f s8 -ar 44100 -ac 1 output.pcm
        """,
    )

    parser.add_argument("input", type=Path, help="Input PCM file (44.1kHz, signed 8-bit, mono)")
    parser.add_argument("-o", "--output", type=Path, help="Output file (default: stdout for arduino, required for binary)")
    parser.add_argument("--format", choices=["arduino", "binary"], default="arduino",
                       help="Output format (default: arduino)")
    parser.add_argument("--sample-rate", type=int, default=44100, help="PCM sample rate (default: 44100)")
    parser.add_argument("--speed", type=float, default=1.0,
                       help="Speed multiplier (0.8 for 20MHz Arduino, 1.0 for 16MHz)")
    parser.add_argument("--base-freq", type=float, default=55.0,
                       help="Base frequency for note calculation (default: 55 Hz = A1)")
    parser.add_argument("--method", choices=["edge", "threshold"], default="edge",
                       help="Binarization method: 'edge' (default, for degraded signals) or 'threshold' (original)")
    parser.add_argument("--threshold", type=int, default=30,
                       help="For edge: min delta (default 30). For threshold: absolute level.")
    parser.add_argument("--smooth", type=int, default=0,
                       help="if > 0, smoothens sound by removing notes with length > arg between 2 same notes")
    parser.add_argument("--merge", type=int, default=1,
                       help="if > 0, merges consecutively equal notes")
    parser.add_argument("--window-size", type=int, default=5,
                       help="Window size for edge detection (default: 5)")
    parser.add_argument("--preview", type=Path,
                       help="Generate a square wave WAV preview file")

    args = parser.parse_args()

    if not args.input.exists():
        print(f"Error: File not found: {args.input}", file=sys.stderr)
        return 1

    try:
        notes = decode_pcm(
            args.input,
            sample_rate=args.sample_rate,
            speed_multiplier=args.speed,
            base_freq=args.base_freq,
            threshold=args.threshold,
            smooth=args.smooth,
            merge=args.merge,
            method=args.method,
            window_size=args.window_size,
        )

        if not notes:
            print("Warning: No notes extracted!", file=sys.stderr)
            return 1

        # Calculate stats
        total_dur_ms = sum(n.dur_ms for n in notes)
        print(f"\nResult: {len(notes)} notes, {total_dur_ms/1000:.2f}s total duration")

        # Generate preview WAV if requested
        if args.preview:
            print(f"Generating preview WAV...")
            preview_dur = generate_preview_wav(notes, args.preview)
            print(f"  Wrote {args.preview} ({preview_dur:.2f}s)")

        if args.format == "arduino":
            output = output_arduino_format(notes)
            if args.output:
                args.output.write_text(output)
                print(f"Wrote {args.output}")
            else:
                print("\n" + output)

        elif args.format == "binary":
            if not args.output:
                print("Error: --output required for binary format", file=sys.stderr)
                return 1
            data = output_binary_format(notes)
            args.output.write_bytes(data)
            print(f"Wrote {args.output} ({len(data)} bytes)")

        return 0

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
