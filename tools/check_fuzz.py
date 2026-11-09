#!/usr/bin/env python3
"""Run the seeded fuzz gate before a hardware image is built.

The gate has four layers:

1. UI input fuzzing on every modelled panel, in both the touch and
   FURBLE_SIM_NO_TOUCH=1 layouts.
2. Host API fuzzing for the WebUI, companion protocol and MQTT owner.
3. Random memory-pressure boots and the fixed memory-pressure scenarios.
4. Source and runtime checks for the two known hardware crashes: the MQTT
   owner stack overflow at 4/6 KiB and the Home Assistant discovery OOM at
   about 1.7 KiB free.

A finding prints the exact seed and reproduction command. The gate exits
non-zero until the finding is fixed, so a hardware build cannot silently ship
with a known crash.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SEEDS_FILE = ROOT / "tests/fuzz/seeds.json"


def load_seeds(path: Path = SEEDS_FILE) -> dict:
    with path.open(encoding="utf-8") as handle:
        document = json.load(handle)
    for area in ("ui", "webui", "companion", "mqtt", "memory"):
        if area not in document:
            raise SystemExit(f"{path} is missing the {area} seed set")
    return document


def resolve_mqtt_owner_stack(root: Path) -> tuple[int | None, str]:
    """Resolve the owner stack expression, or return None and the expression."""
    source = root / "src/FurbleMQTT.cpp"
    try:
        text = source.read_text(encoding="utf-8")
    except OSError as error:
        return None, f"cannot read {source}: {error}"
    match = re.search(r"xTaskCreate\(\s*taskEntry\s*,\s*\"mqtt\"\s*,\s*([^,]+),", text)
    if not match:
        return None, "cannot locate the MQTT owner xTaskCreate call"
    expression = match.group(1).strip()
    if expression.isdigit():
        return int(expression), expression
    header = root / "include/FurbleMQTTMemory.h"
    if header.is_file():
        header_text = header.read_text(encoding="utf-8")
        constant = re.search(r"OWNER_STACK_BYTES\s*=\s*(\d+)", header_text)
        if constant:
            return int(constant.group(1)), expression
    return None, f"cannot resolve the MQTT owner stack expression '{expression}'"

DEFAULT_UI_BINARIES = {
    "m5stick-s3": ROOT / "sim/build/furble-sim",
    "m5stick-c": ROOT / "sim/build-stickc/furble-sim",
    "m5stack-core": ROOT / "sim/build-core/furble-sim",
}

CRASH_MARKERS = (
    "SIM CRASH",
    "Guru Meditation",
    "panic",
    "stack overflow",
    "stack canary",
    "Segmentation fault",
    "AddressSanitizer",
    "runtime error:",
)



def binary_sanitized(binary: Path) -> bool:
    """True when the fuzz binary itself links the ASan and UBSan runtimes.

    The binary is checked, not the CMake cache: a host build configured
    before every fuzz target got the flags, or with
    FURBLE_FUZZ_ALLOW_UNSANITIZED=ON, can still hold unsanitized binaries.
    Both runtimes leave their entry symbols in the image on every supported
    toolchain, as a definition or as a dynamic import.
    """
    try:
        image = binary.read_bytes()
    except OSError:
        return False
    return b"__asan_init" in image and b"__ubsan_handle_" in image


class Gate:
    def __init__(self, seeds: dict) -> None:
        self.findings: list[str] = []
        self.checks = 0
        self.skips: list[str] = []
        self.known = {entry["id"]: entry for entry in seeds.get("known_findings", [])}

    def finding(self, label: str, detail: str) -> None:
        self.findings.append(f"[{label}] {detail}")

    def run(self, label: str, command: list[str], cwd: Path | None = None,
            env: dict[str, str] | None = None, timeout: int = 900) -> subprocess.CompletedProcess[str]:
        self.checks += 1
        merged = os.environ.copy()
        merged.update(env or {})
        try:
            result = subprocess.run(
                command,
                cwd=str(cwd or ROOT),
                env=merged,
                capture_output=True,
                text=True,
                timeout=timeout,
            )
        except subprocess.TimeoutExpired as error:
            def as_text(value):
                if isinstance(value, bytes):
                    return value.decode("utf-8", "replace")
                return value or ""

            output = as_text(error.stdout) + as_text(error.stderr)
            self.finding(label, f"timed out after {timeout}s\n{output[-2000:]}")
            return subprocess.CompletedProcess(command, 124, output, "")
        if result.returncode != 0:
            output = result.stdout + result.stderr
            finding_lines = [
                line for line in output.splitlines()
                if "FINDING" in line or "FAIL" in line or "SUMMARY" in line or "ERROR" in line
            ]
            self.finding(
                label,
                "exit "
                + str(result.returncode)
                + "\n"
                + "\n".join(finding_lines[-20:] or output.splitlines()[-20:]),
            )
        return result

    def check_source_stack(self) -> None:
        self.checks += 1
        value, detail = resolve_mqtt_owner_stack(ROOT)
        if value is None:
            self.finding("mqtt-owner-stack", detail)
            return
        if value < 10240:
            owner = self.known.get("mqtt-owner-stack", {}).get("owner", "unknown owner")
            self.finding(
                "mqtt-owner-stack",
                f"owner stack is {value} bytes; hardware overflowed at 4096 and 6144. "
                f"Use 10240 (owned by {owner}).",
            )

    def fuzz_ready(self, label: str, binary: Path) -> bool:
        """A fuzz binary runs only if it exists and links ASan and UBSan."""
        if not binary.is_file():
            self.finding(label, f"fuzz binary missing: {binary}")
            return False
        if not binary_sanitized(binary):
            self.finding(label, f"{binary} has no ASan/UBSan runtime; rebuild the host "
                                "tests with a compiler that has them")
            return False
        return True

    def run_host_fuzz(self, host_build: Path, seeds: dict, smoke: bool) -> None:
        binaries = {
            "webui": host_build / "webui_fuzz",
            "companion": host_build / "companion_fuzz",
            "mqtt": host_build / "mqtt_fuzz",
        }
        key = "smoke" if smoke else "full"
        for name, binary in binaries.items():
            if not self.fuzz_ready(f"host-{name}", binary):
                continue
            area = seeds[name]
            for seed in area[key]:
                iterations = area[f"{key}_iterations"]
                command = [str(binary), "--seed", str(seed), "--iterations", str(iterations)]
                # Keep sanitizer errors fatal. MQTT allocation leaks are
                # checked explicitly by the discovery OOM phase, and the
                # sanitized host CI job runs LeakSanitizer where the platform
                # supports it.
                env = {"ASAN_OPTIONS": "halt_on_error=1",
                       "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
                result = self.run(f"host-{name}-seed-{seed}", command, env=env, timeout=300)
                if result.returncode != 0:
                    self._persist(name, seed, result.stdout + result.stderr)
        ota = host_build / "ota_mqtt_protocol_test"
        if ota.is_file():
            self.run("host-ota", [str(ota)], timeout=300)
        else:
            self.finding("host-ota", f"OTA fuzz target missing: {ota}")
        for label, target in (
            ("host-settings-migration", "settings_migration_fuzz"),
            ("host-wifi-policy", "wifi_policy_fuzz"),
            ("host-psram-present", "bt_debug_journal_s3_test"),
            ("host-psram-absent", "bt_debug_journal_s3_fallback_test"),
            ("host-nvs-failures", "settings_nvs_roundtrip_test"),
        ):
            binary = host_build / target
            if target.endswith("_fuzz"):
                if self.fuzz_ready(label, binary):
                    self.run(label, [str(binary)], timeout=300)
            elif binary.is_file():
                self.run(label, [str(binary)], timeout=300)
            else:
                self.finding(label, f"memory-failure target missing: {binary}")

    def _persist(self, name: str, seed: int, output: str) -> None:
        directory = ROOT / "tests/fuzz/failures"
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / f"{name}-seed-{seed}.log"
        path.write_text(output, encoding="utf-8")

    def run_discovery_oom(self, host_build: Path) -> None:
        binary = host_build / "mqtt_fuzz"
        if not self.fuzz_ready("ha-discovery-oom", binary):
            return
        command = [str(binary), "--seed", "1", "--iterations", "1", "--discovery-oom"]
        result = self.run(
            "ha-discovery-oom",
            command,
            env={"ASAN_OPTIONS": "halt_on_error=1"},
            timeout=300,
        )
        if result.returncode != 0:
            self._persist("ha-discovery-oom", 1, result.stdout + result.stderr)
            owner = self.known.get("ha-discovery-oom", {}).get("owner", "unknown owner")
            self.findings[-1] += (
                "\n  reproduce: "
                + " ".join(command)
                + "\n  the cJSON failure path leaks tree nodes under heap pressure; "
                + f"owned by {owner}."
            )

    def run_ui(self, binaries: dict[str, Path], seeds: dict, smoke: bool) -> None:
        area = seeds["ui"]
        key = "smoke" if smoke else "full"
        steps = area[f"{key}_steps"]
        for board, binary in binaries.items():
            if not binary.is_file():
                self.finding(f"ui-{board}", f"simulator binary missing: {binary}")
                continue
            for layout, extra_env in (
                ("touch", {}),
                ("buttons", {"FURBLE_SIM_NO_TOUCH": "1"}),
            ):
                env = {
                    "SDL_VIDEODRIVER": "dummy",
                    "SDL_AUDIODRIVER": "dummy",
                    "FURBLE_SIM_BIN": str(binary),
                    "FURBLE_FUZZ_SEEDS": " ".join(str(seed) for seed in area[key]),
                    "FURBLE_FUZZ_STEPS": str(steps),
                    "FURBLE_FUZZ_REPEAT_SEED": "",
                }
                env.update(extra_env)
                result = self.run(
                    f"ui-{board}-{layout}",
                    ["sh", "sim/scripts/run-fuzz.sh"],
                    env=env,
                    timeout=1800,
                )
                if result.returncode != 0:
                    self._persist(f"ui-{board}-{layout}", 0, result.stdout + result.stderr)

    def run_memory(self, default_binary: Path, mqtt_binary: Path | None, smoke: bool) -> None:
        driver = ROOT / "tests/fuzz/memory_fuzz.py"
        seeds = SEEDS["memory"]["smoke" if smoke else "full"]
        if default_binary.is_file():
            self.run(
                "memory-webui",
                [sys.executable, str(driver), "--sim", str(default_binary),
                 "--seeds", *[str(seed) for seed in seeds]],
                env={"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"},
                timeout=900,
            )
        else:
            self.finding("memory-webui", f"simulator binary missing: {default_binary}")

        if mqtt_binary is not None and mqtt_binary.is_file():
            self.run(
                "memory-mqtt-fixed",
                ["sh", "sim/scripts/run-memory.sh"],
                env={
                    "SDL_VIDEODRIVER": "dummy",
                    "SDL_AUDIODRIVER": "dummy",
                    "FURBLE_SIM_BIN": str(mqtt_binary),
                },
                timeout=1800,
            )
            self.run(
                "memory-mqtt-random",
                [sys.executable, str(driver), "--sim", str(mqtt_binary), "--mqtt",
                 "--seeds", *[str(seed) for seed in seeds]],
                env={"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"},
                timeout=1800,
            )
        elif mqtt_binary is not None:
            self.finding("memory-mqtt", f"MQTT simulator binary missing: {mqtt_binary}")
        else:
            self.skips.append("memory-mqtt: no MQTT simulator binary supplied")

    def report(self) -> int:
        if self.findings:
            print("FUZZ GATE FAIL")
            for finding in self.findings:
                print(f"  {finding}")
            print(f"FUZZ GATE checks={self.checks} skips={len(self.skips)} findings={len(self.findings)}")
            return 1
        for skip in self.skips:
            print(f"SKIP {skip}")
        print(f"FUZZ GATE PASS checks={self.checks} skips={len(self.skips)}")
        return 0


def parse_ui_binaries(values: list[str]) -> dict[str, Path]:
    binaries = dict(DEFAULT_UI_BINARIES)
    for value in values:
        if "=" not in value:
            raise SystemExit(f"--ui-binary expects board=path, got {value}")
        board, path = value.split("=", 1)
        if board not in binaries:
            raise SystemExit(f"unknown board {board}; expected one of {sorted(binaries)}")
        binaries[board] = Path(path)
    return binaries


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--smoke", action="store_true",
                        help="small fixed seed budget for pull requests")
    parser.add_argument("--full", action="store_true",
                        help="larger seed budget before a hardware build")
    parser.add_argument("--host-build", type=Path, default=ROOT / "host-build")
    parser.add_argument("--mqtt-binary", type=Path, default=ROOT / "sim/build-mqtt/furble-sim")
    parser.add_argument("--ui-binary", action="append", default=[],
                        metavar="BOARD=PATH")
    parser.add_argument("--skip-ui", action="store_true")
    parser.add_argument("--skip-host", action="store_true")
    parser.add_argument("--skip-memory", action="store_true")
    args = parser.parse_args()

    if args.smoke == args.full:
        raise SystemExit("choose exactly one of --smoke or --full")

    global SEEDS
    SEEDS = load_seeds()

    gate = Gate(SEEDS)
    gate.check_source_stack()
    if not args.skip_host:
        gate.run_host_fuzz(args.host_build, SEEDS, args.smoke)
        gate.run_discovery_oom(args.host_build)
    if not args.skip_ui:
        gate.run_ui(parse_ui_binaries(args.ui_binary), SEEDS, args.smoke)
    if not args.skip_memory:
        gate.run_memory(DEFAULT_UI_BINARIES["m5stick-s3"], args.mqtt_binary, args.smoke)

    return gate.report()


if __name__ == "__main__":
    raise SystemExit(main())
