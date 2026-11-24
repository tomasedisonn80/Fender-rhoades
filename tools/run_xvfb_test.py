#!/usr/bin/env python3
"""Run one Linux test on a private, cookie-authenticated TCP Xvfb display."""

import argparse
import math
import os
from pathlib import Path
import secrets
import signal
import socket
import subprocess
import sys
import tempfile
import time


def positive_seconds(value: str) -> float:
    seconds = float(value)
    if not math.isfinite(seconds) or seconds <= 0:
        raise argparse.ArgumentTypeError("timeout must be a finite positive number")
    return seconds


def stop_process(process: subprocess.Popen | None) -> None:
    """Reap our process and stop any descendants in its private process group."""
    if process is None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait(timeout=2)


def show_server_log(path: Path, cookie: str) -> None:
    if path.exists():
        with path.open("rb") as stream:
            stream.seek(0, os.SEEK_END)
            stream.seek(max(0, stream.tell() - 16384))
            output = stream.read().decode("utf-8", errors="replace")
        if output.strip():
            print("Xvfb output:\n" + output.replace(cookie, "<redacted>"), file=sys.stderr)


def run(args: argparse.Namespace) -> int:
    with tempfile.TemporaryDirectory(prefix="rhodes-xvfb-") as temporary:
        authority = Path(temporary) / "authority"
        authority.touch(mode=0o600)
        log_path = Path(temporary) / "Xvfb.log"
        cookie = secrets.token_hex(16)
        server = child = None
        try:
            deadline = time.monotonic() + args.startup_timeout
            with socket.socket() as probe:
                probe.bind(("127.0.0.1", 0))
                display = probe.getsockname()[1] - 6000
            if display <= 0:
                raise RuntimeError("allocated port is outside the X11 display range")
            display_name = f"127.0.0.1:{display}"
            # Supply the cookie on stdin, keeping it out of command lines/errors.
            authentication = subprocess.run(
                [args.xauth, "-q", "-f", str(authority)],
                input=f"add {display_name} MIT-MAGIC-COOKIE-1 {cookie}\n",
                text=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                timeout=max(0.001, deadline - time.monotonic()), check=False)
            if authentication.returncode != 0:
                raise RuntimeError(f"xauth exited with status {authentication.returncode}")
            environment = os.environ.copy()
            environment.update(DISPLAY=display_name, XAUTHORITY=str(authority))
            with log_path.open("wb") as output:
                server = subprocess.Popen(
                    [args.xvfb, f":{display}", "-screen", "0", "1280x900x24",
                     "-nolisten", "local", "-nolisten", "unix", "-listen", "tcp",
                     "-auth", str(authority), "-noreset"],
                    stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
                while True:
                    if server.poll() is not None:
                        raise RuntimeError(f"Xvfb exited with status {server.returncode}")
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise RuntimeError("Xvfb startup timed out")
                    try:
                        ready = subprocess.run(
                            [args.xdpyinfo], env=environment,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            timeout=min(1.0, remaining), check=False)
                        if ready.returncode == 0:
                            break
                    except subprocess.TimeoutExpired:
                        pass
                    time.sleep(min(0.05, max(0, deadline - time.monotonic())))
                child = subprocess.Popen(args.command, env=environment, start_new_session=True)
                try:
                    status = child.wait(timeout=args.test_timeout)
                    # Match shell status for a child terminated by a signal.
                    return status if status >= 0 else 128 - status
                except subprocess.TimeoutExpired:
                    print(f"run_xvfb_test: test timed out after {args.test_timeout:g}s",
                          file=sys.stderr)
                    return 124
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
            print(f"run_xvfb_test: {error}".replace(cookie, "<redacted>"), file=sys.stderr)
            show_server_log(log_path, cookie)
            return 1
        finally:
            try:
                stop_process(child)
            finally:
                stop_process(server)


def interrupted(signum: int, _frame: object) -> None:
    # Allow the first cancellation to unwind through process/temp cleanup.
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    raise SystemExit(128 + signum)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xvfb", default="Xvfb", help="configured Xvfb executable")
    parser.add_argument("--xauth", default="xauth", help="configured xauth executable")
    parser.add_argument("--xdpyinfo", default="xdpyinfo", help="authenticated readiness probe")
    parser.add_argument("--startup-timeout", type=positive_seconds, default=10.0)
    parser.add_argument("--test-timeout", type=positive_seconds, default=30.0)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.command[:1] == ["--"]:
        args.command.pop(0)
    if not args.command:
        parser.error("a test command is required after --")
    signal.signal(signal.SIGINT, interrupted)
    signal.signal(signal.SIGTERM, interrupted)
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
