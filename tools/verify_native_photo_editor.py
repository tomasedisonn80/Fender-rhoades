#!/usr/bin/env python3
r"""Exercise the real Linux photo editor on an existing X11 DISPLAY.

Developer-only dependencies are Pillow and python-xlib (with XTEST support).
No dependencies are downloaded and no SDK or plug-in files are modified. Use a
dedicated display at least 1600 x 1200; 1920 x 1440 is recommended. The stock SDK
editorhost rejects edits and Release builds suppress its callback logs. Such a
run establishes attachment/rendering only; missing callback evidence returns 2
(incomplete) if no other check fails. A host
that accepts edits and emits SDK-style beginEdit/performEdit/endEdit lines can
qualify the gesture checks. Neither host mode processes audio in this tool.

Example:
  python3 tools/verify_native_photo_editor.py --editorhost /path/editorhost \
    --bundle /path/RhodesResearch.vst3 --output-dir /path/new-results \
    --reference plugins/vst3/resources/rhodes-suitcase.png

Exit status: 0 all observable checks pass; 1 a check or runtime fails;
2 evidence is incomplete (for example, edit callbacks are not logged).
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.metadata
import json
import math
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import threading
import time
import traceback


ROOT = Path(__file__).resolve().parents[1]
SOURCE_SIZE = (1536, 1152)
DEFAULT_SIZE = (1024, 768)
# Independent visual anchors in original-image pixels, checked against the
# supplied photograph; these are UI test coordinates, not physical measures.
VOLUME = (319, 458)
PEDAL = (776, 932)
VIBRATO = (560, 465)
WHITE_KEY = (716, 603)
CALLBACK = re.compile(
    r"\b(beginEdit|performEdit|endEdit)(?: called)?\s*\(\s*(\d+)"
    r"(?:\s*,\s*([-+0-9.eE]+))?\s*\)")


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def file_record(path: Path) -> dict:
    data = path.read_bytes()
    return {"path": str(path.resolve()), "sha256": hashlib.sha256(data).hexdigest(),
            "bytes": len(data)}


def image_metrics(actual, expected) -> dict:
    from PIL import ImageChops, ImageStat

    if actual.size != expected.size:
        return {"exact": False, "actual_size": list(actual.size),
                "expected_size": list(expected.size), "dimension_mismatch": True}
    difference = ImageChops.difference(actual.convert("RGB"), expected.convert("RGB"))
    channels = difference.split()
    any_channel = ImageChops.lighter(ImageChops.lighter(channels[0], channels[1]), channels[2])
    total = actual.width * actual.height
    changed = total - any_channel.histogram()[0]
    statistics = ImageStat.Stat(difference)
    rms = math.sqrt(sum(value * value for value in statistics.rms) / 3.0)
    return {"exact": changed == 0, "pixels": total, "different_pixels": changed,
            "different_pixel_fraction": changed / total,
            "max_abs_channel_error": max(high for _, high in difference.getextrema()),
            "mean_abs_channel_error": sum(statistics.mean) / 3.0,
            "rms_channel_error": rms, "difference_bbox": difference.getbbox(),
            "psnr_db": None if rms == 0 else 20.0 * math.log10(255.0 / rms)}


def gesture_summary(events: list[dict], parameter: int) -> dict:
    selected = [event for event in events if event["parameter_id"] == parameter]
    opened = False
    complete = 0
    errors = []
    values = []
    for event in selected:
        kind = event["kind"]
        if kind == "beginEdit":
            if opened:
                errors.append("beginEdit received before prior endEdit")
            opened = True
        elif kind == "performEdit":
            if not opened:
                errors.append("performEdit received outside a gesture")
            value = event.get("value")
            if value is None or not math.isfinite(value) or not 0.0 <= value <= 1.0:
                errors.append("performEdit value is not finite and normalized")
            values.append(value)
        elif kind == "endEdit":
            if not opened:
                errors.append("endEdit received without beginEdit")
            else:
                complete += 1
            opened = False
    if opened:
        errors.append("gesture has no endEdit")
    return {"callback_count": len(selected), "completed_gestures": complete,
            "values": values, "last_value": values[-1] if values else None,
            "errors": errors, "balanced": bool(selected) and not errors}


class NativeRun:
    def __init__(self, args, report: dict):
        from PIL import Image
        from Xlib import X, XK, display, error, protocol
        from Xlib.ext import xtest

        self.Image, self.X, self.XK = Image, X, XK
        self.error, self.protocol, self.xtest = error, protocol, xtest
        self.args, self.report = args, report
        self.connection = display.Display()
        self.root = self.connection.screen().root
        self.view = self.top = self.host = None
        self.focus_target = None
        self.reader = None
        self.log_file = None
        self.events = []
        self.start = time.monotonic()
        self.report["checks"] = []
        self.report["actions"] = []
        self.report["captures"] = {}
        self.report["host_callbacks"] = self.events
        self.report["x11_focus_events"] = []

    def check(self, name: str, status: str, **details):
        self.report["checks"].append({"name": name, "status": status, **details})
        print(f"{status.upper()} {name}", flush=True)

    def elapsed(self) -> float:
        return round(time.monotonic() - self.start, 6)

    def alive(self):
        if self.host.poll() is not None:
            raise RuntimeError(f"editorhost exited early with status {self.host.returncode}")

    def pause(self, seconds=0.2):
        self.connection.sync()
        time.sleep(seconds)
        self.drain_focus_events()
        self.alive()

    def drain_focus_events(self):
        while self.connection.pending_events():
            event = self.connection.next_event()
            if event.type in (self.X.FocusIn, self.X.FocusOut):
                self.report["x11_focus_events"].append({
                    "kind": "FocusIn" if event.type == self.X.FocusIn else "FocusOut",
                    "window_id": event.window.id, "mode": event.mode,
                    "detail": event.detail, "observed_seconds": self.elapsed()})

    def tree(self) -> list[dict]:
        rows = []

        def visit(window, parent, depth):
            if depth > 12:
                return
            try:
                geometry = window.get_geometry()
                attributes = window.get_attributes()
                prop = window.get_full_property(self.embed_atom, self.X.AnyPropertyType)
                rows.append({"id": window.id, "parent": parent, "depth": depth,
                             "name": window.get_wm_name(), "width": geometry.width,
                             "height": geometry.height, "x": geometry.x, "y": geometry.y,
                             "map_state": attributes.map_state,
                             "xembed_info": list(prop.value) if prop is not None else None})
                for child in window.query_tree().children:
                    visit(child, window.id, depth + 1)
            except self.error.BadWindow:
                return

        visit(self.root, None, 0)
        return rows

    def read_log(self):
        for line in self.host.stdout:
            self.log_file.write(line)
            self.log_file.flush()
            match = CALLBACK.search(line)
            if match:
                item = {"kind": match[1], "parameter_id": int(match[2]),
                        "observed_seconds": self.elapsed(), "raw": line.rstrip("\n")}
                if match[3] is not None:
                    value = float(match[3])
                    item["value"] = value if math.isfinite(value) else None
                self.events.append(item)

    def launch(self):
        screen = self.connection.screen()
        self.report["display"] = {"name": os.environ.get("DISPLAY"),
                                  "width": screen.width_in_pixels,
                                  "height": screen.height_in_pixels,
                                  "depth": screen.root_depth,
                                  "xtest": self.connection.has_extension("XTEST")}
        if screen.width_in_pixels < 1600 or screen.height_in_pixels < 1200:
            raise RuntimeError("DISPLAY must be at least 1600 x 1200 for uncropped native capture")
        if not self.connection.has_extension("XTEST"):
            raise RuntimeError("DISPLAY does not provide the XTEST input extension")
        # The pinned SDK host asks XInternAtom(..., only_if_exists=True) before
        # the plug-in sets this property; seed the atom before launching it.
        self.embed_atom = self.connection.intern_atom("_XEMBED_INFO", only_if_exists=False)
        self.connection.sync()
        existing = {row["id"] for row in self.tree()}
        command = [str(self.args.editorhost), "--componentHandler", str(self.args.bundle)]
        self.report["host_command"] = command
        self.log_file = (self.args.output_dir / "editorhost.log").open("w", encoding="utf-8")
        self.host = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     text=True, encoding="utf-8", errors="replace", bufsize=1)
        self.reader = threading.Thread(target=self.read_log, daemon=True)
        self.reader.start()
        deadline = time.monotonic() + self.args.startup_timeout
        while time.monotonic() < deadline:
            self.alive()
            rows = self.tree()
            candidates = [row for row in rows if row["id"] not in existing and
                          row["xembed_info"] is not None and row["map_state"] == self.X.IsViewable
                          and (row["width"], row["height"]) == DEFAULT_SIZE]
            if candidates:
                if len(candidates) != 1:
                    raise RuntimeError("More than one new XEmbed editor matches; use an isolated DISPLAY")
                selected = candidates[0]
                self.view = self.connection.create_resource_object("window", selected["id"])
                self.top = self.view
                while self.top.query_tree().parent.id != self.root.id:
                    self.top = self.top.query_tree().parent
                self.report["attached_window_tree"] = rows
                self.report["editor_window"] = selected
                self.report["host_window_id"] = self.top.id
                self.top.change_attributes(event_mask=self.X.FocusChangeMask)
                self.view.change_attributes(event_mask=self.X.FocusChangeMask)
                self.focus_target = self.root.create_window(
                    screen.width_in_pixels - 120, screen.height_in_pixels - 120, 80, 80, 0,
                    screen.root_depth, self.X.InputOutput, self.X.CopyFromParent,
                    background_pixel=screen.black_pixel, event_mask=self.X.FocusChangeMask)
                self.focus_target.set_wm_name("Native photo verifier focus target")
                self.focus_target.map()
                self.top.configure(x=20, y=20, stack_mode=self.X.Above)
                self.outside()
                self.pause(0.7)
                self.check("native_xembed_attachment", "pass", child_window=self.view.id)
                return
            time.sleep(0.1)
        self.report["startup_window_tree"] = self.tree()
        raise RuntimeError("No mapped 1024 x 768 XEmbed child editor appeared before timeout")

    def outside(self):
        screen = self.connection.screen()
        self.xtest.fake_input(self.connection, self.X.MotionNotify,
                              x=screen.width_in_pixels - 20, y=screen.height_in_pixels - 20)
        self.connection.sync()

    def pointer(self, point):
        geometry = self.view.get_geometry()
        origin = self.root.translate_coords(self.view, 0, 0)
        scale = min(geometry.width / SOURCE_SIZE[0], geometry.height / SOURCE_SIZE[1])
        x = round(origin.x + (geometry.width - SOURCE_SIZE[0] * scale) / 2.0 + point[0] * scale)
        y = round(origin.y + (geometry.height - SOURCE_SIZE[1] * scale) / 2.0 + point[1] * scale)
        self.xtest.fake_input(self.connection, self.X.MotionNotify, x=x, y=y)
        self.pause(0.06)

    def button(self, down: bool, delay=0.09):
        self.xtest.fake_input(self.connection, self.X.ButtonPress if down else self.X.ButtonRelease,
                              detail=1)
        self.pause(delay)

    def click(self, point):
        self.pointer(point)
        self.button(True)
        self.button(False)

    def key(self, name):
        code = self.connection.keysym_to_keycode(self.XK.string_to_keysym(name))
        if not code:
            raise RuntimeError(f"No X11 keycode for {name}")
        self.xtest.fake_input(self.connection, self.X.KeyPress, detail=code)
        self.pause(0.07)
        self.xtest.fake_input(self.connection, self.X.KeyRelease, detail=code)
        self.pause(0.12)

    def capture(self, name):
        original_name = name
        suffix = 2
        while name in self.report["captures"]:
            name = f"{original_name}-{suffix}"
            suffix += 1
        self.connection.sync()
        geometry = self.view.get_geometry()
        origin = self.root.translate_coords(self.view, 0, 0)
        screen = self.connection.screen()
        if (origin.x < 0 or origin.y < 0 or origin.x + geometry.width > screen.width_in_pixels
                or origin.y + geometry.height > screen.height_in_pixels):
            raise RuntimeError("Editor is partly off screen; refusing potentially undefined capture")
        formats = self.connection.display.info.pixmap_formats
        match = next((item for item in formats if item.depth == geometry.depth), None)
        if (match is None or match.bits_per_pixel != 32
                or self.connection.display.info.image_byte_order != self.X.LSBFirst):
            raise RuntimeError("Capture currently supports little-endian 32-bit X11 pixel storage only")
        pixels = self.view.get_image(0, 0, geometry.width, geometry.height,
                                     self.X.ZPixmap, 0xffffffff)
        stride = len(pixels.data) // geometry.height
        image = self.Image.frombytes("RGB", (geometry.width, geometry.height), pixels.data,
                                     "raw", "BGRX", stride, 1)
        path = self.args.output_dir / (name + ".png")
        image.save(path)
        self.report["captures"][name] = {**file_record(path), "width": image.width,
                                         "height": image.height,
                                         "rgb_sha256": hashlib.sha256(image.tobytes()).hexdigest(),
                                         "observed_seconds": self.elapsed()}
        return image

    def action(self, name, operation, parameter=None, expected=None):
        self.pause(0.6)  # Separate intentional single clicks from double clicks.
        start = len(self.events)
        item = {"name": name, "started_seconds": self.elapsed(),
                "first_callback_index": start, "input": "XTEST native pointer/key events"}
        operation()
        self.pause(0.15)
        item["finished_seconds"] = self.elapsed()
        item["last_callback_index_exclusive"] = len(self.events)
        if parameter is not None:
            summary = gesture_summary(self.events[start:], parameter)
            item["parameter_id"] = parameter
            item["host_gesture"] = summary
            if summary["callback_count"] == 0:
                status = "incomplete"
            else:
                correct_value = expected is None or (summary["last_value"] is not None and
                                math.isclose(summary["last_value"], expected, abs_tol=2e-5))
                status = "pass" if summary["balanced"] and summary["values"] and correct_value else "fail"
            self.check(name, status, expected_final_value=expected, host_gesture=summary)
        self.report["actions"].append(item)
        return item

    def volume_drag(self):
        self.pointer(VOLUME)
        self.button(True)
        for step in range(1, 7):
            self.pointer((VOLUME[0], VOLUME[1] - step * 6))
        self.capture("volume-drag-held")
        self.button(False)

    def reset_volume(self):
        self.pointer(VOLUME)
        # Pinned VSTGUI Linux uses a strict <250 ms inter-press threshold.
        # Keep the cursor still and deliver the two presses about 80 ms apart.
        for down in (True, False, True, False):
            self.button(down, 0.04)
        self.capture("volume-double-click-reset")

    def resize(self):
        self.outside()
        self.top.configure(width=SOURCE_SIZE[0], height=SOURCE_SIZE[1])
        self.connection.sync()
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            self.alive()
            geometry = self.view.get_geometry()
            if (geometry.width, geometry.height) == SOURCE_SIZE:
                self.pause(0.5)
                self.check("resize_1536x1152", "pass", observed=[geometry.width, geometry.height])
                return
            time.sleep(0.1)
        raise RuntimeError(f"Native child did not resize to 1536 x 1152; observed {geometry.width} x {geometry.height}")

    def focus_loss(self):
        self.pointer(VOLUME)
        self.button(True)
        self.pointer((VOLUME[0], VOLUME[1] - 18))
        before = len(self.events)
        self.focus_target.set_input_focus(self.X.RevertToPointerRoot, self.X.CurrentTime)
        self.pause(0.3)
        ended = any(event["kind"] == "endEdit" and event["parameter_id"] == 0
                    for event in self.events[before:])
        self.report["focus_loss"] = {"method": "XSetInputFocus to separate sibling window during XTEST drag",
                                     "focus_window_id": self.connection.get_input_focus().focus.id,
                                     "end_edit_before_button_release": ended}
        self.capture("focus-loss-during-drag")
        self.button(False)
        self.check("focus_loss_ends_gesture_before_pointer_release",
                   "pass" if ended else ("fail" if self.events else "incomplete"))

    def keyboard_surface(self):
        self.pointer(WHITE_KEY)
        hover = self.capture("keyboard-hover")
        self.button(True)
        held = self.capture("keyboard-held")
        self.button(False)
        released = self.capture("keyboard-released")
        changed = image_metrics(held, released)
        self.report["keyboard_visuals"] = {
            "anchor_source_pixels": WHITE_KEY, "hover_vs_held": image_metrics(held, hover),
            "held_vs_released": changed,
            "scope": "Observed UI feedback and release only; editorhost does not process audio."}
        self.check("photographed_keyboard_down_up_feedback", "pass" if not changed["exact"] else "fail",
                   comparison=changed)
        self.outside()
        self.pause(0.25)

    def keyboard_focus_loss(self):
        self.pointer(WHITE_KEY)
        self.button(True)
        self.focus_target.set_input_focus(self.X.RevertToPointerRoot, self.X.CurrentTime)
        self.pause(0.3)
        image = self.capture("keyboard-focus-loss")
        comparison = image_metrics(image, self.reference)
        self.check("keyboard_focus_loss_clears_visual_hold", "pass" if comparison["exact"] else "fail",
                   comparison=comparison,
                   scope="Visual hold release; audio/processor queue release is not observed")
        self.button(False)
        self.outside()

    def xembed_active(self, active):
        # A separate protocol probe, deliberately distinguished from the
        # native FocusOut path above. This does not prove host focus delivery.
        event = self.protocol.event.ClientMessage(
            window=self.view, client_type=self.connection.intern_atom("_XEMBED"),
            data=(32, [self.X.CurrentTime, 1 if active else 2, 0, 0, 0]))
        self.view.send_event(event, event_mask=self.X.NoEventMask)
        self.pause(0.2)

    def xembed_volume_loss(self):
        self.xembed_active(True)
        self.pointer(VOLUME)
        self.button(True)
        self.pointer((VOLUME[0], VOLUME[1] - 18))
        start = len(self.events)
        self.xembed_active(False)
        ended = any(event["kind"] == "endEdit" and event["parameter_id"] == 0
                    for event in self.events[start:])
        self.capture("direct-xembed-volume-deactivation")
        self.check("direct_xembed_deactivation_ends_held_gesture",
                   "pass" if ended else ("fail" if self.events else "incomplete"),
                   scope="Explicit XEMBED_WINDOW_DEACTIVATE packet; independent of host focus forwarding")
        self.button(False)
        self.xembed_active(True)

    def xembed_keyboard_loss(self):
        self.xembed_active(True)
        self.pointer(WHITE_KEY)
        self.button(True)
        self.xembed_active(False)
        image = self.capture("direct-xembed-keyboard-deactivation")
        comparison = image_metrics(image, self.reference)
        self.check("direct_xembed_deactivation_clears_keyboard_visual_hold",
                   "pass" if comparison["exact"] else "fail", comparison=comparison,
                   scope="Explicit XEMBED_WINDOW_DEACTIVATE packet; no audio observation")
        self.button(False)
        self.xembed_active(True)
        self.outside()

    def close_during_drag(self):
        self.pause(0.6)
        start = len(self.events)
        self.pointer(VOLUME)
        self.button(True)
        self.pointer((VOLUME[0], VOLUME[1] - 18))
        self.capture("close-with-held-volume")
        close_event = self.protocol.event.ClientMessage(
            window=self.top, client_type=self.connection.intern_atom("WM_PROTOCOLS"),
            data=(32, [self.connection.intern_atom("WM_DELETE_WINDOW"), self.X.CurrentTime, 0, 0, 0]))
        self.top.send_event(close_event, event_mask=self.X.NoEventMask)
        self.connection.sync()
        try:
            result = self.host.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            self.check("native_close", "fail", reason="Host did not exit after WM_DELETE_WINDOW")
            return
        self.reader.join(timeout=1.0)
        self.check("native_close", "pass" if result == 0 else "fail", returncode=result,
                   method="WM_DELETE_WINDOW while XTEST button remains held; no window manager required")
        summary = gesture_summary(self.events[start:], 0)
        self.check("close_ends_held_gesture", "pass" if summary["balanced"] else
                   ("fail" if summary["callback_count"] else "incomplete"), host_gesture=summary)
        self.report["actions"].append({"name": "close_during_volume_drag",
                                       "first_callback_index": start,
                                       "last_callback_index_exclusive": len(self.events),
                                       "host_gesture": summary})

    def run(self):
        self.launch()
        with self.Image.open(self.args.reference) as reference:
            self.reference = reference.convert("RGB")
        if self.reference.size != SOURCE_SIZE:
            raise RuntimeError("Reference PNG must be exactly 1536 x 1152")
        self.report["reference_decoded_rgb_sha256"] = hashlib.sha256(self.reference.tobytes()).hexdigest()
        idle = self.capture("idle-1024x768")
        self.check("idle_1024x768_capture", "pass" if idle.size == DEFAULT_SIZE else "fail")
        self.action("volume_drag_1024", self.volume_drag, 0, 0.7)
        self.action("volume_double_click_reset_1024", self.reset_volume, 0, 0.5)
        self.resize()
        idle = self.capture("idle-1536x1152")
        comparison = image_metrics(idle, self.reference)
        self.report["full_size_idle_comparison"] = comparison
        self.check("full_size_idle_exact_reference_pixels", "pass" if comparison["exact"] else "fail",
                   comparison=comparison)
        if not comparison["exact"]:
            from PIL import ImageChops
            ImageChops.difference(idle, self.reference).save(self.args.output_dir / "idle-reference-difference.png")
        self.action("volume_drag_1536", self.volume_drag, 0, 0.7)
        self.action("volume_double_click_reset_1536", self.reset_volume, 0, 0.5)
        self.action("volume_keyboard_up", lambda: self.key("Up"), 0, 0.51)
        self.action("volume_keyboard_down", lambda: self.key("Down"), 0, 0.5)
        self.action("pedal_on", lambda: self.click(PEDAL), 1, 1.0)
        self.action("pedal_off", lambda: self.click(PEDAL), 1, 0.0)
        self.action("vibrato_on", lambda: self.click(VIBRATO), 7, 1.0)
        self.action("vibrato_off", lambda: self.click(VIBRATO), 7, 0.0)
        self.action("photographed_keyboard_down_up", self.keyboard_surface)
        self.action("photographed_keyboard_focus_loss", self.keyboard_focus_loss)
        self.action("volume_focus_loss", self.focus_loss, 0, 0.6)
        self.action("volume_reset_after_focus_loss", self.reset_volume, 0, 0.5)
        self.action("direct_xembed_volume_deactivation", self.xembed_volume_loss, 0, 0.6)
        self.action("volume_reset_after_xembed_deactivation", self.reset_volume, 0, 0.5)
        self.action("direct_xembed_keyboard_deactivation", self.xembed_keyboard_loss)
        self.outside()
        self.pause(0.3)
        final_idle = self.capture("idle-after-interactions")
        comparison = image_metrics(final_idle, self.reference)
        self.check("idle_after_interactions_exact_reference_pixels",
                   "pass" if comparison["exact"] else "fail", comparison=comparison)
        self.close_during_drag()

    def cleanup(self):
        try:
            self.xtest.fake_input(self.connection, self.X.ButtonRelease, detail=1)
            self.connection.sync()
        except Exception:
            pass
        if self.host is not None and self.host.poll() is None:
            self.report["forced_host_cleanup"] = True
            self.host.terminate()
            try:
                self.host.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                self.host.kill()
                self.host.wait(timeout=3.0)
        if self.reader is not None:
            self.reader.join(timeout=1.0)
        if self.host is not None:
            self.report["host_returncode"] = self.host.returncode
            if self.host.stdout is not None:
                self.host.stdout.close()
        if self.log_file is not None:
            self.log_file.close()
        if self.focus_target is not None:
            self.focus_target.destroy()
            self.connection.sync()
        self.connection.close()


def arguments():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editorhost", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True, help="new or empty result directory")
    parser.add_argument("--reference", type=Path, default=ROOT / "plugins/vst3/resources/rhodes-suitcase.png")
    parser.add_argument("--host-label", default="unspecified", help="e.g. stock-sdk or accepting-diagnostic")
    parser.add_argument("--source", type=Path, action="append", default=[], help="additional source/provenance file to hash")
    parser.add_argument("--startup-timeout", type=float, default=15.0)
    args = parser.parse_args()
    for name in ("editorhost", "bundle", "reference", "output_dir"):
        setattr(args, name, getattr(args, name).expanduser().resolve())
    for name in ("editorhost", "reference"):
        if not getattr(args, name).is_file():
            parser.error(f"--{name} must be an existing file")
    if not args.bundle.is_dir() or args.bundle.suffix != ".vst3":
        parser.error("--bundle must be an existing .vst3 directory")
    if not math.isfinite(args.startup_timeout) or args.startup_timeout <= 0:
        parser.error("--startup-timeout must be positive and finite")
    if args.output_dir.exists() and any(args.output_dir.iterdir()):
        parser.error("--output-dir must be empty; retained observations are never overwritten")
    return args


def main() -> int:
    args = arguments()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    paths = [Path(__file__), args.editorhost, args.reference,
             ROOT / "plugins/vst3/photo_editor.cpp", ROOT / "plugins/vst3/photo_geometry.hpp",
             ROOT / "plugins/vst3/controller.cpp", ROOT / "plugins/vst3/processor.cpp",
             *sorted(path for path in args.bundle.rglob("*") if path.is_file()), *args.source]
    report = {"schema_version": 1, "started_utc": utc_now(), "platform": platform.platform(),
              "python": sys.version, "host_label": args.host_label,
              "audio_scope": "Not exercised: editorhost provides no audio-processing loop.",
              "input_scope": "XTEST pointer/key events, XSetInputFocus, native WM_DELETE_WINDOW; separately labeled direct XEmbed deactivation probes",
              "source_files": [], "checks": []}
    runner = None
    try:
        report["source_files"] = [file_record(path) for path in dict.fromkeys(paths)]
        report["dependencies"] = {name: importlib.metadata.version(name) for name in ("Pillow", "python-xlib")}
        runner = NativeRun(args, report)
        runner.run()
    except Exception as exc:
        report["checks"].append({"name": "runtime", "status": "fail", "error": str(exc)})
        report["traceback"] = traceback.format_exc()
        print(f"FAIL native verification: {exc}", file=sys.stderr, flush=True)
    finally:
        if runner is not None:
            try:
                runner.cleanup()
            except Exception as exc:
                report["checks"].append({"name": "cleanup", "status": "fail", "error": str(exc)})
        changed = []
        for original in report["source_files"]:
            path = Path(original["path"])
            if not path.is_file() or file_record(path)["sha256"] != original["sha256"]:
                changed.append(str(path))
        report["checks"].append({"name": "source_inputs_unchanged_during_run",
                                  "status": "fail" if changed else "pass", "changed_paths": changed})
        report["finished_utc"] = utc_now()
        states = {check["status"] for check in report["checks"]}
        result = 1 if "fail" in states else 2 if "incomplete" in states else 0
        report["result"] = {"exit_code": result, "status": {0: "pass", 1: "fail", 2: "incomplete"}[result]}
        report_path = args.output_dir / "outcome.json"
        report_path.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        print(f"{report['result']['status'].upper()} report: {report_path}", flush=True)
    return result


if __name__ == "__main__":
    raise SystemExit(main())
