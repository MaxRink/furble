# 352 - M5GFX board pinning

Status: implemented on `fix/m5gfx-board-pin`. No hardware run. The mechanism,
the artifact evidence and the regression check are below; the last section
lists what is still unverified.

Related: [42](42-waveshare-eth-node.md) (the Waveshare node
whose pin this fixes), [314](../314-memory-resilience.md) (the pin that saved
about 22 KB on the ESP32 images), [322](322-hardware-abstraction.md) (what a
new board has to do with this). Patches: `patches/m5gfx_board.patch` and
`patches/m5gfx_board_noprobe.patch`, both applied by
`post:patches/m5gfx_board.py`. Check: `tools/check_m5gfx_pinning.py`.

## The defect

`platformio.ini` pins `M5GFX_BOARD=board_t::board_X` on an image that runs on
exactly one M5 board, and `patches/m5gfx_board.patch` assigned it at the top of
`M5GFX::autodetect`:

```cpp
#if defined ( M5GFX_BOARD )
    board = M5GFX_BOARD;
#endif
```

That assignment is enough for a board M5GFX can configure, because every arm of
the function is guarded by `board == 0 || board == board_t::board_Y`. With
`board` already pinned, only Y's own arm can match and the compiler drops the
rest.

It is not enough for the Waveshare ESP32-S3-ETH, which pins
`board_M5StampS3` (136). M5GFX 0.2.27 has no `board_M5StampS3` arm anywhere in
`M5GFX.cpp`: the ESP32-S3 switch covers `board_M5StackCoreS3`, `CoreS3SE`,
`StackChan`, `Dial`, `StopWatch`, `PaperMono`, `ChainCaptain`, `PaperColor`,
`PaperS3`, `PaperDIY`, the `AtomS3` variants, `DinMeter`, `StickS3`, `Tab5`,
`ArduinoNessoN1`, `UnitC6L`, `toughC5` and `CoreMatrix`, not the StampS3. No
guard matched, control reached

```cpp
    board = board_t::board_unknown;
    goto init_clear;
```

and `M5GFX::init_impl` retried the whole function five times
(`int retry = 4; do { ... } while (board_unknown == board && --retry >= 0);`).
The image therefore ended at `board_unknown`, which is the state the pin was
supposed to remove.

The consequences were exactly those of an unpinned image. `M5Unified::begin`
calls `_check_boardtype(Display.getBoard())`, and that function runs its
ESP32-S3 identification probe only `if (board == board_unknown)`: it pulls
GPIO 4, 8, 10, 12 and 38, reads them, and classifies the board. On this board
GPIO 10 and 12 are the W5500 INT and MISO (`src/FurbleEthernet.cpp`), and 4, 8
and 38 are the TF card CS, the camera power gate and a camera data line. The
same unknown state
then made `_setup_pinmap` use the `board_unknown` row of
`_pin_table_i2c_ex_in`, GPIO 39 SDA and GPIO 38 SCL, so `In_I2C` was opened on
two camera-header pins.

The `platformio.ini` comment that the pin "compiles every probe out" was
false, and the defect was invisible to every gate: the code compiled, linked,
booted and served MQTT, it just ran the probes.

## Mechanism

Two patches, applied by the same post script and guarded by different markers.

- `patches/m5gfx_board.patch` keeps the assignment at the top of
  `autodetect`, with a comment that names the second half.
- `patches/m5gfx_board_noprobe.patch` adds the return. In the
  `#elif defined (CONFIG_IDF_TARGET_ESP32S3)` section, after `pkg_ver` is read
  and before the switch on it, the display-less family leaves early:

```cpp
#if defined ( M5GFX_BOARD )
    switch (board)
    {
    case board_t::board_M5StampS3:
    case board_t::board_M5StampS3Bat:
    case board_t::board_M5StampS3Mini:
      goto init_clear;
    default: break;
    }
#endif
```

They are separate files because `apply_patch` skips a patch whose marker is
already in the source. One combined patch would have left a libdeps tree
patched by an older revision at "present", silently keeping the defect. With
two markers the second half lands on any tree, and
`patches/test_m5gfx_board.py` fails if the markers ever move back into one
file.

`init_clear` only calls `panel(_panel_last.get())` and returns `board`, which is
the path the old fall-through took, so the panel contract does not change: a
display-less board still ends with `Panel_NULL`, `LGFX_Device::init_impl`
returns false, and M5Unified does not register a display. The jump crosses no
non-vacuous initialization because `id` and `pkg_ver` are declared above it;
that is why the guard sits after them rather than before the switch.

Why this suppresses rather than hides: every probe below the guard is still
inside an arm that begins `board == 0 || board == board_Y`. With `board` pinned
to a StampS3 id those comparisons are all false at runtime, whatever the
optimizer does, and the new case list returns before the first one runs. The
optimizer result is a size benefit, not the correctness argument.

What it does not touch:

- `m5stick-c` and `m5stack-core` compile the ESP32 branch, which the no-probe
  patch is not part of. Their panel probes (`board == board_t::board_M5StickC`
  at `M5GFX.cpp:1173`, `board == board_t::board_M5Stack` at `:1538`) are
  unchanged.
- `m5stick-s3` pins `board_M5StickS3` (26), which has its own arm at
  `M5GFX.cpp:2837`; the guard does not match it and that arm still runs.
- The shared images (`m5stick-c-plus`, `m5stack-core2`) and the unpinned
  headless profile set no pin and are untouched.

## M5Unified after the pin

With `Display.getBoard()` returning 136:

- `_check_boardtype(136)` skips the S3 probe, because the whole probe block is
  inside `if (board == board_unknown)`. No GPIO is configured, pulled or read.
- `_setup_pinmap(136)` selects the `board_M5StampS3` row. Its internal I2C pins
  are 255 and 255, so `_setup_i2c` calls `In_I2C.setPort(I2C_NUM_MAX, 255, 255)`
  instead of `In_I2C.begin`. M5GFX rejects that port in `i2c::init` and in every
  transaction (`i2c_port >= LGFX_I2C_PORT_NUM`), so no pad is configured and no
  bus is created. The same row records `Ex_I2C` as GPIO 15 and 13; `setPins`
  only stores them, and `Ex_I2C.begin()` runs only for `external_rtc` or
  `external_imu`, both false.
- `_setup_led(136)` builds an RMT LED on GPIO 21 (the board's WS2812). The
  object is inert until `M5.Led.display()` is called, which furble never does,
  so it costs a small heap allocation and no pin traffic.
- `M5.Rtc` and `M5.Imu` still get a `begin(&In_I2C, board)` call, because
  `In_I2C.isEnabled()` is `_port_num >= 0` and the sentinel port is
  `I2C_NUM_MAX`, which is greater than zero. `Platform::init` therefore turns
  both off for this board through `FURBLE_HAS_INTERNAL_I2C 0`, so the drivers
  are not started at all rather than started and rejected. That is the
  "M5Unified config change for a board with no display and no internal I2C".

Left alone: M5Unified drives GPIO 46 high on every ESP32-S3 before anything
else. GPIO 46 is not a W5500 or camera pin on this board, and changing it is
out of scope for this fix.

## Evidence

### Source

`python3 tools/check_m5gfx_pinning.py --env waveshare-s3-eth --require-source`
against the installed source with the old patch fails:

```
FAIL: waveshare-s3-eth: board_M5StampS3 matches no arm in the esp32s3 branch of
M5GFX::autodetect, so it falls through to board_unknown and every probe runs,
five times over the retry loop
```

With this patch it passes. The table and post-script checks cover all four
pinned envs; the source check ran for `waveshare-s3-eth` and `m5stick-s3`, the
two envs built here.

### Builds

`FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e waveshare-s3-eth` on macOS, at
`origin/master` (before) and on this branch (after). One build at a time in
the same tree, so only the sources changed:

| | before, master | after, this branch |
| :--- | --: | --: |
| `firmware.bin` | 1,529,408 | 1,529,376 |
| `.text` | 1,249,993 | 1,249,969 |
| `M5GFX::autodetect` | 136 B, 54 instructions | 137 B, 54 instructions |
| value it returns | `movi.n a2, 0` (`board_unknown`) | `movi a2, 136` (`board_M5StampS3`) |
| `M5Unified::_check_boardtype` | 561 B, 213 instructions | unchanged |
| `Panel_` symbols | 68 | 68 |
| `Touch_` symbols | 3 | 3 |
| `_detect_i2c_device` | 1 | 1 |

The 24 bytes of `.flash.text` are three symbols: `M5GFX::autodetect` +1,
`Furble::Platform::getInstance` -8 and `M5GFX::init_impl` -17. The
`Platform::getInstance` shrink is `cfg.internal_imu = false` replacing the
`Settings::load<Settings::IMU>()` result, so the setting load is gone for this
board. The source of `M5GFX::init_impl` is unchanged by this branch; its size
moved with compiler layout around the same retry loop, which is why the
`autodetect` result is the evidence and the byte count is not.

The two `autodetect` bodies have identical call and reference targets
(`operator new`, `memset`, `setPanel`, `get_pkg_ver`) and differ in the
constant they return. The 68 `Panel_` symbols are the `Panel_Device` and
`Panel_NULL` base class plus the `setPanel` plumbing, and the three `Touch_`
symbols are `m5::Touch_Class::update*`; no per-board panel or touch driver is
linked into either image, in neither state of the patch.

The other pinned env was built to prove it is unchanged. `m5stick-s3` at
`origin/master` is 1,861,920 bytes and on this branch 1,861,936, the +16 being
the `.dirty` suffix of the version string. `M5GFX::autodetect` there is 810
bytes and 317 instructions in both, its 16 call and reference targets are the
same, and its instruction sequence is identical; only the rodata addresses its
`l32r` instructions point at move, because the image around it changed. The
StickS3 arm still runs, which is what keeps that pin working.

Re-measured on the rebased branch, same commands, at `b57f0118` (before) and on
the rebased commit (after): `waveshare-s3-eth` is 1,529,440 bytes before and
1,529,424 after, `m5stick-s3` is 1,861,936 in both states, and
`M5GFX::autodetect` returns `movi a2, 136` instead of `movi.n a2, 0`. The table
above was measured at 92618bff, where the after-builds carried a `.dirty` suffix
and a shorter unambiguous hash, so the absolute counts move with the version
string. The per-symbol deltas agree: `M5GFX::autodetect` +1,
`M5GFX::init_impl` -17, `Furble::Platform::getInstance` -8.

`m5stick-s3`'s `M5GFX::autodetect` is byte-identical between the two states: 317
instructions at the same address and no differing instruction encoding. No
symbol in `.flash.text` differs in any byte. The three differing `.flash.text`
bytes that remain are a literal pool whose rodata targets moved; the rest of the
image difference is the version string.

### Binary

`M5GFX::autodetect` in `firmware.elf`, disassembled with
`xtensa-esp32s3-elf-objdump`. Before, the whole function is the bus and panel
reset plus a fall-through that M5GFX's own retry loop calls five times:

```
42020076:  call8  42023cc8 <lgfx::v1::get_pkg_ver()>
42020079:  l32i   a11, a2, 0x108
4202007c:  mov.n  a10, a2
4202007e:  call8  420210a4 <lgfx::v1::LGFX_Device::setPanel(lgfx::v1::Panel_Device*)>
42020081:  addmi  a7, a2, 0x100
42020084:  movi.n a2, 0
42020086:  retw.n
```

After, the same function returns the pin:

```
42020067:  call8  4202108c <lgfx::v1::LGFX_Device::setPanel(lgfx::v1::Panel_Device*)>
4202006a:  call8  42023cb0 <lgfx::v1::get_pkg_ver()>
4202006d:  l32i   a11, a2, 0x108
42020070:  mov.n  a10, a2
42020072:  call8  4202108c <lgfx::v1::LGFX_Device::setPanel(lgfx::v1::Panel_Device*)>
42020075:  addmi  a7, a2, 0x100
42020078:  movi   a2, 136
4202007b:  retw.n
```

Neither body calls `_detect_i2c_device`, `i2c::init`, `pinMode` or a panel
constructor, so M5GFX's own probes are absent in both. That is the point of
the defect: the pin already suppressed M5GFX's probes, and it still reported
`board_unknown`.

What the return value changes is M5Unified's decision. `M5Unified::begin`
calls `_check_boardtype(Display.getBoard())` (`M5Unified.hpp:361`), and the
compiled guard in the same image is:

```
4202660e:  bnei  a10, 1, +0x10          ; pkg_ver is not LGA56
42026614:  bnez  a3, +0x82              ; board != board_unknown, return it
42026617:  movi.n a11, 4                ; otherwise probe GPIO 4
```

With 136 in `a3` the branch is taken straight to `retw`, so the
`pin_backup_t` setup for GPIO 4, 8, 10, 12 and 38, the `gpio::command` burst
and the `_detect_i2c_device` calls below it do not run. Before this patch `a3`
was 0 and they did.

## Regression check

`tools/check_m5gfx_pinning.py`, wired into `tools/pre_review.py` and into the
firmware matrix job in `.github/workflows/main.yml` after the build. It checks:

1. every release env in `REQUIRED_PINS` still resolves its board through
   `build_flags`, including the `${env:....build_flags}` chains of the debug
   envs;
2. every env that pins a board still resolves `post:patches/m5gfx_board.py` in
   its `extra_scripts`, so nothing can pin the macro without patching the
   library;
3. the patched `M5GFX::autodetect` names the pinned board before the first
   probe in the branch that env compiles: an arm for a configurable board, the
   no-probe case list for a display-less one.

It is a source check on purpose. The runtime suppression comes from the guard
that wraps each probe, not from the optimizer, so the source is where a
regression appears. `tests/test_check_m5gfx_pinning.py` covers each failure,
including the exact defect: a display-less pin that matches no arm.
`patches/test_m5gfx_board.py` covers the other half, that both patches apply
and stay independently identified, and the reproducible workflow runs it with
the other patch isolation tests.

## Unverified

- No hardware run. Nothing here measures the pins with a scope; the claim that
  the probes no longer run is from the source and the linked image.
- The Waveshare image has no bench session behind it in this branch, so the
  M5GFX return value is proven in the binary, not observed on a device.
- `m5stick-c` and `m5stack-core` were not rebuilt for this branch (the
  no-probe patch is outside the branch they compile, see above). `m5stick-s3`
  was.
- M5Unified is not patched, so its identification probe and its 255-pin
  sentinel handling stay in the image; they are unreachable for this pin, not
  compiled out.
- `FURBLE_HAS_INTERNAL_I2C 0` is compile-verified for the Waveshare only. No
  device confirms that its RTC and IMU drivers stay down, or that the inert
  RMT LED object on GPIO 21 costs nothing at run time.
- The full `python3 -m unittest discover` run was not completed for this
  branch. The focused suites (`tests/test_check_m5gfx_pinning.py` and
  `patches/test_m5gfx_board.py`) and the pre-review scanner set ran.
