# UI layout audit

## Motivation

On-screen text already collides with other UI elements on some screens. The
firmware needs a repeatable way to expose those collisions while text sizes and
board geometries change.

LVGL 9.4 has no built-in static layout analyzer. A source-only checker cannot
evaluate flex and grid layout, scrolling, hidden objects, or the final font
metrics. The realistic long-term path is a runtime audit in the SDL simulator
described by `plans/28-emulator.md`.

## Chosen approach

The simulator will run the audit after each screen loads. The walker starts at
the active screen and follows the LVGL object tree. It visits every visible
object and every visible label. For each label it calls `lv_obj_get_coords` on
the label and on each visible sibling widget. It reports bounding-box
intersections. It also measures the label text with the label's resolved font,
letter spacing, and line spacing. It reports labels whose unwrapped rendered
text width is greater than the label object's width.

The first automatable step is shared firmware code in
`src/FurbleUIAudit.cpp`. It is compiled only for `FURBLE_SIM` or
`FURBLE_CONSOLE`. The simulator can call `Furble::UIAudit::dump` directly.
The developer console reaches the same code through `ui audit`. The console
request is queued to the UI task because LVGL is not thread safe. The command
therefore works on real hardware over the existing USB console without any
simulator dependency.

The walker uses stable object paths instead of pointers. `root` is the active
screen. A child path appends its zero-based LVGL child index, such as
`root/0/2`. Rectangle coordinates are inclusive LVGL coordinates in the order
`x1,y1,x2,y2`.

## Output format

The command prints JSON Lines. Every line is one JSON object. The first line is
a `begin` record. A `clipped` record identifies text wider than its label. An
`overlap` record identifies a label and a visible sibling whose rectangles
intersect. The final line is an `end` record with counts. Text is JSON escaped.
An empty screen report has only `begin` and `end` records. A missing root emits
an `error` record.

The schema is `furble-ui-audit/v1`. A report with both issue types looks like
this:

```text
{"type":"begin","schema":"furble-ui-audit/v1","screen":"active"}
{"type":"overlap","label_path":"root/0/1","sibling_path":"root/0/2","label_rect":[0,34,134,51],"sibling_rect":[0,46,134,63],"text":"Brightness"}
{"type":"clipped","label_path":"root/1/0","label_rect":[0,72,79,87],"label_width":80,"text_width":94,"text":"Inactivity timeout"}
{"type":"end","visible_objects":17,"labels":6,"issues":2,"overlaps":1,"clipped":1}
```

The report is a diagnostic snapshot of the current screen. It does not decide
whether an overlap is intentional. The future simulator job will compare
reports against a reviewed baseline.

## CI follow-up

There is no CI enforcement in this change. A full audit-all-screens job depends
on the SDL simulator in `plans/28-emulator.md` landing first.

After that dependency lands, the intended job will run the simulator headless,
navigate every screen through scripted UI input, and collect one report for
each supported board resolution. It will fail the build when a new overlap or
clipped label appears compared with a checked-in baseline report. The baseline
will be reviewed with the same care as a screenshot change.

## Implementation state

Implemented on `feat/ui-text-scaling` together with the `TEXT_SIZE` setting.

Rebase notes:

- `TEXT_SIZE` is assigned wire_id 40, continuing after `SD_GPX` (39) from
  PR 41. `src/FurbleCompanion.cpp` settingType and settingValue cover it as
  SETTING_U8.
- The Display settings page keeps master's Screen off roller from PR 26; the
  Text size page is added below it.
- The three Montserrat font sizes (10, 14, 28) are enabled consistently in
  all five release sdkconfig files.
- This branch has no numbered plans document; this section serves as its
  implementation state record.
