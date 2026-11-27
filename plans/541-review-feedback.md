# 541 - Review feedback for the board-level document branch

Review-feedback record for PR #541 (`docs/board-level-documents`), branch head
`f7e3d97d`. An independent adversarial review raised two blocking findings and
three non-blocking ones. Each is recorded here with its disposition: fixed,
already fixed, or a false positive with the evidence.

The review re-fetched the registered documents and compared bytes, not titles,
re-derived the three code claims from the drawings and from the vendor's own
source and hardware pages, and attacked the register where a hash register can
lie. Its verification of the rest of the branch is recorded at the end.

## Blocking

### B1 - the V_BCKP conclusion was read off the wrong preprocessor branch

Reported at `docs/power/board-document-gaps.md:76-78`: the branch claimed that
"Under `T_BEAM_S3_SUPREME` the vendor's own `LoRaBoards.cpp` sets
`XPOWERS_ALDO4` to 3300 mV ... and sets `XPOWERS_VBACKUP` to 3300 mV". The
ALDO4 half is right and the VBACKUP half is not.

Reproduced before changing anything. The 3300 mV `XPOWERS_VBACKUP` write is at
`examples/Factory/LoRaBoards.cpp:152-153`, inside
`#if defined(CONFIG_IDF_TARGET_ESP32)` which opens at `:137` and closes at
`:169`. That is the classic ESP32 T-Beam, not the Supreme. The Supreme guard
opens at `:172`, sets ALDO4 and ALDO3 under `//gps` and `// lora`, and then, at
`:218` under `//not use channel`, disables DCDC2, DLDO1, DLDO2 and at `:224`
`PMU->disablePowerOutput(XPOWERS_VBACKUP)`. `powerOff()` also disables VBACKUP,
in the shared AXP2101 block rather than a Supreme-only one.

The vendor hardware page says the same thing twice. In
`docs/en/t_beam_supreme/t_beam_supreme_hw.md` the rail table lists
`| LDO1(VRTC) | Unused | X |` at `:178` and `| VBACKUP | Unused | X |` at
`:187`, and the note under it reads "T-Beam Supreme GPS backup power comes from
18650 battery. If you remove the 18650 battery, you will not be able to get GPS
hot start."

Fixed. The `[inferred]` tag is back on the M10S backup path, and every place
that inherited the stronger claim now says what is actually documented:

- `docs/power/board-document-gaps.md`: the 5.1 row no longer claims a VBACKUP
  rail; section 3 item 3 states that the file does not say that for the backup,
  with the branch and line evidence; the "newly verifiable" list drops the
  V_BCKP bullet and says the tag stays; "what stays open" gains a V_BCKP source
  bullet naming the three disagreeing sources; the facts-changed row reads
  "Still `[inferred]` ... UNVERIFIED, unchanged".
- `docs/components/ublox-max-m10s.md`: the board-side paragraph keeps the
  drawing fact (pin 6 on a net labelled `VRTC`) and drops the VBACKUP half,
  saying instead that nothing read establishes the source.
- `plans/318-next-board-battery-gps.md`: the follow-up now opens with "one of
  the two facts ... has a named path. The other does not, and keeps its
  `[inferred]` tag", and the V_BCKP bullet carries the branch evidence.
- `plans/376-board-level-documents.md` (was 367): the M10S answer bullet and
  the facts-moved row both keep the tag.
- `plans/README.md`: the index row no longer claims the V_BCKP path.
- `docs/power/datasheet-register.md` 5.2: the `LoRaBoards.cpp` row no longer
  says the file names the `VRTC` rail, and records where the VBACKUP write
  really sits.

What is still claimed: `A_LDO4` is ALDO4 at 3300 mV for the Supreme, read from
`:172-177` inside the right guard. The review checked that and it holds.

### B2 - eight rows cited a URL that serves HTML

Reported at `docs/power/datasheet-register.md:158-165`: eight of the 28 rows
used `github.com/<org>/<repo>/blob/...`, which serves the HTML viewer, so the
registered bytes could not be reproduced from the printed URL. The registered
sizes and hashes were correct for the underlying files.

Fixed. Every LilyGO row now cites `raw.githubusercontent.com`, and the eleven
URLs were re-fetched from exactly the form now printed:

| Row | Bytes | Pages | SHA-256 |
| :--- | ---: | ---: | :--- |
| T-Beam-S3-Core.pdf | 273,619 | 3 | `f1ae1e2b7e06525c19b34f1e7bf0588f84cabe6125cc9504fbf33d7f4ae60e66` |
| T-Beam-S3-Supreme-V3.0.pdf | 380,728 | 1 | `f5c201ff8d2add86c65dfe0f41c288854ae4e397ebd7e84eda5a16b887cd143c` |
| T-Beam-S3-Supreme-V3.1.pdf | 329,664 | 1 | `42d79201cc38ce1eea7cbc9f26035029fcb14c72a9a8e328ce1d96e4380e8ec2` |
| schematic.pdf (T-Deck) | 1,137,059 | 1 | `c3dcd1de8cc7966c69c5bff182deb41c776ee36d4c3a1e32afbc2a5164269ff4` |
| T-Deck-GPS V1.0 24-03-29.pdf | 387,015 | 1 | `94ba05c69f9fc28634a4dbe56d1bf22c6e0380800ceb0620af8d3448d61add18` |
| T-Deck-L76K V1.0 24-08-12.pdf | 347,800 | 1 | `f3fdca3c64ccd8edfd9e29e2cf875e24b592e8407db1100c5f38fa811a81ecc2` |
| T-Lora Pager V1.0 SCH 25-06-13.pdf | 959,469 | 5 | `23ed5e6623aa29872cb7679b611681b6d65844032c82780e16e01a0284556bd3` |
| T-Watch Ultra V1.0 SCH 25-07-24.pdf | 778,895 | 5 | `cae987e0d19f87890df668b3313df491907cb9f6e4f5905dc285942c6219ae51` |

The three artifacts in 5.2 were converted for the same reason, because their
hashes had the same reproducibility defect: `utilities.h` 28,783 bytes
`1f234017d45099f90e3a5cb7fb6ed16b78dfe11fa57f2b82a58f9d1b9411f733`,
`LoRaBoards.cpp` 42,023 bytes
`b5ab071a1b3946f435f8d324f317cc550e7ba48a398b047ddf099a341aac5174`, and
`t_beam_supreme_hw.md` 17,088 bytes
`5e6dd0ab2390cedc1db3360f7e42beef7f7ebdcbd7a4c274bf3f4820e8199967`. Every
value is unchanged from the previous commit; only the citation form moved. Each
raw URL answered `200` with `application/octet-stream` for the PDFs and
`text/plain` for the three text files.

The four `github.com/.../blob/` URLs that remain in the register belong to
earlier passes and other vendors (the M5StickC-Plus MPU6886 driver, the ATGM336H
manual, the M5PM1 library and the M5Core2 AXP192 driver). They are outside this
branch's 28 rows and were left alone rather than widened into this diff.

## Non-blocking

### N1 - plan 318 contradicted itself

Reported: the new follow-up said the `[inferred]` tag "is replaced", while the
two rows the tag lives in were not touched.

Fixed, as part of B1. The follow-up now says the tag stays, so
`plans/318-next-board-battery-gps.md:83` and `:289` agree with it.

### N2 - the summary sentence was wider than the register

Reported: the change was described as registering each document with "the
licence finding", but the 5.1 tables have no licence column; verdicts live per
vendor group in 5.1.5.

Fixed in the PR body, which now says the licence finding is recorded per vendor
group and points at 5.1.5. The register itself was already correct at that
granularity.

### N3 - plan number 367 is claimed by three other pull requests

Reported: this branch added `plans/367-board-level-documents.md` while #538
renumbers plans into 367 to 374, #540 claims `367-hal-register-verification.md`
and #543 claims `367-board-verification.md`.

Fixed by renumbering to `plans/376-board-level-documents.md`: 367 to 374 are
claimed by #538, 375 by #545, and no open pull request claims 376 at this
commit. `plans/README.md` moves with the file.

## Verified by the review, no change needed

Recorded here so the re-check is not repeated: ten rows re-fetched with byte
size, page count and SHA-256 exact; no registered URL returns 404; the
`Revision V3.0` and `5/13/2026` title-block reading reproduces on all three
pages; the V_IO pin 7 to `A_LDO4` mapping is right and correctly left
UNVERIFIED; the Core2 v1.1 AXP2101 on sheet 4 holds, twice on that page and
zero times elsewhere, with V1.0 carrying one AXP192; zero PDFs are tracked and
no binary is in the diff; the licence conclusions are narrower than the
permissions they describe; the FCC 403 claims are accurate; the counts in the
register are what it says; and the repository's own gates were silent on both
blockers, which is why the re-check had to be a human reading of the vendor
source.
