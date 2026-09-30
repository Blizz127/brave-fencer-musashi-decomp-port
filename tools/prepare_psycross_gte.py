#!/usr/bin/env python3
"""Generate the narrowly adapted PsyCross GTE TU without editing its checkout."""
import argparse
from pathlib import Path


def prepare(source: str) -> str:
    # Signed int32 translations times 4096 fit int64. Left-shifting a
    # negative signed value is undefined C++; multiplication is defined.
    for selector in (1, 2, 3):
        original = f"(long long)CV{selector}(cv) << 12"
        if source.count(original) != 2:
            raise ValueError(f"expected exactly two {original!r} occurrences")
    for selector in (1, 2, 3):
        source = source.replace(f"(long long)CV{selector}(cv) << 12",
                                f"(long long)CV{selector}(cv) * 4096LL")
    return source


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        generated = prepare(args.source.read_text())
    except ValueError as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text() != generated:
        args.output.write_text(generated)


if __name__ == "__main__":
    main()
