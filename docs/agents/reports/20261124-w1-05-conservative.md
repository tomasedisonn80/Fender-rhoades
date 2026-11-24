# Wave 1, assignment 05 — conservative contact maximum

**Agent:** `/root/w1_02_contact/w1_05_conservative`. Read-only review of the
independent `conservative_impact` fixture in
`tools/validate_contact_reference.py`, `exact_free`, and the adopted
[analytic slice](../../specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md).
Only this report changed; no solver, state or Git mutation was performed.

## Independent derivation

With modal mass `m=0.002 kg`, hammer mass `mh=0.008 kg`, initial relative
velocity `w0=1 m/s`, zero initial compression and `k=20000 N/m`, reciprocal
forces give `delta''=-k*(1/m+1/mh)*delta`. Thus
`mu=m*mh/(m+mh)`, `omega=sqrt(k/mu)` and
`delta=w0*sin(omega*t)/omega` while compressed. Its derivative is
`w0*cos(omega*t)`, so exactly one positive maximum occurs between onset and
separation: `tmax=pi/(2*omega)`, `Dmax=w0/omega`, `Fmax=k*Dmax`.
At separation `pi/omega`, relative speed is `-w0`; subsequent free motion
decreases the gap and cannot recontact during the fixture. The second mode has
zero hammer projection and zero initial state, so it does not alter this result.

A separate standard-library Python calculation, without importing any candidate
or oracle module, gave:

| Quantity | Computed value |
| --- | ---: |
| Compression maximum | `0.00028284271247461896 m` |
| Force maximum | `5.656854249492379 N` |
| Maximum time | `0.00044428829381583655 s` |
| Separation time | `0.0008885765876316731 s` |

A 60-digit Decimal calculation using nominal decimal fixture masses gave
`0.0002828427124746190097603377448419396 m` and
`5.656854249492380195206754896838792 N`. These confirm the retained assignment
54 targets to floating-point roundoff; no incorrect target was found.

## Oracle and comparison cautions

- Derive reduced mass from the independent definition and retain that definition
  hash. Binary64 multiplication/division can differ by a few ULPs from a rounded
  nominal `0.0016`; neither result warrants an exact-real enclosure claim.
- Represent known analytic endpoints explicitly. Evaluating `sin(pi)` directly
  produced a spurious `3.46e-20 m` positive gap at separation in the probe.
  This is roundoff, not a new contact event or physical force.
- Maximum times are isolated and lie inside the complete `0..0.002 s` fixture.
  Compare their physical times with the supplemental budget; do not require the
  independent time to lie inside the candidate replay bracket.
- Both damper records are **disabled**, with zero values and no meaningful
  isolated maximum. Do not conflate them with an enabled contact's zero plateau.

Project validation, generated-state drift checking and `next --json` passed.
No candidate sweep, build or physical measurement ran. This analytic result
supports only the declared synthetic fixture and leaves PHY-002 acceptance open.
