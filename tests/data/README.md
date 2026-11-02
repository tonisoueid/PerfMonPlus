# Test data

## golden-1.pmptrace

A 20-sample recording at 1 Hz, written by `tests/trace/golden.hpp` (fixed arithmetic only). It claims to come from
`macOS 15` on a fabricated CPU, so it stands in for "a trace recorded on another operating system".

* 4 logical CPUs (2 performance, 2 efficiency), 1 GPU, 5 processes (`compiler`, `browser`, `editor`, `indexer`, `shell`).
* A 40-second hole before sample 12 (a sleep): `gapBefore(12)` is true and the wall time jumps by 41 s.
* `cpu.total` of sample *i* is `10 + 2.5·i`, so sample 7 is exactly **27.5**.
* `disk.combined.writeBps` is a *measured zero*; `net.combined.txBps` is *Restricted*; process GPU is *Unsupported*.

The test `the golden file regenerates byte for byte` rebuilds the recording under a different time zone and locale
and compares it to this file, so any host-dependent byte in the format fails it.

To regenerate after an intentional format change (which also needs a format version bump):

    PMP_WRITE_GOLDEN=tests/data/golden-1.pmptrace ./build/tests/pmp_tests "[golden]"
