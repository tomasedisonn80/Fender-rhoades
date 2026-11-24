#!/usr/bin/env python3
r"""Verify photo-editor interactions through a real accepting Linux SDK host.

This supplements verify_native_photo_editor.py without changing it. Reuse that
tool's CLI options, XTEST input, native attachment, RGB capture and callback
parser. Supply an editorhost that both ACCEPTS and logs SDK beginEdit,
performEdit and endEdit callbacks; the stock Release editorhost cannot qualify
these checks. No audio loop, DAW integration or physical fidelity is tested.

Example (on an isolated X11 DISPLAY of at least 1600 x 1200):
  python3 tools/verify_photo_interactions.py --editorhost /path/editorhost-accepting \
    --bundle /path/RhodesResearch.vst3 --output-dir /path/new-results \
    --host-label accepting-diagnostic --source /path/host-provenance.json

Pillow and python-xlib are developer-only prerequisites. This tool installs no
dependencies and changes no SDK, bundle or project source. Exit codes: 0 pass,
1 observed failure, 2 incomplete callback evidence. Output directories must be
new or empty; earlier observations are never overwritten.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import math
from pathlib import Path
import platform
import sys
import time
import traceback

import verify_native_photo_editor as native


MINIMUM_SIZE = (768, 576)
# Original-photo anchors, in source pixels. The keyboard geometry contract
# defines 43 white fronts and 30 black faces; these are visual, not physical,
# measurements. The masks below deliberately exclude antialiased boundaries.
WHITE_FRONT_BOUNDARIES = (
    113, 143, 174, 205, 236, 267, 298, 329, 360, 391, 422,
    453, 484, 515, 546, 577, 608, 639, 670, 701, 732, 763,
    794, 825, 856, 887, 919, 950, 981, 1013, 1044, 1076,
    1107, 1139, 1170, 1202, 1234, 1266, 1297, 1329, 1361,
    1393, 1425, 1457,
)
BLACK_FRONT_CENTERS = (
    183, 217, 252, 305, 342, 395, 430, 464, 517, 555,
    608, 642, 677, 730, 767, 821, 856, 891, 945, 982,
    1036, 1071, 1106, 1159, 1197, 1252, 1287, 1322, 1377, 1415,
)
BLACK_PITCH_CLASSES = (1, 3, 6, 8, 10)
FEEDBACK_EXPIRY_WAIT = 2.2  # The source contract is 1.7 s, plus timer/render margin.


def keyboard_masks():
    """Make eroded black-face masks, independent of the highlight path builder."""
    from PIL import Image, ImageDraw, ImageFilter

    notes = [note for note in range(28, 101) if note % 12 in BLACK_PITCH_CLASSES]
    combined = Image.new("L", native.SOURCE_SIZE)
    masks = []
    for note, front in zip(notes, BLACK_FRONT_CENTERS, strict=True):
        rear = 780.0 + (front - 780.0) * 0.97
        polygon = ((rear - 8.5, 507), (rear + 8.5, 507),
                   (front + 9.5, 565), (front - 9.5, 565))
        left = math.floor(min(point[0] for point in polygon)) - 4
        top = math.floor(min(point[1] for point in polygon)) - 4
        right = math.ceil(max(point[0] for point in polygon)) + 5
        bottom = math.ceil(max(point[1] for point in polygon)) + 5
        mask = Image.new("L", (right - left, bottom - top))
        ImageDraw.Draw(mask).polygon([(x - left, y - top) for x, y in polygon], fill=255)
        # Seven-pixel square erosion removes three pixels at each face edge.
        mask = mask.filter(ImageFilter.MinFilter(7))
        local_bounds = mask.getbbox()
        if local_bounds is None:
            raise RuntimeError(f"Empty black interior mask for MIDI {note}")
        crop = mask.crop(local_bounds)
        bounds = (local_bounds[0] + left, local_bounds[1] + top,
                  local_bounds[2] + left, local_bounds[3] + top)
        masks.append({"midi_note": note, "bounds": bounds, "mask": crop,
                      "pixels": crop.histogram()[255],
                      "mask_sha256": hashlib.sha256(crop.tobytes()).hexdigest()})
        combined.paste(crop, bounds)
    return masks, combined


def masked_difference(actual, expected, mask):
    from PIL import ImageChops

    channels = ImageChops.difference(actual, expected).split()
    maximum = ImageChops.lighter(ImageChops.lighter(channels[0], channels[1]), channels[2])
    selected = ImageChops.darker(maximum, mask)
    histogram = selected.histogram()
    changed = sum(histogram[1:])
    return {"exact": changed == 0, "different_pixels": changed,
            "max_abs_channel_error": selected.getextrema()[1]}


class InteractionRun(native.NativeRun):
    def __init__(self, args, report):
        super().__init__(args, report)
        self.callback_ready = False
        self.held_modifier_codes = set()

    def exact_gestures(self, name, parameter, values, operation):
        item = self.action(name, operation)
        # Initial native font/render work can defer event delivery beyond the
        # base harness's fixed action pause. Observe the expected callback count
        # before closing this action's evidence interval; never reassign late
        # callbacks to the following interaction.
        deadline = time.monotonic() + 2.0
        while len(self.events) - item["first_callback_index"] < len(values) * 3 and time.monotonic() < deadline:
            self.pause(0.025)
        self.pause(0.05)
        item["callback_observation_finished_seconds"] = self.elapsed()
        item["last_callback_index_exclusive"] = len(self.events)
        events = self.events[item["first_callback_index"]:item["last_callback_index_exclusive"]]
        summary = native.gesture_summary(events, parameter)
        expected = [(kind, parameter, value if kind == "performEdit" else None)
                    for value in values for kind in ("beginEdit", "performEdit", "endEdit")]
        actual = [(event["kind"], event["parameter_id"], event.get("value")) for event in events]
        correct = len(actual) == len(expected) and all(
            a[:2] == e[:2] and (e[2] is None or
            (a[2] is not None and math.isclose(a[2], e[2], rel_tol=0.0, abs_tol=2e-5)))
            for a, e in zip(actual, expected))
        status = "incomplete" if not events else "pass" if correct else "fail"
        item.update(parameter_id=parameter, expected_values=values, host_gesture=summary)
        self.check(name, status, expected_values=values, host_gesture=summary,
                   exact_callback_sequence=correct, unexpected_parameter_ids=sorted(
                       {event["parameter_id"] for event in events if event["parameter_id"] != parameter}))
        if status == "pass":
            self.callback_ready = True
        return item

    def resize_to(self, size):
        self.outside()
        self.top.configure(width=size[0], height=size[1])
        self.connection.sync()
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            self.alive()
            geometry = self.view.get_geometry()
            if (geometry.width, geometry.height) == size:
                self.pause(0.4)
                self.check(f"resize_{size[0]}x{size[1]}", "pass", observed=list(size))
                return
            time.sleep(0.05)
        raise RuntimeError(f"Requested {size}; native editor remained {geometry.width} x {geometry.height}")

    def rapid_switches(self):
        for parameter, point, label in ((1, native.PEDAL, "pedal"), (7, native.VIBRATO, "vibrato")):
            # These positive probes establish both logged callbacks and actual
            # accepted state transitions before the rapid On/Off matrix.
            self.exact_gestures(f"{label}_initial_on", parameter, [1.0], lambda: self.click(point))
            self.exact_gestures(f"{label}_initial_off", parameter, [0.0], lambda: self.click(point))
            # Off double -> Off; Off triple -> On; On double -> On; On triple -> Off.
            for initial in (0, 1):
                for presses in (2, 3):
                    press_times = []

                    def clicks():
                        self.pointer(point)
                        for _ in range(presses):
                            press_times.append(time.monotonic())
                            self.button(True, 0.035)
                            self.button(False, 0.035)

                    expected = [float((initial + index + 1) % 2) for index in range(presses)]
                    name = f"{label}_rapid_{presses}_from_{'on' if initial else 'off'}"
                    item = self.exact_gestures(name, parameter, expected, clicks)
                    gaps = [b - a for a, b in zip(press_times, press_times[1:])]
                    item["inter_press_seconds"] = gaps
                    self.check(name + "_rapid_stimulus", "pass" if all(0 < gap < 0.25 for gap in gaps) else "fail",
                               inter_press_seconds=gaps, strict_click_threshold_seconds=0.25)
                    self.capture(name)

    def chord(self, arrow, modifiers=()):
        codes = [self.connection.keysym_to_keycode(self.XK.string_to_keysym(name)) for name in modifiers]
        if any(not code for code in codes):
            raise RuntimeError(f"Missing native keycode for {modifiers}")
        try:
            for code in codes:
                self.xtest.fake_input(self.connection, self.X.KeyPress, detail=code)
                self.held_modifier_codes.add(code)
            self.pause(0.04)
            state = self.root.query_pointer().mask
            expected_mask = 0
            mapped = True
            for code in codes:
                code_mask = sum(1 << index for index, group in enumerate(
                    self.report["native_modifier_mapping"]) if code in group)
                mapped = mapped and code_mask != 0
                expected_mask |= code_mask
            self.report.setdefault("native_modifier_states", []).append(
                {"arrow": arrow, "modifiers": list(modifiers), "keycodes": codes,
                 "x11_state": state, "expected_mask": expected_mask})
            self.check("native_modifiers_" + "_".join((*modifiers, arrow)),
                       "pass" if mapped and (state & expected_mask) == expected_mask else "fail",
                       x11_state=state, expected_mask=expected_mask, keycodes=codes)
            self.key(arrow)
        finally:
            for code in reversed(codes):
                self.xtest.fake_input(self.connection, self.X.KeyRelease, detail=code)
                self.held_modifier_codes.discard(code)
            self.pause(0.06)

    def modifier_arrows(self):
        self.action("volume_reset_before_keyboard", self.reset_volume, 0, 0.5)
        self.exact_gestures("plain_arrow_before_shortcuts", 0, [0.51], lambda: self.key("Up"))
        self.exact_gestures("plain_reverse_before_shortcuts", 0, [0.5], lambda: self.key("Down"))
        for modifier in ("Control_L", "Alt_L", "Super_L"):
            for with_shift in (False, True):
                modifiers = (modifier, "Shift_L") if with_shift else (modifier,)
                for arrow in ("Left", "Right", "Up", "Down"):
                    name = "shortcut_" + "_".join((*modifiers, arrow))
                    item = self.action(name, lambda: self.chord(arrow, modifiers))
                    events = self.events[item["first_callback_index"]:item["last_callback_index_exclusive"]]
                    self.check(name + "_has_no_edits", "fail" if events else
                               "pass" if self.callback_ready else "incomplete",
                               callback_count=len(events), positive_callback_probe=self.callback_ready)
        self.exact_gestures("plain_arrow_after_shortcuts", 0, [0.51], lambda: self.key("Up"))
        self.exact_gestures("shift_fine_arrow_up", 0, [0.511], lambda: self.chord("Up", ("Shift_L",)))
        self.exact_gestures("shift_fine_arrow_down", 0, [0.51], lambda: self.chord("Down", ("Shift_L",)))
        self.exact_gestures("plain_reverse_after_shortcuts", 0, [0.5], lambda: self.key("Down"))

    def patch(self, bounds):
        """Read a small RGB patch using the capture format already validated by NativeRun."""
        left, top, right, bottom = bounds
        width, height = right - left, bottom - top
        self.connection.sync()
        pixels = self.view.get_image(left, top, width, height, self.X.ZPixmap, 0xffffffff)
        return self.Image.frombytes("RGB", (width, height), pixels.data, "raw", "BGRX",
                                    len(pixels.data) // height, 1)

    def wait_for_patch_change(self, bounds, before, timeout=2.0):
        expected = before.tobytes()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.patch(bounds).tobytes() != expected:
                return True
            self.pause(0.025)
        return False

    def white_holds_preserve_black_interiors(self):
        self.outside()
        self.pause(0.2)
        masks, combined = keyboard_masks()
        mask_path = self.args.output_dir / "black-interior-mask.png"
        combined.save(mask_path)
        self.report["black_interior_mask"] = {
            **native.file_record(mask_path), "black_keys": len(masks),
            "pixels": combined.histogram()[255], "edge_erosion_source_pixels": 3,
            "scope": "Every pixel inside each eroded photographed black-face polygon; antialiased edges excluded.",
            "regions": [{key: value for key, value in mask.items() if key != "mask"} for mask in masks],
        }
        notes = [note for note in range(28, 101) if note % 12 not in BLACK_PITCH_CLASSES]
        anchors = [((left + right) / 2.0, 605.0)
                   for left, right in zip(WHITE_FRONT_BOUNDARIES, WHITE_FRONT_BOUNDARIES[1:])]
        results = []
        for note, point in zip(notes, anchors, strict=True):
            self.pointer(point)
            bounds = (round(point[0]) - 4, 601, round(point[0]) + 4, 609)
            hover_patch = self.patch(bounds)
            self.button(True)
            self.wait_for_patch_change(bounds, hover_patch)
            held = self.capture(f"white-held-midi-{note:03d}")
            press_change = native.image_metrics(held.crop(bounds), hover_patch)
            black_results = [{"midi_note": mask["midi_note"], "pixels": mask["pixels"],
                              **masked_difference(held.crop(mask["bounds"]),
                                  self.reference.crop(mask["bounds"]), mask["mask"])} for mask in masks]
            self.button(False)
            correct = not press_change["exact"] and all(item["exact"] for item in black_results)
            self.check(f"white_midi_{note}_held_preserves_all_black_interiors", "pass" if correct else "fail",
                       white_press_visibly_differs_from_hover=not press_change["exact"],
                       black_regions=len(black_results), changed_black_pixels=sum(
                           item["different_pixels"] for item in black_results))
            results.append({"white_midi_note": note, "source_anchor": point,
                            "press_vs_hover_patch": press_change, "black_interiors": black_results})
        self.report["white_hold_black_interior_matrix"] = results
        self.outside()
        self.pause(0.2)

    def hover_after_timeout(self):
        for label, point in (("volume", native.VOLUME), ("white_key", native.WHITE_KEY)):
            self.outside()
            self.pause(0.15)
            self.pointer(point)
            initial = self.capture(f"{label}-hover-before-timeout")
            self.pause(FEEDBACK_EXPIRY_WAIT)
            expired = self.capture(f"{label}-hover-expired")
            # Stay within the same target: the prior hover key/parameter has not
            # changed, so only renewed movement can restore the expired feedback.
            self.pointer((point[0] + 2, point[1]))
            renewed = self.capture(f"{label}-hover-renewed-by-motion")
            initial_diff = native.image_metrics(initial, self.reference)
            expired_diff = native.image_metrics(expired, self.reference)
            renewed_diff = native.image_metrics(renewed, self.reference)
            same_feedback = native.image_metrics(renewed, initial)
            correct = (not initial_diff["exact"] and expired_diff["exact"] and
                       not renewed_diff["exact"] and same_feedback["exact"])
            self.check(f"{label}_hover_renews_after_timeout_on_same_target_motion",
                       "pass" if correct else "fail", wait_seconds=FEEDBACK_EXPIRY_WAIT,
                       initial_vs_reference=initial_diff, expired_vs_reference=expired_diff,
                       renewed_vs_reference=renewed_diff, renewed_vs_initial=same_feedback)
        self.outside()

    def run(self):
        self.launch()
        with self.Image.open(self.args.reference) as reference:
            self.reference = reference.convert("RGB")
        if self.reference.size != native.SOURCE_SIZE:
            raise RuntimeError("Reference must contain exactly 1536 x 1152 pixels")
        self.report["reference_decoded_rgb_sha256"] = hashlib.sha256(self.reference.tobytes()).hexdigest()
        self.report["native_modifier_mapping"] = [list(codes) for codes in self.connection.get_modifier_mapping()]
        for label, size in (("default", native.DEFAULT_SIZE), ("minimum", MINIMUM_SIZE),
                            ("maximum", native.SOURCE_SIZE)):
            if size != native.DEFAULT_SIZE:
                self.resize_to(size)
            idle = self.capture(f"idle-{label}-{size[0]}x{size[1]}")
            self.check(f"{label}_idle_capture_dimensions", "pass" if idle.size == size else "fail",
                       expected=list(size), observed=list(idle.size))
            if size == native.SOURCE_SIZE:
                comparison = native.image_metrics(idle, self.reference)
                self.check("idle_1536x1152_exact_reference_pixels", "pass" if comparison["exact"] else "fail",
                           comparison=comparison)
            self.pointer(native.VOLUME)
            appeared = self.wait_for_patch_change((0, 0, size[0], size[1]), idle)
            feedback = self.capture(f"feedback-{label}-{size[0]}x{size[1]}")
            comparison = native.image_metrics(feedback, idle)
            self.check(f"{label}_visible_feedback_capture", "pass" if appeared and not comparison["exact"] else "fail",
                       comparison=comparison,
                       scope="Visible native Volume feedback retained for separate text-legibility assessment; no font-size guarantee.")
            self.outside()
            self.pause(0.15)
        self.rapid_switches()
        self.modifier_arrows()
        self.white_holds_preserve_black_interiors()
        self.hover_after_timeout()
        self.pause(0.2)
        final_idle = self.capture("idle-after-all-interactions-1536x1152")
        comparison = native.image_metrics(final_idle, self.reference)
        self.check("final_idle_exact_reference_pixels", "pass" if comparison["exact"] else "fail",
                   comparison=comparison)
        self.close_during_drag()

    def cleanup(self):
        for code in tuple(self.held_modifier_codes):
            self.xtest.fake_input(self.connection, self.X.KeyRelease, detail=code)
            self.held_modifier_codes.discard(code)
        super().cleanup()


def arguments():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editorhost", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True, help="new or empty result directory")
    parser.add_argument("--reference", type=Path,
                        default=native.ROOT / "plugins/vst3/resources/rhodes-suitcase.png")
    parser.add_argument("--host-label", default="unspecified", help="e.g. accepting-diagnostic")
    parser.add_argument("--source", type=Path, action="append", default=[],
                        help="additional source/provenance file to hash")
    parser.add_argument("--startup-timeout", type=float, default=15.0)
    args = parser.parse_args()
    for name in ("editorhost", "bundle", "reference", "output_dir"):
        setattr(args, name, getattr(args, name).expanduser().resolve())
    args.source = [path.expanduser().resolve() for path in args.source]
    for path in [args.editorhost, args.reference, *args.source]:
        if not path.is_file():
            parser.error(f"Expected an existing input file: {path}")
    if not args.bundle.is_dir() or args.bundle.suffix != ".vst3":
        parser.error("--bundle must be an existing .vst3 directory")
    if not math.isfinite(args.startup_timeout) or args.startup_timeout <= 0:
        parser.error("--startup-timeout must be positive and finite")
    if args.output_dir.exists() and (not args.output_dir.is_dir() or any(args.output_dir.iterdir())):
        parser.error("--output-dir must be empty; retained observations are never overwritten")
    return args


def main():
    args = arguments()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    sources = [Path(__file__), Path(native.__file__), args.editorhost, args.reference,
               *sorted(native.ROOT.joinpath("plugins/vst3").glob("photo*")),
               native.ROOT / "plugins/vst3/controller.cpp", native.ROOT / "plugins/vst3/processor.cpp",
               *sorted(path for path in args.bundle.rglob("*") if path.is_file()), *args.source]
    report = {
        "schema_version": 1, "started_utc": native.utc_now(), "platform": platform.platform(),
        "python": sys.version, "host_label": args.host_label,
        "audio_scope": "Not exercised: the accepting SDK editorhost has no audio-processing loop.",
        "input_scope": "XTEST native pointer and keyboard events; native resize and WM_DELETE_WINDOW.",
        "callback_boundary": "Positive accepted value transitions precede negative shortcut checks; missing callbacks are incomplete.",
        "source_files": [], "checks": [],
    }
    runner = None
    try:
        report["source_files"] = [native.file_record(path) for path in dict.fromkeys(sources)]
        report["dependencies"] = {name: importlib.metadata.version(name) for name in ("Pillow", "python-xlib")}
        runner = InteractionRun(args, report)
        runner.run()
    except Exception as exc:
        report["checks"].append({"name": "runtime", "status": "fail", "error": str(exc)})
        report["traceback"] = traceback.format_exc()
        print(f"FAIL native interaction verification: {exc}", file=sys.stderr, flush=True)
    finally:
        if runner is not None:
            try:
                runner.cleanup()
            except Exception as exc:
                report["checks"].append({"name": "cleanup", "status": "fail", "error": str(exc)})
        changed = []
        for original in report["source_files"]:
            path = Path(original["path"])
            if not path.is_file() or native.file_record(path)["sha256"] != original["sha256"]:
                changed.append(str(path))
        report["checks"].append({"name": "source_inputs_unchanged_during_run",
                                 "status": "fail" if changed else "pass", "changed_paths": changed})
        report["finished_utc"] = native.utc_now()
        states = {check["status"] for check in report["checks"]}
        result = 1 if "fail" in states else 2 if "incomplete" in states else 0
        report["result"] = {"exit_code": result, "status": {0: "pass", 1: "fail", 2: "incomplete"}[result]}
        report_path = args.output_dir / "outcome.json"
        report_path.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
        print(f"{report['result']['status'].upper()} report: {report_path}", flush=True)
    return result


if __name__ == "__main__":
    raise SystemExit(main())
