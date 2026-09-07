#!/usr/bin/env python3
"""Guard the IMU light-sleep wake pin against interrupt-type reconfiguration.

gpio_set_intr_type() and gpio_wakeup_enable() write the same hardware field,
hw->pin[n].int_type, and the IDF refuses an edge type on a wakeup pin
("GPIO wakeup only supports level mode", esp_driver_gpio/src/gpio.c). A GPIO
interrupt installed on the wake pin therefore cancels the level wake it was
meant to observe, and it does so silently: arming still reports success and the
device simply never wakes on motion.

That is exactly what an edge counter added to armMotionWake() did once. It is
invisible to every existing gate, because src/FurblePlatform.cpp is compiled by
no host or simulator target, so this guard reads the source instead.

A full host build of the platform GPIO and PMIC logic behind shims would be
stronger and is recorded as owed in plans/20-imu-hw-motion.md. This catches the
specific regression that already happened once.
"""

import pathlib
import re
import sys

FORBIDDEN = ("gpio_set_intr_type", "gpio_isr_handler_add", "gpio_install_isr_service")


def motion_wake_bodies(text):
    """Yield (name, body) for each motion wake function in the file."""
    for name in ("armMotionWake", "disarmMotionWake", "motionWakeSample"):
        match = re.search(r"\bPlatform::" + name + r"\s*\([^)]*\)[^{]*\{", text)
        if match is None:
            continue
        depth = 0
        start = match.end() - 1
        for index in range(start, len(text)):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    yield name, text[start : index + 1]
                    break


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    source = root / "src" / "FurblePlatform.cpp"
    text = source.read_text(encoding="utf-8")

    failures = []
    for name, body in motion_wake_bodies(text):
        # Comments are allowed to name the call; code is not.
        code = re.sub(r"//[^\n]*", "", body)
        code = re.sub(r"/\*.*?\*/", "", code, flags=re.S)
        for call in FORBIDDEN:
            if call + "(" in code:
                failures.append(
                    "%s calls %s(). That writes the wake pin's interrupt type "
                    "and cancels the level wake source." % (name, call)
                )

    if failures:
        for failure in failures:
            print("%s: %s" % (source.relative_to(root), failure), file=sys.stderr)
        return 1

    print("IMU wake pin interrupt configuration is clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
