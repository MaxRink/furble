# 374 Control release durability

State: implemented on `fix/control-release-durability`, host-tested only. Base:
fork master `18bbcb5a`. Dispatched from plan 371 (fork network sweep) item 3, which took the claim
from the closed upstream PR #313 (`LittleBidet/xteink_x3_furble_port`). Plan 371
is not on master yet, so it is named here rather than linked.

No hardware run. The fork is in sim-only mode
([313-sim-only-status.md](313-sim-only-status.md)), so this plan claims source,
host-test and mutation evidence only.

## Motivation

Two independent statements say the same thing:

- Plan 371 item 3: "`Control::sendCommand` drops a command when the queue is
  full and only logs it, so a release queued behind a press can be lost."
- Upstream PR #313 also claims it made a release survive queue overflow.

The claim is true, and the defect is real rather than cosmetic. The drop site is
one level lower than plan 371 says, so the first section corrects that before
anything is claimed about the fix.

## Where the command actually goes

`Control::sendCommand` is a router. On the base commit it is lines 586 to 611
of `src/FurbleControl.cpp`, and it is unchanged by this work. Camera commands are handed to every target's own queue and never touch
the 32-slot control queue:

```
   600	  switch (cmd) {
   601	    case CMD_SHUTTER_PRESS:
   602	    case CMD_SHUTTER_RELEASE:
   603	    case CMD_FOCUS_PRESS:
   604	    case CMD_FOCUS_RELEASE:
   605	      return sendCameraCommand(cmd).any ? pdTRUE : pdFALSE;
   606	    default:
   607	      break;
   608	  }
   609
   610	  return xQueueSend(m_Queue, &cmd, 0);
```

The return value is "at least one target took it", so a single target that
refuses the command reads as `pdFALSE` to the caller.

The queue that matters is `Control::Target::m_Queue`, 8 entries
(`include/FurbleControl.h:98`), and the drop is in `Control::Target::sendCommand`
(lines 102 to 137 of `src/FurbleControl.cpp` on the base commit):

```
   127	  // Reserve the count before the command can be received. Incrementing after
   128	  // xQueueSend lets a fast target finish and decrement first, which can make
   129	  // the counter zero while the command is still executing.
   130	  m_InFlight.fetch_add(1, std::memory_order_acq_rel);
   131	  BaseType_t ret = xQueueSend(m_Queue, &cmd, 0);
   132	  if (ret != pdTRUE) {
   133	    m_InFlight.fetch_sub(1, std::memory_order_acq_rel);
   134	    ESP_LOGE(LOG_TAG, "Failed to send command to target.");
   135	  }
   136	  return ret;
```

`xQueueSend` with a zero timeout, so a full queue discards the command. One log
line is the only trace, the reservation is given back, and the caller sees
`pdFALSE`. `CMD_DISCONNECT` above it is the one command already protected, by
draining the queue and posting at the front (`102-125`).

## Why a lost release is not cosmetic

The target task is the only caller of `shutterRelease()` in the firmware
(lines 179 to 183 of `src/FurbleControl.cpp` on the base commit):

```
   179	      case CMD_SHUTTER_RELEASE:
   180	        m_Camera->noteConnActivity(false);
   181	        ESP_LOGI(LOG_TAG, "shutterRelease(%s)", name);
   182	        m_Camera->shutterRelease();
   183	        break;
```

Nothing in `Camera` and nothing in a vendor driver releases the shutter on a
timer. There is no hold deadline, no watchdog and no retry below this queue:

| Driver | Press | Release | A lost release leaves |
| :--- | :--- | :--- | :--- |
| Fujifilm Basic and Secure (`lib/furble/Fujifilm.cpp:187-194`, frames at `lib/furble/protocol/FujifilmProtocol.cpp:144-158`) | `{01 00}` then `{02 00}` | `{01 00}` then `{00 00}` | the camera holding S2 at full press |
| Canon EOS Remote (`lib/furble/CanonEOSRemote.cpp:69-80`) | `0x8c` | `0x0c` | the shutter bit held |
| Canon EOS Smart (`lib/furble/CanonEOSSmart.cpp:194-201`) | `{00 01}` | `{00 02}` | the shutter held |
| Nikon Remote (`lib/furble/NikonRemote.cpp:89-97`) | `{02 02}` | `{02 00}` | the shutter held |
| Nikon Smart (`lib/furble/NikonSmart.cpp:167-173`) | no-op | no-op | nothing: this profile sends no shutter command |
| Sony (`lib/furble/Sony.cpp:173-183`) | `0x0901` down | `0x0801` up | the button held down |
| Lumix (`lib/furble/Lumix.cpp:322-328`) | `0x04` | `0x05` | the button held, which is what bulb uses |
| Ricoh (`lib/furble/Ricoh.cpp:448-458`) | one `OperationRequest {0x01, 0x01}` capture | ignored on purpose | nothing: the press is the whole capture |
| DJI Osmo (`lib/furble/DJIOsmo.cpp:447-462`) | press | ignored | nothing |

The Canon Remote research note states it directly: there is "no local timeout or
camera-state confirmation beyond the underlying BLE call"
(`docs/camera-research/fuji-canon-nikon.md:337`). Ricoh and DJI are the two
immune families, because their press is a complete capture and their release is
a deliberate no-op.

The existing code already treats this as real, and two of the five producers
retry a refused release because of it:

- `include/FurbleMQTT.h:92-94`: "A rejected shutter or focus release leaves the
  camera held, so retry it on this shorter deadline", 250 ms.
- `include/FurbleWebUI.h:69` with `src/FurbleWebUI.cpp:1448-1449`: a 200 ms
  per-target release retry for the targets that still owe one.

The other producers do not retry. The touch UI sends releases at
`src/FurbleUI.cpp:2175` (shutter lock), `2221` and `2292` (bulb), `3936`
(click), `10890` (bulb timer) and `13075` (gesture). The intervalometer sends
them at `7777` on a 100 ms tick (`947`). The console reports the failure to the
user (`src/FurbleConsole.cpp:170-171`) and the companion reports it to the peer
(`src/FurbleCompanionService.cpp:1260-1324`); neither can retry on its own. So a
queue overflow is recovered only when the lost release came from MQTT or the
WebUI, which is why this belongs in the queue rather than in another caller.

## When the queue fills

Producer and consumer rates bound the window.

- The UI loop runs on `app_main`'s task, which ESP-IDF starts at priority 1 and
  no sdkconfig in this tree overrides. A target task runs at priority 3
  (`src/FurbleControl.cpp:1418`) and the control task at 4
  (`src/main.cpp:524`).
- Because a runnable target task preempts the UI, the queue holds at most one
  command in normal operation. It can only build a backlog while the target task
  is blocked inside a GATT write.
- That block has no deadline on this side. `Camera::gattWrite` is
  `characteristic->writeValue(data, length, response)` with no timeout
  (`lib/furble/Camera.cpp:1052`). A peer that stops answering holds the task
  until the ATT transaction fails or the link drops at the supervision timeout,
  which is 7 s in the idle profile (`lib/furble/Camera.h:740`,
  `m_IdleTimeout = 700` in 10 ms units).
- In those seconds the producers keep posting. At the fastest intervalometer
  settings one command leaves per 100 ms tick, so 8 slots is 800 ms. A 1 Hz GPS
  fan-out adds one per second (`src/FurbleControl.cpp:275-277`), and a
  multi-connect session shares one radio across up to three queues, which
  multiplies each command's latency by the number of cameras.

Sequences that lose a release rather than only a press:

1. **The last intervalometer frame.** Press at `STATE_SHUTTER_OPEN`
   (`src/FurbleUI.cpp:7769`), release on the next tick at `STATE_DELAY`
   (`7777`), then `STATE_FINISHED`. A release refused there ends the run with
   the camera still pressed, and no later release exists to repair it.
2. **Bulb or the touch shutter lock.** `UI::shutterLock` presses once
   (`src/FurbleUI.cpp:2157`) and `UI::shutterUnlock` releases on a later touch
   (`2175`), seconds or minutes later. The hold is exactly the window in which a
   stalled write plus the GPS fan-out fill the queue.
3. **A tap behind a burst.** A press lands while one slot is free, the burst
   fills the rest, and the release from that same tap is refused.

In all three the immediate victim is a release, and the only repair is a later
release that gets through. That makes case 1 the damaging one, and it is why the
intervalometer and the shutter lock matter even though they are not the busiest
producers.

`allConnectedLocked` (`src/FurbleControl.cpp:806-836`) is not part of this. It
decides whether the machine stays `STATE_ACTIVE`; it does not gate the send and
does not drop anything. The other refusal in `sendCameraCommand` is a target
whose link is down, which is intended: the press went with the connection.

## Fix

Chosen: a release takes the oldest waiting command's slot. The release path
becomes unbounded, every request path stays bounded by the same 8 slots, and
neither side grows the queue.

**Narrowed after review ([546-review-feedback.md](546-review-feedback.md), finding
1).** "The oldest waiting command" can be a release, and the review built the
case: a delivered press, a shutter release behind it at the head of the queue,
seven presses behind that, then a focus release. The reviewed head evicted the
shutter release and left the shutter held on Lumix, Sony, Canon EOS Smart, Nikon
Remote, Ricoh and DJI, which is the failure this plan exists to remove. The rule
now displaces the oldest waiting command only when it is a request, puts a queued
release back at the front with `xQueueSendToFront`, and refuses its own release
instead. The code quoted below is the reviewed head, not the current file.

Rejected alternatives:

- **Reserve the last slot for releases** by refusing requests at 7 waiting. It
  cannot deadlock either, but it does not close the hole. The queue can still be
  full when a release arrives, because the item holding the reserved slot may be
  an older release, and the newest press behind that older release then has no
  release after it.
- **Collapse a queued press that a release supersedes.** A click needs both
  frames delivered, so collapsing is only safe for a press that a release is
  paired with, and the queue does not record that pairing.
- **A separate one-slot release latch drained by the task.** It adds state to
  `Target`, and `idle()` and `waitForCameraCommands()` both have to learn about
  it, or the deep-sleep release wait (`src/FurbleUI.cpp:7802`) returns early.
  It also delivers the release ahead of queued presses unless the drain is
  ordered by hand. Too much surface for this failure.

Implementation, `Control::Target::sendCommand`, with the release predicate in the
file's anonymous namespace:

```
    79	/**
    80	 * True for the two commands that end a press the camera is already acting on.
    81	 *
    82	 * These are the camera commands a full target queue must not discard: the
    83	 * press before them has already been delivered, and the camera holds whatever
    84	 * that press asserted until the matching release arrives. No vendor driver
    85	 * times the hold out (plan 374), so a dropped release is the shutter held.
    86	 */
    87	bool isReleaseCommand(Control::cmd_t cmd) {
    88	  return (cmd == Control::CMD_SHUTTER_RELEASE) || (cmd == Control::CMD_FOCUS_RELEASE);
    89	}
```

```
   144	  if (m_DisconnectPending.load(std::memory_order_acquire)) {
   145	    // The teardown is already queued and the target task exits on it, so this
   146	    // command would never run. Refuse it before the reservation instead of
   147	    // queueing work that only the destructor would ever remove.
   148	    ESP_LOGD(LOG_TAG, "Dropped command %d: target is disconnecting.", static_cast<int>(cmd));
   149	    return pdFALSE;
   150	  }
   151
   152	  // Reserve the count before the command can be received. Incrementing after
   153	  // xQueueSend lets a fast target finish and decrement first, which can make
   154	  // the counter zero while the command is still executing.
   155	  m_InFlight.fetch_add(1, std::memory_order_acq_rel);
   156	  BaseType_t ret = xQueueSend(m_Queue, &cmd, 0);
   157	  if ((ret != pdTRUE) && isReleaseCommand(cmd)) {
   158	    // A release must not be the command a full queue discards. The press it
   159	    // ends has already been delivered, the camera is holding it, and nothing in
   160	    // the firmware or in a vendor driver lets go on its own (plan 374), so the
   161	    // dropped release leaves the shutter held until the user presses and
   162	    // releases again. Take the oldest waiting command's slot instead. Every
   163	    // command still in the queue is worth less than the release: a request that
   164	    // loses its slot is a lost press, which the release behind it answers with
   165	    // an up the camera ignores.
   166	    //
   167	    // Both calls are non-blocking and the receive frees exactly the slot the
   168	    // send then takes, so this cannot grow the queue or wait on the target
   169	    // task draining it. Two attempts cover another producer taking the freed
   170	    // slot in between: that producer is another release, which releases the
   171	    // camera just as well, or one more request to evict.
   172	    for (uint8_t attempt = 0; (ret != pdTRUE) && (attempt < 2); attempt++) {
   173	      cmd_t evicted = CMD_ERROR;
   174	      if (xQueueReceive(m_Queue, &evicted, 0) != pdTRUE) {
   175	        break;
   176	      }
   177	      // The receive proves this side removed the item, so exactly one
   178	      // reservation retires here, the same rule the CMD_DISCONNECT drain uses.
   179	      m_InFlight.fetch_sub(1, std::memory_order_acq_rel);
   180	      ret = xQueueSend(m_Queue, &cmd, 0);
   181	    }
   182	  }
   183	  if (ret != pdTRUE) {
   184	    m_InFlight.fetch_sub(1, std::memory_order_acq_rel);
   185	    ESP_LOGE(LOG_TAG, "Failed to send command to target.");
   186	  }
   187	  return ret;
```

Why it is safe:

- Both calls are non-blocking, `xQueueReceive(..., 0)` and `xQueueSend(..., 0)`,
  so no path waits on the target task draining and none can deadlock.
- The receive runs before the send, so the queue never holds more than
  `m_QueueLength` items. The send that fits in the freed slot is the only one
  added, and nothing else is.
- FIFO order survives for every command that remains: the evicted item is the
  oldest and the release is appended at the tail, so a press that stays queued
  is still followed by the release. An evicted press never reaches the camera,
  and the release behind it is an up on a camera that was never pressed, which
  every driver above ignores.
- `m_InFlight` stays exact. One reservation is taken at entry, one retires for
  the evicted item (the same rule the `CMD_DISCONNECT` drain uses at
  `src/FurbleControl.cpp:132-135`), and one retires only if the release still
  fails. `Target::idle()` (`971-974`) and `waitForCameraCommands()` (`976`)
  therefore keep their meaning, which the deep-sleep release wait depends on.
- Two attempts, because another producer can take the freed slot between the
  receive and the send. That producer is either another release, which releases
  the camera just as well, or one more request to evict.
- The eviction cannot take `CMD_DISCONNECT`. Taking the oldest waiting command
  is only safe while that item is disposable, and the teardown is not: the
  target task exits on it, and losing it hangs `disconnect()` until its cap.
  Posting a teardown therefore sets `Target::m_DisconnectPending` before the
  drain, and every other command is refused from then on
  (`src/FurbleControl.cpp:144-150`). The drain then leaves exactly one item, so
  no release can ever find a disconnect at the front to evict, and no producer
  can race work past the flag into the window between the drain and the
  front insert. That also makes the existing "Reset guarantees space" claim
  above the drain true rather than probable.

## Test

`tests/host/control_release_durability_test.cpp`, registered as
`control-release-durability` in `tests/host/CMakeLists.txt`. It runs the
production `Control` and the real target task against `FujifilmVirtualCamera`,
and uses the existing `target_command_complete` sync point
(`lib/furble/FurbleTestSync.h`) to park the target task after a delivered press.
That reproduces a stalled write deterministically instead of waiting for one.

Steps and what each one pins:

1. connect to the virtual peer and reach `STATE_ACTIVE`;
2. park the task after one press and assert the peer holds
   `kShutterFullPress`, so the press is delivered and the queue cannot drain;
3. fill with 8 more presses, all accepted, then assert a 9th is refused, which
   pins that the request path is still bounded;
4. assert `CMD_SHUTTER_RELEASE` is accepted by the full queue (the defect);
5. assert `CMD_FOCUS_RELEASE` is accepted in the same full state, because the
   release set has two members;
6. unpark, drain with `waitForCameraCommands()`, and assert the peer's
   `lastShutterValue()` is `kShutterRelease`, which is the camera's final button
   state;
7. a press and release pair on an empty queue still arrives in order, so the
   ordinary path is unchanged;
8. park once more, queue a teardown from a worker thread, and assert that a
   press and then a release are both refused. The press refusal alone would
   also come from a full queue, so the release is the check that separates the
   two: without the teardown rule it would be accepted by evicting the
   disconnect. Then unpark and assert that `disconnect()` returned true.

Two mutations were run on this branch, each one disabled in isolation.

Restoring the base `sendCommand`, so a full queue drops the release again, fails
exactly the three checks that describe the defect:

```
  FAIL: shutter release is accepted by a full queue
  FAIL: focus release is accepted by a full queue
  FAIL: the camera ends with the shutter released
control-release-durability: FAIL (3 checks)
```

Disabling only the teardown rule (`m_DisconnectPending`) while keeping the
eviction fails the step 8 check and then the outcome check, because the release
takes the disconnect's slot and the target task never exits:

```
  FAIL: a release cannot evict a queued teardown
```

With both in place the test passes. The step 3 check keeps passing in the first
mutation, which is what shows the request path is unchanged rather than the
whole test failing.

## Deviations

- Plan 371 (fork network sweep) said `Control::sendCommand` drops the command.
  It is
  `Control::Target::sendCommand`, one level down, as quoted above. The 32-slot
  control queue still refuses a GPS update or a no-op wake when full, which is
  harmless, intended and unchanged.
- The fix lives in the queue instead of in each caller's retry, because five
  producers send releases and only two retry. The MQTT and WebUI retries stay:
  they are harmless once the queue accepts the release, and they still cover a
  target whose link drops between the press and the release.
- No new setting and no new command. The behaviour is a fix, not an option, so
  it is not configurable and defaults keep every existing gesture identical.

## Citation maintenance

The inserted helper and release branch move every line below them in
`src/FurbleControl.cpp`, and the new Doxygen comment above `Target::sendCommand`
moves the lines below it in `include/FurbleControl.h`.
`python3 tools/check_doc_citations.py --root .` failed on 11 citations after the
edit and passes again once they were updated: `docs/power/ble.md`,
`docs/power/soc.md` twice, `plans/33-wifi-hub.md` twice,
`plans/34-ota-partitions.md`, `plans/50-companion-app-design.md`,
`plans/97-followups.md`, `plans/implemented/09-reconnect-backoff.md`,
`plans/implemented/60-issue-257.md` and
`plans/implemented/76-reconnect-stuck.md`. The inline citations in
`include/FurbleControl.h` and `plans/327-control-gps-race.md` were updated too,
although the gate does not attribute either of them.

## Gates

Host, run on this branch in a clean worktree based on `18bbcb5a`:

- `control-release-durability` passes, ten consecutive runs, 0.4 to 0.6 s each,
  and fails under both mutations quoted above.
- The control neighbours were rebuilt against the changed `Control` and pass:
  `control-interleave`, `control-abort-republish`, `control-disconnect`,
  `control-zombie-cancel`, `control-teardown-wedge`, `control-e2e`,
  `control-conn-saver`, `control-secure-stall`, `control-reclaim-uaf`, plus
  `console-commands` and `companion-gatt`, which drive the same command path.
- `python3 tools/pre_review.py --base origin/master` reports `PASS: 13 gates`,
  exit 0. The one first-run failure was trailing whitespace in this document's
  own quoted code blocks, fixed before the recorded run.
- The wider `control-`, `console-commands`, `companion-gatt`, `mqtt-owner` and
  `ricoh-control-flap` ctest selection ran 47 tests: 36 passed, 10 were not run
  because their binaries are not built in this checkout (`connect-mid-drop`, the
  seven `control-fuzz` seeds, `boundary-companion-gatt`), and
  `control-tsan-wrapper-contract` hit its 10 s CTest timeout. That last one is
  host slowness, not a regression: the contract script passes, but takes 52 s
  here because each freshly written fixture script is executed for the first
  time, and the same script on the unmodified base tree takes 1 min 37 s. It
  does not touch this change, and its CMake timeout is unchanged.

`python3 tools/check_doc_citations.py --root .` reports 0 errors over 3179
citations, which is the check that the inserted lines did not rotate anything.

No hardware gate is satisfied or claimed. Which model stays stuck, and for how
long, after a real dropped release is untested, and no vendor's camera was
attached to this branch. The fix does not depend on the answer: the command is
no longer lost for any of the nine vendor paths in the table above.
