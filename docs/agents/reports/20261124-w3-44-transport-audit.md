# Wave 3 assignment 44 — UTF-8 timing transport audit

Worker: `/root/w3_44_transport_audit`. Reviewed commit
`18738833a778c8dec7652e224fec9621ff39f30f` and the current files below.
**No material defect found in the encoding repair.** PHY-002 remains incomplete;
this review supplies no measured fidelity or native Windows execution result.

## Findings

The sole production difference from `340ff2b` sets `encoding="utf-8"` on the
private worker's `subprocess.run`. Python's
[subprocess documentation](https://docs.python.org/3.11/library/subprocess.html)
confirms that explicit encoding controls its text pipes. This removes reliance
on the parent's locale without changing quadrature settings, numerical budgets,
or the timeout argument. It does not force arbitrary external programs to emit
UTF-8; the interface remains the module's own fixed worker.

Invalid UTF-8 raises `UnicodeDecodeError`, covered by the existing `ValueError`
handler through the documented
[exception hierarchy](https://docs.python.org/3.11/library/exceptions.html#UnicodeDecodeError).
Such decoding failures retain inconclusive timing, without invented intervals.
`_damped` computes scalar maxima and root refinements before requesting timing;
their values and status survive. Nonzero, valid UTF-8 worker stderr retains its
exit code and bounded decoded diagnostic. Missing SciPy remains separately
unavailable. No acceptance condition or candidate-parser rule was weakened.

The new regression launches a real child with Python UTF-8 mode and locale
coercion disabled, selects the C locale, and has a nested worker emit explicit
UTF-8 bytes. It asserts exact decoded failure text, return code, scalar intervals,
root evidence, and inconclusive timing. This exercises the local Linux pipe
boundary; its Windows branch retains the host locale and needs native CI.

The transport report correctly distinguishes the earlier failed Windows job,
the subsequent local repair, and pending native confirmation. I verified its
current source digests, saved replay manifest digest, and original 86-test log
digest. I did not rerun that suite or SciPy replay. The Wave 2 final JSON/archive
and W2-37 report retain their earlier identities and conclusions; their oracle
identity precedes this later encoding correction.

## Actual checks

Read the operating contract, README, roadmap and canonical revision 27.
`project.py validate`, `sync --check`, and `next --json` succeeded; no unclaimed
ready task was reported. The single focused regression below passed **1 test in
0.107 seconds**, exit 0, with no skips. All seven listed hashes matched before
and after. No build, numerical replay, broad suite, Git or state mutation ran.

```sh
python3 -m unittest discover -s tests/python -p test_contact_peak_backend_boundaries.py -k test_worker_utf8_survives_ascii_parent_locale -v
```

Next action: root checks native Windows CI; preserve historical failure evidence
and keep physical/general-extrema gates open.

## Examined SHA-256 identities

| Path | SHA-256 |
| --- | --- |
| `tools/contact_peak_oracles.py` | `e691093a11ef34ea68cbbe25614d42a2dbd4f4576535c2c35b289133e8fc2c50` |
| `tests/python/test_contact_peak_backend_boundaries.py` | `cee18099f3f26a9e71b293b59307bce64e17638e2b3c7a04a5ef69493a4df2e1` |
| `docs/validation/contact-worker-utf8-20261124.md` | `8448e425d4d8f930c0d09a6f19ea3408696ada6108128c8e43fb5a93f3d3c931` |
| `docs/validation/contact-worker-utf8-20261124.json` | `9b1e313ade2bb2bca127d7f312690c6be030ab6c7ecc1b1454b2590b3d2b1ffd` |
| `docs/validation/contact-peaks-final-replay-20261124.json` | `dfbe13724511c0437e2e6a266d1815360df35791608b6b0be5e4f0f29b60a879` |
| `docs/validation/contact-peaks-final-replay-20261124.tar.gz` | `fc48780e653fb4bdbd97dab153461797d2d7607086c8a9821e8f44f4e207e8e1` |
| `docs/agents/reports/20261124-w2-37-transport-contract.md` | `2fdc13bdc3c145c879818af50eee81a34a7d938aaadbc508a25c05b2ead5e490` |
