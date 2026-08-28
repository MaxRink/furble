# Controls

This page covers the physical buttons per board and both input modes. It
mirrors Part 2 of the [Settings Reference](Settings-Reference).


### Physical inputs per board

furble drives the interface with three logical inputs: previous, select, and
next. They map onto real buttons differently per board.

- **M5StickC, M5StickC Plus.** Front M5 button (BtnA), a small top button
  (BtnB), and the power button. The power button is the PMIC PEK button, read as
  a debounced click.
- **M5StickC Plus2, M5StickS3.** Front M5 button (BtnA), top button (BtnB), and a
  dedicated side power button. On the StickS3 the side button is a plain input
  owned by furble. Its hardware reset and power-off gestures are disabled at
  boot, so it never resets or powers off the device on its own.
- **M5Stack Core Basic.** Three buttons under the screen: BtnA, BtnB, BtnC. No
  touch.
- **M5Core2, M5Tough.** Three touch zones act as BtnA, BtnB, BtnC, plus a touch
  screen. On the shutter page these boards show on-screen Shutter, Focus, and
  Shutter Lock buttons. A double-click of the power button toggles a screen
  lock.

### Navigation model (two-button)

This is the default and the only navigation model. Three inputs drive a standard
scrolling menu:

- **Previous (left).** Moves focus to the previous item.
- **Select (middle).** Activates the focused item. On the shutter page it is the
  shutter.
- **Next (right).** Moves focus to the next item. On the shutter page it holds
  focus.

Per board, previous, select, next are:

- Sticks: power button, BtnA (front), BtnB (top).
- Cores: BtnA (left), BtnB (middle), BtnC (right).

**Back.** A long press of the left button, about 0.8 seconds, is Back from
anywhere. It is deliberately stronger than the on-screen back arrow, so it works
even where that arrow is hidden. On the shutter page a short click of the left
button is also Back. Menus also carry a focusable back arrow in the header that
you can select the normal way.

### Shutter, focus, and shutter lock (two-button)

Open the connected `Remote` page to reach the shutter control.

- **Select (middle) is the shutter.** Press to fire, release to end.
- **Next (right) is focus.** Press to half-press focus, release to end.
- Ricoh cameras do not support the focus action over this BLE path. Their
  focus control is a no-op; use the camera body to configure autofocus. Ricoh's
  supported shutter operation sends `OperationRequest {0x01, 0x01}`, which means
  capture with autofocus, not a separate focus command.
- **Shutter lock.** Hold focus (right), then press the shutter (select). The
  shutter locks open and stays open until you press a button. This is handy for
  a long exposure without holding a button down. You can also long-press the
  on-screen lock icon to toggle the lock. On touch boards the lock icon is
  always on screen.

To end a locked exposure, press any button. Leaving the page also releases the
shutter, so an exposure never keeps running out of sight.

### One-button mode

One-button mode is a shipped, opt-in alternative. Set it under `Settings` >
`Features` > `Button Mode` > `One-button`. It changes only the shutter page. All
menu navigation stays the same. The single select button (BtnA on sticks) then
does everything on the shutter page:

- **Hold to focus.** Press and hold the button. Focus engages while held and
  releases when you let go.
- **Double-click to shoot.** A second press that lands within about 0.4 seconds
  of a short click fires the shutter with no leading focus.
- **Click then hold to hold the shutter.** Click once, then press and hold. The
  shutter fires and stays down until you release.
- A single lone click does nothing on its own.

The gesture hint is printed on the Button Mode page: "One-button: hold=focus,
double-click=shoot, click+hold=hold shutter". The default is two-button.

### Other remote page controls

The connected `Remote` group also offers:

- `IR`: fire the camera over IR, shown only when Infrared is on.
- `Bulb`: timed long exposure. Set `Duration`, press Start, and the shutter
  releases at zero.
- `Interval`: the intervalometer, sharing the Timer configuration.
- `GPS Data`: the live GPS page.
- `Disconnect`: drop the camera and return.

### Automation surface

Debug builds expose a USB serial console for host scripts. Two facts about how it
drives input:

- The simulator button injector reproduces the real button path, including Back
  on a left-button hold, and it respects one-button versus two-button mode.
- The console `shutter` and `focus` commands are lower level. They enqueue camera
  commands directly and bypass the button-mode dispatch and the shutter-lock
  state. Use them to trigger the camera, not to test the button gestures.

See the Serial console section of the [README](Getting-Started) for the full
command list.
