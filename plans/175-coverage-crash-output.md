# 175 - Coverage crash output

## Motivation

The simulator coverage runner captured child output but discarded it. When a
scenario died by signal, the coverage job named the signal without preserving
the simulator's final diagnostic lines.

## Change

Scenario results retain the last 64 KiB of simulator output as bytes before
decoding, so malformed output cannot hide a crash report. The tail is printed
only for timeouts and unexpected exits, while expected invalid-scenario exits
remain quiet. Coverage accounting and simulator behavior are unchanged.

## Verification

`tests/test_coverage_tooling.py` exercises the byte bound and verifies that the
tail retains the final crash detail. Build and test execution are intentionally
left to the root validation lane.
