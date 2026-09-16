#!/usr/bin/env python3
"""Reject literal printf formats unsupported by the firmware formatter."""

from __future__ import annotations

import argparse
import ast
import re
import sys
from pathlib import Path


FORMAT_ARGUMENT = {
    "printf": 0,
    "printf_": 0,
    "vprintf": 0,
    "vprintf_": 0,
    "sprintf": 1,
    "sprintf_": 1,
    "snprintf": 2,
    "snprintf_": 2,
    "vsnprintf": 2,
    "vsnprintf_": 2,
    "fctprintf": 2,
}

CALL_PATTERN = re.compile(
    r"\b(" + "|".join(sorted(FORMAT_ARGUMENT, key=len, reverse=True)) + r")\s*\("
)
STRING_PATTERN = re.compile(r'"(?:\\.|[^"\\])*"', re.DOTALL)
SUPPORTED_CONVERSIONS = frozenset("duosc%")
SUPPORTED_FLAGS = frozenset("0+ ")


def filter_debug_blocks(source: str, debug_defined: bool) -> str:
    """Remove inactive direct DEBUG preprocessor branches while preserving lines."""
    frames: list[tuple[str, bool]] = []
    output: list[str] = []

    for line in source.splitlines(keepends=True):
        directive = line.lstrip()
        match = re.match(r"#\s*(ifdef|ifndef)\s+DEBUG\b", directive)
        defined_match = re.match(
            r"#\s*if\s+(!\s*)?defined\s*\(\s*DEBUG\s*\)", directive
        )

        if match:
            active = debug_defined if match.group(1) == "ifdef" else not debug_defined
            frames.append(("debug", active))
            output.append("\n" if line.endswith("\n") else "")
            continue
        if defined_match:
            active = debug_defined
            if defined_match.group(1):
                active = not active
            frames.append(("debug", active))
            output.append("\n" if line.endswith("\n") else "")
            continue
        if re.match(r"#\s*(if|ifdef|ifndef)\b", directive):
            frames.append(("other", True))
            output.append(line)
            continue
        if re.match(r"#\s*(else|elif)\b", directive) and frames:
            kind, active = frames[-1]
            if kind == "debug":
                frames[-1] = (kind, not active)
                output.append("\n" if line.endswith("\n") else "")
            else:
                output.append(line)
            continue
        if re.match(r"#\s*endif\b", directive) and frames:
            frames.pop()
            output.append(line)
            continue

        if any(kind == "debug" and not active for kind, active in frames):
            output.append("\n" if line.endswith("\n") else "")
        else:
            output.append(line)

    return "".join(output)


def strip_comments(source: str) -> str:
    """Replace C comments with whitespace while retaining strings and line numbers."""
    output: list[str] = []
    index = 0
    state = "code"

    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""

        if state == "code":
            if char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            elif char == "/" and next_char == "/":
                output.extend((" ", " "))
                index += 2
                state = "line_comment"
                continue
            elif char == "/" and next_char == "*":
                output.extend((" ", " "))
                index += 2
                state = "block_comment"
                continue
            output.append(char)
        elif state == "string":
            output.append(char)
            if char == "\\" and next_char:
                output.append(next_char)
                index += 2
                continue
            if char == '"':
                state = "code"
        elif state == "char":
            output.append(char)
            if char == "\\" and next_char:
                output.append(next_char)
                index += 2
                continue
            if char == "'":
                state = "code"
        elif state == "line_comment":
            output.append("\n" if char == "\n" else " ")
            if char == "\n":
                state = "code"
        else:
            output.append("\n" if char == "\n" else " ")
            if char == "*" and next_char == "/":
                output.append(" ")
                index += 2
                state = "code"
                continue

        index += 1

    return "".join(output)


def call_arguments(source: str, open_paren: int) -> list[str] | None:
    arguments: list[str] = []
    start = open_paren + 1
    index = start
    depth = 1
    state = "code"

    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""

        if state == "string":
            if char == "\\" and next_char:
                index += 2
                continue
            if char == '"':
                state = "code"
        elif state == "char":
            if char == "\\" and next_char:
                index += 2
                continue
            if char == "'":
                state = "code"
        elif char == '"':
            state = "string"
        elif char == "'":
            state = "char"
        elif char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
            if depth == 0:
                arguments.append(source[start:index])
                return arguments
        elif char == "," and depth == 1:
            arguments.append(source[start:index])
            start = index + 1

        index += 1

    return None


def decode_literals(expression: str) -> str | None:
    literals = STRING_PATTERN.findall(expression)
    if not literals:
        return None

    decoded: list[str] = []
    for literal in literals:
        try:
            decoded.append(ast.literal_eval(literal))
        except (SyntaxError, ValueError):
            return None
    return "".join(decoded)


def unsupported_fragment(format_string: str) -> str | None:
    index = 0
    while index < len(format_string):
        if format_string[index] != "%":
            index += 1
            continue

        start = index
        index += 1
        if index < len(format_string) and format_string[index] == "%":
            index += 1
            continue

        while index < len(format_string) and format_string[index] in SUPPORTED_FLAGS:
            index += 1
        if index < len(format_string) and format_string[index] == "*":
            index += 1
        else:
            while index < len(format_string) and format_string[index].isdigit():
                index += 1

        if index < len(format_string) and format_string[index] == ".":
            index += 1
            if index < len(format_string) and format_string[index] == "*":
                index += 1
            else:
                while index < len(format_string) and format_string[index].isdigit():
                    index += 1

        if index >= len(format_string) or format_string[index] not in SUPPORTED_CONVERSIONS:
            end = index
            while end < len(format_string) and end - start < 16:
                end += 1
                if format_string[end - 1].isalpha():
                    break
            return format_string[start:end]
        index += 1

    return None


def validate_file(path: Path, debug_defined: bool) -> list[str]:
    source = path.read_text(encoding="utf-8")
    source = strip_comments(filter_debug_blocks(source, debug_defined))
    errors: list[str] = []

    for match in CALL_PATTERN.finditer(source):
        function = match.group(1)
        arguments = call_arguments(source, match.end() - 1)
        format_index = FORMAT_ARGUMENT[function]
        if arguments is None or len(arguments) <= format_index:
            continue

        format_string = decode_literals(arguments[format_index])
        if format_string is None:
            continue

        fragment = unsupported_fragment(format_string)
        if fragment is not None:
            line = source.count("\n", 0, match.start()) + 1
            errors.append(
                f"{path}:{line}: unsupported minimal printf format {fragment!r}; "
                "supported conversions are %d, %u, %o, %s, %c and %%"
            )

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--define", action="append", default=[])
    args = parser.parse_args()

    debug_defined = "DEBUG" in args.define
    errors: list[str] = []
    source_files = (
        *args.source_root.rglob("*.c"),
        *args.source_root.rglob("*.h"),
    )
    for path in sorted(source_files):
        if "external" in path.parts:
            continue
        errors.extend(validate_file(path, debug_defined))

    if errors:
        print("Minimal printf format validation failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1

    print("Minimal printf formats: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
