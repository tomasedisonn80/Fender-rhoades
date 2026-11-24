# Contact timing worker UTF-8 boundary — 24 November 2026

The Windows rerun at `d95fc0fc4703e486e841fb435e040d451ef9adac`
still failed the exact Unicode-stderr assertion in
[job 138250336060](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45336692982/job/138250336060).
The explicit UTF-8 fixture bytes exposed a production assumption: the parent
used `subprocess.run(text=True)` without an encoding, leaving decoding to the
host locale. The earlier test-fixture correction alone was insufficient.
The macOS project-state job and all native C++/sanitizer jobs passed that run.

The parent now explicitly decodes the private timing worker as UTF-8. Invalid
UTF-8 remains an inconclusive timing failure through the existing exception
handler; independent scalar values remain available. No numeric formulas,
comparison budgets, process timeout, or native DSP code changed.

A new real-subprocess regression disables Python UTF-8 mode and POSIX locale
coercion, selects the C locale, and verifies the exact UTF-8 failure text and
preserved scalar results. The existing nonzero, garbled, and invalid-byte worker
tests remain intact. The complete contact discovery command passed **86 tests,
no skips, in 4.223 seconds**: 75 new diagnostics and 11 existing sweep tests.

```sh
python3 -m unittest discover -s tests/python -p 'test_contact_peak_*.py'
```

The real damped CLI with requested SciPy timing also returned **0** with the same
C-locale overrides. It reused the original archived native candidate and only
hashed the declared producer. The exact command, source hashes, output and
manifest identity are in [the transport result](contact-worker-utf8-20261124.json).
Source hashes were identical before and after that replay. The 86-test log has
an after-run identity; no before-run test-source snapshot was taken.

The original Wave 2 archives and reports remain immutable and describe their
then-tested source. This additional observation covers the later transport
change. Native Windows CI must confirm the repair separately; a local Linux
locale test is not a native Windows result. PHY-002 remains incomplete and
physical fidelity remains unvalidated.
