# Initial loaded-panel audio fixture

The initial fixture passed one actual CTest invocation with 96 renders, 36,000
callbacks, 20,730,240 scalar comparisons and 9,255,124 checks on the three
final Linux modules. The complete log is retained; the JUnit system-out is
truncated by CTest. This observation predates the independent worker-80 review.

That review identified host setup ordering and missing stereo, transition-envelope
and Bass/Treble mapping assertions. The initial pass therefore establishes only
the assertions present in this retained source; it is not the final qualification.
The strengthened fixture and its subsequent observation are recorded separately.

CTest XML uses the container-local clock label; the progress record uses UTC.
