#!/usr/bin/env python3
"""Run the fuzz targets named by sim/coverage/fuzz_targets.json.

The flash-readiness gate consumes sim/coverage/fuzz_results.json. Every target
record names its seed count, the iterations it ran, its failures and any known
failing seed. A target is passing only when every command and scenario exits
zero. Use --all to regenerate the complete result file.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SEEDS = json.loads((ROOT / "tests/fuzz/seeds.json").read_text(encoding="utf-8"))
MANIFEST = json.loads(
    (ROOT / "sim/coverage/fuzz_targets.json").read_text(encoding="utf-8")
)

DEFAULT_SIM = ROOT / "sim/build/furble-sim"
MQTT_SIM = ROOT / "sim/build-mqtt/furble-sim"
HOST_BUILD = Path(os.environ.get("FURBLE_HOST_BUILD", ROOT / "host-build"))

TARGET_IDS = [entry["id"] for entry in MANIFEST["targets"]]

# Production failures found while bringing the combined tree up. They are not
# open fuzz seeds: each one carries the file and the regression that now guards
# it, so a result file never hides a fixed crash.
FIXED_REGRESSIONS = [
    {
        "id": "sim-preferences-deferred-commit",
        "source": "sim/PreferencesSim.cpp",
        "detail": "#397 added Preferences::setDeferredCommit()/commit() to the shared settings seeder; the checked simulator adapter did not implement them, so the simulator failed to link.",
        "regression": "sim/scenarios/e2e/hw-boot-settings-migration.txt",
        "fixed": True,
    },
    {
        "id": "mqtt-owner-stack-6k",
        "source": "src/FurbleMQTT.cpp",
        "detail": "Hardware overflowed the MQTT owner task at 4 KiB and 6 KiB. #398/#393 keep the owner at 10 KiB and the plain worker at 3 KiB.",
        "regression": "sim/scenarios/e2e/hw-memory-mqtt-10k-success.txt",
        "fixed": True,
    },
    {
        "id": "ha-discovery-oom-1k7",
        "source": "src/FurbleMQTT.cpp",
        "detail": "HA discovery built a cJSON tree with about 1.7 KiB free. #398 adds the 10,240/4,096 discovery gate and null-safe builders.",
        "regression": "mqtt_fuzz --seed 1 --iterations 1 --discovery-oom",
        "fixed": True,
    },
]


class Result:
    def __init__(self, target_id: str) -> None:
        self.target_id = target_id
        self.commands: list[str] = []
        self.seed_runs = 0
        self.iterations = 0
        self.failures = 0
        self.known_failing_seeds: list[dict] = []
        self.output: list[str] = []

    def record(self, command: list[str], seeds: int, iterations: int) -> None:
        display = []
        for part in command:
            text = str(part)
            if text == str(ROOT):
                text = "."
            elif text.startswith(str(ROOT) + "/"):
                text = text[len(str(ROOT)) + 1 :]
            display.append(text)
        self.commands.append(" ".join(display))
        self.seed_runs += seeds
        self.iterations += iterations


def run(result: Result, command: list[str], env: dict[str, str] | None = None,
        seeds: int = 1, iterations: int = 0, timeout: int = 1800) -> bool:
    merged = os.environ.copy()
    merged.update(env or {})
    result.record(command, seeds, iterations)
    try:
        completed = subprocess.run(
            command,
            cwd=str(ROOT),
            env=merged,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        result.failures += 1
        result.output.append(f"TIMEOUT: {' '.join(command)}")
        return False
    if completed.returncode != 0:
        result.failures += 1
        tail = (completed.stdout + completed.stderr).splitlines()[-20:]
        display = " ".join(str(part).replace(str(ROOT) + "/", "") for part in command)
        result.output.append(
            f"FAIL {completed.returncode}: {display}\n" + "\n".join(tail)
        )
        return False
    return True


def host_binary(name: str) -> Path:
    return HOST_BUILD / name


def run_host_seeds(result: Result, name: str, seeds: list[int], iterations: int) -> None:
    binary = host_binary(name)
    if not binary.is_file():
        result.failures += 1
        result.output.append(f"missing host fuzz binary: {binary}")
        return
    for seed in seeds:
        run(result, [str(binary), "--seed", str(seed), "--iterations", str(iterations)],
            seeds=1, iterations=iterations, timeout=600)


def run_scenarios(result: Result, scenarios: list[str], sim: Path = DEFAULT_SIM) -> None:
    if not sim.is_file():
        result.failures += 1
        result.output.append(f"missing simulator binary: {sim}")
        return
    for scenario in scenarios:
        path = ROOT / scenario
        if not path.is_file():
            result.failures += 1
            result.output.append(f"missing scenario: {scenario}")
            continue
        run(result, [str(sim), "--script", str(path)],
            env={"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"},
            seeds=1, iterations=1, timeout=300)


def run_ui(result: Result, physical: bool) -> None:
    binaries = [
        DEFAULT_SIM,
        ROOT / "sim/build-stickc/furble-sim",
        ROOT / "sim/build-core/furble-sim",
    ]
    for binary in binaries:
        if not binary.is_file():
            result.failures += 1
            result.output.append(f"missing simulator binary: {binary}")
            continue
        env = {
            "SDL_VIDEODRIVER": "dummy",
            "SDL_AUDIODRIVER": "dummy",
            "FURBLE_SIM_BIN": str(binary),
            "FURBLE_FUZZ_SEEDS": " ".join(str(seed) for seed in SEEDS["ui"]["full"]),
            "FURBLE_FUZZ_STEPS": str(SEEDS["ui"]["full_steps"]),
            "FURBLE_FUZZ_REPEAT_SEED": "31337",
        }
        if physical:
            env["FURBLE_SIM_NO_TOUCH"] = "1"
        run(result, ["sh", "sim/scripts/run-fuzz.sh"], env=env,
            seeds=len(SEEDS["ui"]["full"]),
            iterations=len(SEEDS["ui"]["full"]) * SEEDS["ui"]["full_steps"], timeout=3600)
    # The hardware-model scenarios name the StickS3 panel explicitly.
    run_scenarios(result, ["sim/scenarios/e2e/hw-buttons-event-count.txt"])
    if not physical:
        run_scenarios(result, ["sim/scenarios/e2e/hw-display-single-buffer.txt"])


def run_target(target_id: str) -> Result:
    result = Result(target_id)
    if target_id == "fuzz.ui.touch":
        run_ui(result, physical=False)
    elif target_id == "fuzz.ui.physical-buttons":
        run_ui(result, physical=True)
    elif target_id == "fuzz.webui.http":
        run_host_seeds(result, "webui_fuzz", SEEDS["webui"]["full"],
                       SEEDS["webui"]["full_iterations"])
        run_scenarios(result, [
            "sim/scenarios/e2e/hw-memory-webui-post394.txt",
            "sim/scenarios/e2e/hw-memory-webui-identity.txt",
            "sim/scenarios/e2e/memory-webui-fail-closed.txt",
            "sim/scenarios/bughunt/console-ui-workflow.txt",
        ])
    elif target_id == "fuzz.companion.ble":
        run_host_seeds(result, "companion_fuzz", SEEDS["companion"]["full"],
                       SEEDS["companion"]["full_iterations"])
        run_scenarios(result, [
            "sim/scenarios/e2e/hw-ble-auth-gatt-reidentify.txt",
            "sim/scenarios/e2e/hw-memory-post394-budget.txt",
        ])
    elif target_id == "fuzz.mqtt":
        run_host_seeds(result, "mqtt_fuzz", SEEDS["mqtt"]["full"],
                       SEEDS["mqtt"]["full_iterations"])
        run_scenarios(result, [
            "sim/scenarios/e2e/hw-memory-mqtt-10k-success.txt",
            "sim/scenarios/e2e/hw-memory-mqtt-tls-6k.txt",
            "sim/scenarios/e2e/hw-mqtt-start-gate-below.txt",
            "sim/scenarios/e2e/hw-mqtt-start-gate-at.txt",
            "sim/scenarios/e2e/hw-mqtt-start-gate-above.txt",
            "sim/scenarios/e2e/hw-mqtt-discovery-gate-below.txt",
            "sim/scenarios/e2e/hw-mqtt-discovery-gate-at.txt",
            "sim/scenarios/e2e/hw-mqtt-discovery-gate-above.txt",
            "sim/scenarios/e2e/hw-mqtt-ha-cache-toggle.txt",
        ])
    elif target_id == "fuzz.gps.ubx":
        run_host_seeds(result, "gps_ubx_fuzz", SEEDS["gps_ubx"]["full"],
                       SEEDS["gps_ubx"]["full_iterations"])
        run_scenarios(result, [
            "sim/scenarios/e2e/gps-ubx-detect.txt",
            "sim/scenarios/e2e/gps-ubx-nak.txt",
            "sim/scenarios/e2e/gps-ubx-fallback-nmea.txt",
        ])
    elif target_id == "fuzz.ota":
        binary = host_binary("ota_mqtt_protocol_test")
        if binary.is_file():
            run(result, [str(binary)], seeds=1, iterations=1, timeout=600)
        else:
            result.failures += 1
            result.output.append(f"missing host fuzz binary: {binary}")
        run_scenarios(result, [
            "sim/scenarios/e2e/hw-ota-slot-rollback.txt",
            "sim/scenarios/e2e/hw-crash-reset-matrix.txt",
        ])
    elif target_id == "fuzz.memory.alloc-failure":
        binary = host_binary("settings_migration_fuzz")
        if binary.is_file():
            run(result, [str(binary)], seeds=1, iterations=1, timeout=300)
        else:
            result.failures += 1
            result.output.append(f"missing host fuzz binary: {binary}")
        run(result, [sys.executable, "tests/fuzz/memory_fuzz.py",
                     "--sim", str(DEFAULT_SIM), "--seeds",
                     *[str(seed) for seed in SEEDS["memory"]["full"]]],
            env={"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"},
            seeds=len(SEEDS["memory"]["full"]), iterations=1, timeout=1800)
        run(result, [sys.executable, "tests/fuzz/memory_fuzz.py",
                     "--sim", str(MQTT_SIM), "--mqtt", "--seeds",
                     *[str(seed) for seed in SEEDS["memory"]["full"]]],
            env={"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"},
            seeds=len(SEEDS["memory"]["full"]), iterations=1, timeout=1800)
        run_scenarios(result, ["sim/scenarios/e2e/hw-memory-post394-budget.txt"])
    elif target_id == "fuzz.memory.fragmentation":
        for seed in SEEDS["mqtt"]["full"]:
            run(result,
                [str(host_binary("mqtt_fuzz")), "--seed", str(seed),
                 "--iterations", str(SEEDS["mqtt"]["full_iterations"]),
                 "--discovery-oom"],
                seeds=1, iterations=SEEDS["mqtt"]["full_iterations"], timeout=600)
        run_scenarios(result, [
            "sim/scenarios/e2e/hw-mqtt-ha-discovery-fragments.txt",
            "sim/scenarios/e2e/hw-memory-mqtt-ha-ok.txt",
            "sim/scenarios/e2e/hw-memory-mqtt-ha-oom.txt",
        ])
    elif target_id == "fuzz.psram":
        for name in ("bt_debug_journal_s3_test", "bt_debug_journal_s3_fallback_test"):
            binary = host_binary(name)
            if binary.is_file():
                run(result, [str(binary)], seeds=1, iterations=1, timeout=300)
            else:
                result.failures += 1
                result.output.append(f"missing host fuzz binary: {binary}")
        run_scenarios(result, [
            "sim/scenarios/e2e/hw-psram-init-fail.txt",
            "sim/scenarios/e2e/hw-psram-absent-core.txt",
            "sim/scenarios/e2e/hw-psram-octal-sticks3.txt",
            "sim/scenarios/e2e/hw-memory-psram-icon-octal.txt",
            "sim/scenarios/e2e/hw-boot-psram-memtest-debug.txt",
            "sim/scenarios/e2e/hw-boot-psram-memtest-release.txt",
        ])
    elif target_id == "fuzz.nvs":
        migration = host_binary("settings_migration_fuzz")
        if migration.is_file():
            run(result, [str(migration)], seeds=1, iterations=1, timeout=300)
        else:
            result.failures += 1
            result.output.append(f"missing host fuzz binary: {migration}")
        binary = host_binary("settings_nvs_roundtrip_test")
        if binary.is_file():
            run(result, [str(binary)], seeds=1, iterations=1, timeout=300)
        else:
            result.failures += 1
            result.output.append(f"missing host fuzz binary: {binary}")
        run_scenarios(result, [
            "sim/scenarios/e2e/hw-boot-settings-migration.txt",
            "sim/scenarios/e2e/hw-boot-wifi-fast-connect.txt",
            "sim/scenarios/e2e/crash-record-survives-restart.txt",
            "sim/scenarios/e2e/ui-restart-persist.txt",
            "sim/scenarios/e2e/hw-power-button-state-machine.txt",
            "sim/scenarios/e2e/hw-usb-jtag-rom-stuck-recovery.txt",
            "sim/scenarios/e2e/hw-pmic-sticks3.txt",
        ])
    else:
        result.failures += 1
        result.output.append(f"unknown fuzz target: {target_id}")
    return result


def as_json(result: Result) -> dict:
    status = "pass" if result.failures == 0 else "fail"
    return {
        "id": result.target_id,
        "status": status,
        "seeds": result.seed_runs,
        "iterations": result.iterations,
        "failures": result.failures,
        "commands": result.commands,
        "known_failing_seeds": result.known_failing_seeds,
        "output": result.output,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", choices=TARGET_IDS, action="append", default=[])
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--results", type=Path,
                        default=ROOT / "sim/coverage/fuzz_results.json")
    args = parser.parse_args()
    if not args.all and not args.target:
        parser.error("choose --all or at least one --target")

    selected = TARGET_IDS if args.all else args.target
    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                            capture_output=True, text=True).stdout.strip()
    records = []
    for target_id in selected:
        result = run_target(target_id)
        record = as_json(result)
        records.append(record)
        print(f"{target_id}: {record['status']} seeds={record['seeds']} "
              f"iterations={record['iterations']} failures={record['failures']}",
              file=sys.stderr)
        if result.output:
            print("\n".join(result.output[-20:]), file=sys.stderr)

    status = "pass" if all(record["status"] == "pass" for record in records) else "fail"
    document = {
        "version": 1,
        "status": status,
        "commit": commit,
        "targets": records,
        "fixed_regressions": FIXED_REGRESSIONS,
        "summary": {
            "targets": len(records),
            "passed": sum(1 for record in records if record["status"] == "pass"),
            "failed": sum(1 for record in records if record["status"] != "pass"),
            "seed_runs": sum(record["seeds"] for record in records),
            "iterations": sum(record["iterations"] for record in records),
            "failures": sum(record["failures"] for record in records),
        },
    }
    args.results.parent.mkdir(parents=True, exist_ok=True)
    args.results.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {args.results}: {status}")
    return 0 if status == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
