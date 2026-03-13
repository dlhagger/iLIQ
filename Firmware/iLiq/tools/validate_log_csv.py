#!/usr/bin/env python3
"""Basic CSV validator for lickometer session logs."""

import argparse
import csv
import pathlib
import sys


EXPECTED_SCHEMA = ["SchemaVersion", "1"]
EXPECTED_HEADER = ["Time(ms)", "WallTime", "Electrode", "Event"]


def validate_file(path: pathlib.Path) -> list[str]:
  errors: list[str] = []

  with path.open("r", newline="", encoding="utf-8") as f:
    reader = csv.reader(f)
    rows = list(reader)

  if len(rows) < 2:
    return [f"{path}: file is too short to contain schema and header rows"]

  if rows[0] != EXPECTED_SCHEMA:
    errors.append(f"{path}: schema row mismatch, found {rows[0]!r}")

  if rows[1] != EXPECTED_HEADER:
    errors.append(f"{path}: header mismatch, found {rows[1]!r}")

  prev_ms: int | None = None
  session_start_count = 0
  session_stop_count = 0

  for i, row in enumerate(rows[2:], start=3):
    if len(row) != 4:
      errors.append(f"{path}:{i}: expected 4 columns, found {len(row)}")
      continue

    time_ms, wall_time, electrode, event = row

    try:
      parsed_ms = int(time_ms)
      if parsed_ms < 0:
        raise ValueError("negative")
    except ValueError:
      errors.append(f"{path}:{i}: invalid Time(ms) value {time_ms!r}")
      continue

    if prev_ms is not None and parsed_ms < prev_ms:
      errors.append(f"{path}:{i}: Time(ms) went backwards ({parsed_ms} < {prev_ms})")
    prev_ms = parsed_ms

    if not electrode.startswith("E"):
      errors.append(f"{path}:{i}: Electrode should start with 'E', got {electrode!r}")

    if not event:
      errors.append(f"{path}:{i}: Event is empty")

    if event == "SESSION_START":
      session_start_count += 1
      if not wall_time:
        errors.append(f"{path}:{i}: SESSION_START must include WallTime")
    elif event == "SESSION_STOP":
      session_stop_count += 1
      if not wall_time:
        errors.append(f"{path}:{i}: SESSION_STOP must include WallTime")

  if session_start_count != 1:
    errors.append(f"{path}: expected exactly one SESSION_START, found {session_start_count}")

  if session_stop_count > 1:
    errors.append(f"{path}: expected at most one SESSION_STOP, found {session_stop_count}")

  if session_stop_count == 1 and session_start_count == 1:
    # No additional ordering check needed beyond monotonic millis and counts.
    pass

  return errors


def main() -> int:
  parser = argparse.ArgumentParser(description="Validate lickometer CSV logs.")
  parser.add_argument("paths", nargs="+", help="CSV file(s) or directories containing CSVs")
  args = parser.parse_args()

  csv_paths: list[pathlib.Path] = []
  for raw_path in args.paths:
    path = pathlib.Path(raw_path)
    if path.is_dir():
      csv_paths.extend(sorted(path.glob("*.csv")))
    else:
      csv_paths.append(path)

  if not csv_paths:
    print("No CSV files found for validation.", file=sys.stderr)
    return 2

  all_errors: list[str] = []
  for csv_path in csv_paths:
    if not csv_path.exists():
      all_errors.append(f"{csv_path}: file does not exist")
      continue
    all_errors.extend(validate_file(csv_path))

  if all_errors:
    print("Validation failed:")
    for error in all_errors:
      print(f"- {error}")
    return 1

  print(f"Validation passed for {len(csv_paths)} file(s).")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
