#!/usr/bin/env python3
"""Regenerate independent, bounded decimal-to-binary64 control-text fixtures.

Only Python's standard library is used. The conversion oracle is float(text),
not the production parser. Decimal preserves the literal's exact value, and
Fraction distances to adjacent binary64 values certify nearest/even rounding.
The generator neither reads nor imports production source.

Run without arguments to regenerate both artifacts, or with --check to detect
drift without modifying either. Python must use IEEE 754 binary64 floats.
"""

from __future__ import annotations

import argparse
from collections import Counter
from decimal import Decimal, ROUND_CEILING, ROUND_FLOOR, localcontext
from fractions import Fraction
import json
import math
from pathlib import Path
import re
import struct
import sys


ROOT = Path(__file__).resolve().parents[1]
JSON_PATH = ROOT / "tests/fixtures/control_text_vectors.json"
HEADER_PATH = ROOT / "tests/fixtures/control_text_vectors.hpp"
MAX_CASES = 400
MAX_INPUT = 127
SIGN = 0x8000000000000000
MAX_FINITE = 0x7FEFFFFFFFFFFFFF
MAX_MAGNITUDE = 0x4040000000000000  # Exactly 32.
GRAMMAR = re.compile(
    r"(?P<sign>[+-]?)(?P<mantissa>(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))"
    r"(?:[eE](?P<exponent>[+-]?[0-9]+))?",
    re.ASCII,
)


def require(condition: bool, message: str) -> None:
    """Keep oracle checks active under python -O as well."""
    if not condition:
        raise ValueError(message)


def bits(value: float) -> int:
    return struct.unpack(">Q", struct.pack(">d", value))[0]


def binary64(encoding: int) -> float:
    return struct.unpack(">d", struct.pack(">Q", encoding))[0]


def binary32(encoding: int) -> float:
    return struct.unpack(">f", struct.pack(">I", encoding))[0]


def binary_fraction(encoding: int) -> Fraction:
    return Fraction.from_float(binary64(encoding))


def certify_rounding(exact: Fraction, encoding: int) -> None:
    """Compare exact rational distances; no decimal-to-binary algorithm here."""
    magnitude = encoding & ~SIGN
    require(exact >= 0, "rounding certificate needs a magnitude")
    if magnitude > MAX_FINITE:
        require(magnitude == MAX_FINITE + 1, "oracle returned NaN")
        overflow_midpoint = Fraction(1 << 1024) - Fraction(1 << 970)
        require(exact >= overflow_midpoint, "premature binary64 overflow")
        return

    chosen = binary_fraction(magnitude)
    distance = abs(exact - chosen)
    neighbors = []
    if magnitude:
        neighbors.append(binary_fraction(magnitude - 1))
    # The virtual successor of the largest finite number is exactly 2**1024.
    neighbors.append(
        binary_fraction(magnitude + 1)
        if magnitude < MAX_FINITE else Fraction(1 << 1024)
    )
    for neighbor in neighbors:
        other_distance = abs(exact - neighbor)
        require(distance <= other_distance, "float(text) is not nearest")
        if distance == other_distance:
            require(magnitude % 2 == 0, "float(text) broke a tie toward odd")


def reference(text: str) -> tuple[str | None, str | None]:
    """Return raw-parser bits or a reason for rejection, independently."""
    if not text.isascii() or len(text) > MAX_INPUT:
        return None, "input_bound"
    match = GRAMMAR.fullmatch(text)
    if match is None:
        return None, "syntax"

    converted = float(text)
    encoding = bits(converted)
    negative = match["sign"] == "-"
    require(bool(encoding & SIGN) == negative, "float(text) lost its sign")
    coefficient = match["mantissa"].replace(".", "").lstrip("0")
    if not coefficient:
        require(encoding == (SIGN if negative else 0), "literal zero changed")
        return f"0x{encoding:016x}", None

    fraction_digits = (
        len(match["mantissa"].split(".", 1)[1])
        if "." in match["mantissa"] else 0
    )
    exponent = int(match["exponent"] or "0") - fraction_digits
    decimal_order = len(coefficient) - 1 + exponent
    # Exponents can occupy almost the entire input. These conservative exact
    # decimal-order bounds avoid constructing integers with absurd sizes.
    # Cases near either actual binary64 boundary use Fraction below instead.
    if decimal_order > 400:
        require(math.isinf(converted), "huge literal did not overflow")
        return None, "overflow"
    if decimal_order < -400:
        require(converted == 0, "tiny literal did not underflow")
        return None, "underflow"

    exact = abs(Fraction(Decimal(text)))
    certify_rounding(exact, encoding)
    magnitude = encoding & ~SIGN
    if math.isinf(converted):
        return None, "overflow"
    if magnitude == 0:
        return None, "underflow"
    if magnitude > MAX_MAGNITUDE:
        return None, "range"
    return f"0x{encoding:016x}", None


def short_decimal(value: Decimal) -> str:
    """Choose the shorter exact fixed/scientific spelling without rounding."""
    sign, digit_tuple, exponent = value.as_tuple()
    digits = "".join(str(digit) for digit in digit_tuple)
    if not any(digit_tuple):
        return "-0" if sign else "0"
    while digits.endswith("0"):
        digits = digits[:-1]
        exponent += 1
    prefix = "-" if sign else ""
    point = len(digits) + exponent
    if point <= 0:
        fixed = "0." + "0" * -point + digits
    elif point >= len(digits):
        fixed = digits + "0" * (point - len(digits))
    else:
        fixed = digits[:point] + "." + digits[point:]
    scientific = digits[0]
    if len(digits) > 1:
        scientific += "." + digits[1:]
    scientific += "e" + str(point - 1)
    return prefix + min((fixed, scientific), key=len)


def exact_decimal(value: Fraction) -> Decimal:
    # All generated exact fractions have a power-of-two denominator no larger
    # than 2**1075; 2,000 digits leave ample headroom for their terminating form.
    with localcontext() as context:
        context.prec = 2000
        result = Decimal(value.numerator) / Decimal(value.denominator)
    require(Fraction(result) == value, "exact decimal conversion rounded")
    return result


def bracket_decimal(value: Fraction) -> tuple[str, str]:
    bounds = []
    for rounding in (ROUND_FLOOR, ROUND_CEILING):
        with localcontext() as context:
            context.prec = 110
            context.rounding = rounding
            rounded = Decimal(value.numerator) / Decimal(value.denominator)
        bounds.append(short_decimal(rounded))
    require(Fraction(Decimal(bounds[0])) < value < Fraction(Decimal(bounds[1])),
            "bounded decimal midpoint brackets must be strict")
    return bounds[0], bounds[1]


def build_vectors() -> list[dict[str, object]]:
    require(sys.float_info.radix == 2 and sys.float_info.mant_dig == 53
            and sys.float_info.max_exp == 1024 and sys.float_info.min_exp == -1021,
            "this generator requires IEEE 754 binary64 Python floats")
    vectors: list[dict[str, object]] = []
    seen_inputs: set[str] = set()
    seen_ids: set[str] = set()

    def add(identifier: str, category: str, text: str) -> None:
        require(text.isascii() and len(text) <= MAX_INPUT,
                f"generated input exceeds the ASCII/length contract: {identifier}")
        require(identifier not in seen_ids, f"duplicate id: {identifier}")
        if text in seen_inputs:
            return
        expected, rejection = reference(text)
        entry: dict[str, object] = {
            "id": identifier,
            "category": category,
            "input": text,
            "expected_bits": expected,
        }
        if rejection is not None:
            entry["rejection"] = rejection
        vectors.append(entry)
        seen_ids.add(identifier)
        seen_inputs.add(text)

    valid = [
        "0", "-0", "+0", "0.0", "-0.000", "+.0", "-.0", "0.", "-0.",
        "0e0", "-0e+99999", "+0E-99999", "1", "-1", "+1", ".5", "-.5",
        "+.5", "1.", "-1.", "0001.2500", "+0001.2500", "-0001.2500",
        "1e0", "1E+0", "1e-0", "1.25e+0000", "125e-002", "00125E-0002",
        "0.1", "-0.1", "16", "-16", "32", "-32", "3.2e1", "320e-1",
        "0.00032E+000005", "32.000000000000000001", "-32.000000000000000001",
        "31.999999999999999999", "1.000000000000000001",
        "2.2250738585072014e-308", "2.225073858507201e-308", "1e-308",
        "5e-324", "-5e-324", "1e-323", "4.9406564584124654e-324",
    ]
    for index, text in enumerate(valid):
        add(f"grammar-valid-{index:02}", "grammar_valid", text)

    malformed = [
        "", ".", "+", "-", "+.", "-.", "e1", "E-1", "1e", "1e+", "1e-",
        "1e.0", "1e++1", "1e--1", "1e+-1", "1ee0", "1E1e0", "1..0", "1.2.3",
        "++1", "--1", "+-1", "-+1", "0x0", "0x1p-1", "0X10", "nan", "NaN",
        "NAN", "nan(1)", "+nan", "inf", "-inf", "Infinity", "+Infinity",
        "1,0", "1_0", "1'0", "16junk", "1x", "1 x", "x", " ", "\t", "\n",
        " 1", "1 ", "1\t0", "+ 1", "1 e0", "1\0", "\x001", "1\x01", "\x071",
        "\x7f1", "1\r", "\v1", "1\f", "Off", "On",
    ]
    for index, text in enumerate(malformed):
        add(f"grammar-reject-{index:02}", "grammar_reject", text)

    extremes = [
        "33", "-33", "32.00000000000001", "-32.00000000000001", "32.1",
        "-32.1", "1e2", "-1e2", "1e308", "-1e308", "1e309", "-1e309",
        "1.7976931348623157e308", "1.7976931348623158e308", "1.7976931348623159e308",
        "1e99999", "-1e99999", "1e-99999", "-1e-99999", "1e-324", "-1e-324",
        "2e-324", "-2e-324", "3e-324", "-3e-324", "2.4703282292062327e-324",
        "2.4703282292062328e-324", "7.410984687618698e-324", "7.410984687618699e-324",
        "2.2250738585072009e-308", "2.2250738585072011e-308",
    ]
    for index, text in enumerate(extremes):
        add(f"boundary-literal-{index:02}", "literal_boundaries", text)

    maximum_length = [
        "0" * 126 + "1", "+" + "0" * 125 + "1", "-" + "0" * 125 + "1",
        "32." + "0" * 124, "-32." + "0" * 123, "0." + "0" * 124 + "1",
        "1e" + "9" * 125, "1e-" + "9" * 124,
        "0e" + "9" * 125, "-0e-" + "9" * 123,
        "1e+" + "0" * 123 + "1", "0" * 126 + ".", "0" * 126 + "x",
    ]
    for index, text in enumerate(maximum_length):
        require(len(text) == MAX_INPUT, "max-length fixture has the wrong length")
        add(f"maximum-length-{index:02}", "maximum_length", text)

    def midpoint_cases(lower_bits: int, identifier: str) -> None:
        midpoint = (binary_fraction(lower_bits) + binary_fraction(lower_bits + 1)) / 2
        decimal = exact_decimal(midpoint)
        exact_text = short_decimal(decimal)
        if len(exact_text) <= MAX_INPUT:
            quantum = Fraction(10) ** (decimal.as_tuple().exponent - 1)
            lower_text = short_decimal(exact_decimal(midpoint - quantum))
            upper_text = short_decimal(exact_decimal(midpoint + quantum))
            if max(len(lower_text), len(upper_text)) > MAX_INPUT:
                lower_text, upper_text = bracket_decimal(midpoint)
            add(identifier + "-below", "binary64_midpoint", lower_text)
            add(identifier + "-tie", "binary64_midpoint", exact_text)
            add(identifier + "-above", "binary64_midpoint", upper_text)
        else:
            lower_text, upper_text = bracket_decimal(midpoint)
            add(identifier + "-below", "binary64_midpoint_bracket", lower_text)
            add(identifier + "-above", "binary64_midpoint_bracket", upper_text)

    for index, encoding in enumerate([
        0, 1, 2, 3, 0x0007FFFFFFFFFFFF, 0x000FFFFFFFFFFFFE,
        0x000FFFFFFFFFFFFF, 0x0010000000000000, 0x0010000000000001,
    ]):
        midpoint_cases(encoding, f"subnormal-{index:02}")
    for exponent in (-1000, -512, -256, -128, -90, -60, -20, -1, 0, 1, 4):
        base = bits(math.ldexp(1.0, exponent))
        for offset in (0, 1):
            midpoint_cases(base + offset, f"power-{exponent}-parity-{offset}")
    for exponent in (-90, -1, 0, 4, 5):
        midpoint_cases(bits(math.ldexp(1.0, exponent)) - 1, f"power-{exponent}-predecessor")
    midpoint_cases(MAX_MAGNITUDE, "upper-range")

    # These are plain gain values, before the controller's /32 normalization
    # and binary32 storage. The midpoint itself is exactly binary64, so include
    # its immediately adjacent binary64 values to catch double rounding later.
    for index, encoding in enumerate([
        0, 1, 2, 3, 0x007FFFFE, 0x007FFFFF, 0x00800000, 0x00800001,
        0x33800000, 0x3DCCCCCC, 0x3F000000, 0x3F000001, 0x3F7FFFFF,
        0x3F800000, 0x3F800001, 0x417FFFFF, 0x41800000, 0x41800001, 0x41FFFFFF,
    ]):
        midpoint = (Fraction.from_float(binary32(encoding))
                    + Fraction.from_float(binary32(encoding + 1))) / 2
        middle_bits = bits(float(midpoint))
        require(binary_fraction(middle_bits) == midpoint, "binary32 midpoint was rounded")
        for offset, label in ((-1, "below"), (0, "tie"), (1, "above")):
            text = (short_decimal(exact_decimal(midpoint)) if offset == 0
                    else format(binary64(middle_bits + offset), ".17g"))
            add(f"binary32-gain-{index:02}-{label}", "binary32_gain_midpoint", text)

    # Fixed bit patterns give normal/subnormal coverage across the exponent
    # range. The decimal oracle still certifies each spelling independently.
    for index, exponent_field in enumerate([
        0, 1, 2, 3, 5, 16, 64, 128, 256, 512, 800, 1000,
        1010, 1018, 1020, 1021, 1022, 1023, 1024, 1025, 1026, 1027,
    ]):
        for fraction in (0x000123456789ABCD, 0x000FEDCBA9876543):
            encoding = (exponent_field << 52) | fraction
            text = format(binary64(encoding), ".17g")
            add(f"exponent-sweep-{index:02}-{fraction:013x}", "exponent_sweep", text)

    for index, text in enumerate([
        "+000.100000E+00000", "-000.100000e-00000", "+00032.00000e000000",
        "-00032.00000E-000000", "+00000005e-00324", "-00000005E-00324",
        "000000000000001e+00000000000000000000000000000000000000000000000000",
        "0.000000000000000000000000000000000000000000000000000000000000001e+63",
        "-2.2250738585072014e-308", "-2.225073858507201e-308",
        "-31.999999999999999999", "-1.000000000000000001",
    ]):
        add(f"spelling-variant-{index:02}", "spelling_variants", text)

    # A fixed integer recurrence provides varied long coefficients, trailing
    # zeros, signs, and decimal exponents. No random-module/version dependency.
    state = 0x5A17C0DEC0FFEE
    index = 0
    require(len(vectors) < MAX_CASES, "structured corpus exceeds the case budget")
    while len(vectors) < MAX_CASES:
        state = (6364136223846793005 * state + 1442695040888963407) & ((1 << 64) - 1)
        digits = str(state) + str(state ^ 0xD1B54A32D192ED03)
        digits = (digits * 3)[: 1 + (index * 17) % 106]
        decimal_order = (-324, -308, -150, -91, -40, -1, 0, 1, 2)[index % 9]
        exponent = decimal_order - len(digits) + 1
        text = ("-" if index % 4 == 3 else "+" if index % 4 == 2 else "")
        text += digits + (".000" if index % 3 == 0 else "") + "e" + str(exponent)
        add(f"decimal-sweep-{index:03}", "decimal_sweep", text)
        index += 1

    return vectors


def render_json(vectors: list[dict[str, object]]) -> str:
    accepted = sum(item["expected_bits"] is not None for item in vectors)
    document = {
        "schema_version": 1,
        "generator": "tools/generate_control_text_vectors.py",
        "oracle": "Python float(text), checked by exact Decimal/Fraction neighbor distances and even ties",
        "contract": {
            "layer": "rhodes::control_text::parse_decimal (raw token; no whitespace or units)",
            "maximum_ascii_characters": MAX_INPUT,
            "maximum_rounded_magnitude": 32,
            "negative_values": "accepted by this raw helper; controller range validation is separate",
            "zero": "preserve literal sign; reject nonzero literals rounded to zero",
            "expected_bits": "16-digit hexadecimal binary64 encoding, or null for rejection",
            "midpoint_limit": "Subnormal and smallest-normal exact ties exceed 127 characters; 110-significant-digit directed decimal brackets straddle their exact rational midpoints.",
        },
        "counts": {
            "total": len(vectors),
            "accepted": accepted,
            "rejected": len(vectors) - accepted,
            "categories": dict(sorted(Counter(str(item["category"]) for item in vectors).items())),
        },
        "vectors": vectors,
    }
    return json.dumps(document, indent=2, ensure_ascii=True) + "\n"


def cpp_string(text: str) -> str:
    # Three-digit octal escapes preserve ASCII controls and embedded NULs.
    # Split before a following digit to avoid MSVC C4125 under warnings-as-errors.
    pieces = ['"']
    for index, character in enumerate(text):
        if character in ('"', "\\"):
            pieces.append("\\" + character)
        elif 32 <= ord(character) <= 126:
            pieces.append(character)
        else:
            pieces.append(f"\\{ord(character):03o}")
            if index + 1 < len(text) and text[index + 1] in "0123456789":
                pieces.append('" "')
    pieces.append('"')
    return "".join(pieces)


def render_header(vectors: list[dict[str, object]]) -> str:
    lines = [
        "// Generated by tools/generate_control_text_vectors.py; do not edit.",
        "// Canonical equivalent: tests/fixtures/control_text_vectors.json.",
        "// Python float(text), certified with exact rational neighbor distances.",
        "#pragma once", "", "#include <array>", "#include <cstdint>",
        "#include <string_view>", "", "namespace rhodes::control_text::test_vectors {", "",
        "struct Vector {", "    std::string_view id;", "    std::string_view input;",
        "    bool accepted;", "    std::uint64_t bits;", "};", "",
        f"inline constexpr std::array<Vector, {len(vectors)}> vectors{{{{",
    ]
    for item in vectors:
        text = str(item["input"])
        accepted = item["expected_bits"] is not None
        encoding = item["expected_bits"] if accepted else "0x0000000000000000"
        lines.append(
            f"    Vector{{{cpp_string(str(item['id']))}, "
            f"std::string_view{{{cpp_string(text)}, {len(text)}}}, "
            f"{'true' if accepted else 'false'}, {encoding}ULL}},"
        )
    lines += ["}};", "", "} // namespace rhodes::control_text::test_vectors", ""]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="check both artifacts without writing")
    arguments = parser.parse_args()
    vectors = build_vectors()
    artifacts = ((JSON_PATH, render_json(vectors)), (HEADER_PATH, render_header(vectors)))
    stale = []
    for path, rendered in artifacts:
        expected = rendered.encode("utf-8")
        if arguments.check:
            if not path.is_file() or path.read_bytes() != expected:
                stale.append(str(path.relative_to(ROOT)))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(expected)
    if stale:
        print("Control-text vector drift: " + ", ".join(stale), file=sys.stderr)
        print("Run python3 tools/generate_control_text_vectors.py", file=sys.stderr)
        return 1
    action = "Verified" if arguments.check else "Generated"
    print(f"{action} {len(vectors)} independent control-text vectors (JSON and C++ header).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
