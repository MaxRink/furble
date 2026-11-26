# IR remote protocol

furble can fire a camera shutter over infrared instead of BLE. Three consumer
remote protocols are implemented, and the timing constants come from
reverse-engineering sources rather than vendor documents.

| Item | Value |
| :--- | :--- |
| Implementation | `src/FurbleIR.cpp`, `include/FurbleIR.h` |
| Peripheral | one RMT TX channel, copy encoder, 1 MHz resolution (`src/FurbleIR.cpp:21,225-236`) |
| Carrier duty cycle | 0.33 (`src/FurbleIR.cpp:264-268`) |
| Protocol enum | `NIKON` 0, `SONY` 1, `CANON` 2, `CANON_DELAYED` 3 (`include/FurbleIR.h:15-20`) |
| Out-of-range value | clamps to Nikon (`src/FurbleIR.cpp:136-141`) |
| Console syntax | `ir fire [protocol]`, `docs/console-commands.md` |
| Hardware page | [Board peripherals](../components/board-map.md#ir-emitter) |

## Nikon ML-L3

Carrier 38 kHz. Sources: SB-Projects and Gough Lui, both listed in
`plans/implemented/22-ir-remote-trigger.md` (`src/FurbleIR.cpp:29-30`).

| Segment | Mark | Space | Citation |
| :--- | ---: | ---: | :--- |
| Header | 2000 us | 27830 us | `src/FurbleIR.cpp:32-33` |
| Pulse 1 | 390 us | 1580 us | `src/FurbleIR.cpp:34-35` |
| Pulse 2 | 410 us | 3580 us | `src/FurbleIR.cpp:36-37` |
| Stop | 400 us | frame gap | `src/FurbleIR.cpp:38` |
| Frame gap | 63000 us total | | `src/FurbleIR.cpp:39-43` |

The 63 ms gap is split into two halves because one RMT duration is limited to
32767 us (`src/FurbleIR.cpp:40-43,68-70`). The encoder emits three frame copies
as 13 RMT symbols including the inter-frame gap
(`src/FurbleIR.cpp:66-84`).

The sources disagree on whether 63.2 ms is the gap between frames or the frame
repeat period. The code treats it as the gap and records that a hardware
waveform check will settle it (`src/FurbleIR.cpp:39-42`).

## Sony SIRC

Carrier 40 kHz. Sources: Ed Cheung and Ken Shirriff
(`src/FurbleIR.cpp:45-46`).

| Item | Value | Citation |
| :--- | :--- | :--- |
| Header | 2400 us mark, 600 us space | `src/FurbleIR.cpp:87-88` |
| Zero bit | 600 us mark | `src/FurbleIR.cpp:90` |
| One bit | 1200 us mark | `src/FurbleIR.cpp:91` |
| Bit space | 600 us | `src/FurbleIR.cpp:88` |
| Bits per frame | 20, least significant bit first | `src/FurbleIR.cpp:92,101` |
| Payload | command 0x2D in the low 7 bits, address 0x1E3A shifted left by 7 | `src/FurbleIR.cpp:49-50,95` |
| Frames per press | 3 | `src/FurbleIR.cpp:103` |
| Repeat interval | 45000 us between frame starts | `src/FurbleIR.cpp:48,105-111` |

The last bit of the first two frames gets the longer repeat space so the next
frame starts at the repeat interval, and the array is 63 symbols
(`src/FurbleIR.cpp:86,105-118`).

## Canon RC-1 and RC-6

Carrier 32.7 kHz. Source: the TU Berlin reverse-engineering notes. The Canon RC-6
handset uses the same two-burst scheme. It is **not** the Philips RC-6 protocol
(`src/FurbleIR.cpp:52-54`).

| Item | Value | Citation |
| :--- | :--- | :--- |
| Burst | 489 us | `src/FurbleIR.cpp:56` |
| Immediate gap | 7330 us | `src/FurbleIR.cpp:57` |
| Delayed gap | 5360 us, the 2 s self-timer variant | `src/FurbleIR.cpp:58` |
| Symbols | two bursts, the second followed by a 1 us terminator | `src/FurbleIR.cpp:122-129` |

## Transmission

| Step | Behaviour | Citation |
| :--- | :--- | :--- |
| Carrier | applied per frame through `rmt_apply_carrier` | `src/FurbleIR.cpp:266-278` |
| Enable | `rmt_enable` before transmit | `src/FurbleIR.cpp:280-286` |
| Transmit | one copy-encoded buffer, loop count 0 | `src/FurbleIR.cpp:288-292` |
| Wait | bounded by `RMT_TX_TIMEOUT_MS` = 1000 ms | `src/FurbleIR.cpp:24,289` |
| Disable | always runs, even on failure, because the driver holds an APB lock between enable and disable | `src/FurbleIR.cpp:291-305` |

## Error handling

- A board without an emitter logs and returns, and `fire()` becomes a no-op
  (`src/FurbleIR.cpp:145-158,193-205`).
- A failed channel or encoder creation logs the `esp_err_t` name and code and
  releases whatever it already created (`src/FurbleIR.cpp:236-260`).
- A queue that is already full logs "IR trigger already pending" instead of
  queueing a second frame, so a double press cannot emit a malformed train
  (`src/FurbleIR.cpp:207-213`).
- The task stack may live in PSRAM because the task only drives RMT and logs, and
  a failed PSRAM allocation falls back to internal RAM rather than dropping the
  feature (`src/FurbleIR.cpp:166-186`).

## Open rows

| Missing | Why the repository cannot close it | Document and section needed |
| :--- | :--- | :--- |
| Nikon ML-L3 frame timing | The two public reverse-engineering sources disagree and neither is authoritative | Nikon ML-L3 service manual or a captured waveform; otherwise the bench capture named in `plans/implemented/22-ir-remote-trigger.md` |
| Sony SIRC address meaning | Only the 20-bit payload is asserted | Sony SIRC protocol description or an LIRC configuration entry for the camera model |
| Canon RC-1 versus RC-6 framing | The code states explicitly that this is not Philips RC-6 | Canon RC-1 teardown or the TU Berlin notes, the burst timing table |
| Whether any supported camera accepts IR | Not verified | Camera manual, the remote-accessory table |
