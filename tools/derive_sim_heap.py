#!/usr/bin/env python3
"""Derive each board's boot internal heap from its firmware build and code.

No hardware is involved. Two figures are derived per release env:

1. Heap at app start: the internal 8-bit (DRAM and DIRAM) regions that the
   pinned ESP-IDF registers for the chip (components/heap/port/<chip>/
   memory_layout.c), minus the ranges it reserves. The static data and BSS
   reservation (`_data_start`..`_heap_start`) and the IRAM code reservation
   (`_iram_start`..`_iram_end`) are read from the env's own firmware.elf, so
   the BT controller, .data, .bss and IRAM sizes of that exact image are used.

2. Attributable boot charges: heap allocations made before the first UI frame
   that the source names with a size: kernel and IDF task stacks and TCBs, the
   BT controller task, NimBLE pools, static WiFi RX buffers, furble boot tasks
   and LVGL draw buffers. Every term carries the file or sdkconfig key that
   sizes it.

`derived_free = heap_at_start - attributable`. That is an upper bound: the
LVGL object tree, the closed WiFi driver's own task and allocations, NimBLE
host state and lwIP control blocks are not attributable from code and remain
in the reported gap. The existing StickS3 console measurement is a cross-check
only, never an input.

Usage:
  derive_sim_heap.py [--env ENV ...] [--output sim/coverage/board_heap.json]
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IDF = Path.home() / ".platformio/packages/framework-espidf"

ENVS = {
    # env: (chip, panel width, panel height, profile id used by the simulator)
    "m5stick-c": ("esp32", 80, 160, "stickc"),
    "m5stick-c-plus": ("esp32", 135, 240, "stickc-plus"),
    "m5stick-s3": ("esp32s3", 135, 240, "sticks3"),
    "m5stack-core": ("esp32", 320, 240, "core"),
    "m5stack-core2": ("esp32", 320, 240, "core2"),
    "waveshare-s3-eth": ("esp32s3", 0, 0, "waveshare-s3-eth"),
}

# Measured on hardware and already recorded in sim/scenarios/e2e headers. Used
# only to report the gap, never to derive a value.
MEASURED = {"m5stick-s3": {"free": 30007, "source": "sim/scenarios/e2e/hw-memory-post394-budget.txt"}}

# Internal 8-bit regions per chip, from memory_layout.c in the pinned SDK.
# Each entry is (start, size, name). Startup-stack regions are included: the
# heap reclaims them once app_main runs.
REGIONS = {
    "esp32": [
        (0x3FFAE000, 0x2000, "pool 16"),
        (0x3FFB0000, 0x8000, "pool 15"),
        (0x3FFB8000, 0x8000, "pool 14"),
        *[(0x3FFC0000 + i * 0x2000, 0x2000, f"pool 10-13 page {i}") for i in range(16)],
        (0x3FFE0000, 0x4000, "pool 9 blk 1"),
        (0x3FFE4000, 0x4000, "pool 9 blk 0"),
        (0x3FFE8000, 0x8000, "pool 8"),
        (0x3FFF0000, 0x8000, "pool 7"),
        (0x3FFF8000, 0x4000, "pool 6 blk 1"),
        (0x3FFFC000, 0x4000, "pool 6 blk 0"),
    ],
    # APP_USABLE_DRAM_END and SOC_DIRAM_DRAM_HIGH bound level 8; the ROM
    # reserved tail is recycled in app_main, so the whole 0x3FCE0000..
    # 0x3FCF0000 range is usable.
    "esp32s3": [
        (0x3FC88000, 0x8000, "level 2"),
        (0x3FC90000, 0x10000, "level 3"),
        (0x3FCA0000, 0x10000, "level 4"),
        (0x3FCB0000, 0x10000, "level 5"),
        (0x3FCC0000, 0x10000, "level 6"),
        (0x3FCD0000, 0x10000, "level 7"),
        (0x3FCE0000, 0x10000, "level 8"),
    ],
}

# Fixed reservations from memory_layout.c, besides the ELF-derived ones.
FIXED_RESERVED = {
    "esp32": [
        (0x3FFE0000, 0x3FFE0440, "rom_pro_data"),
        (0x3FFAE000, 0x3FFAE6E0, "rom_data"),
    ],
    "esp32s3": [],
}
# Only on dual-core ESP32 builds.
ESP32_APP_ROM = (0x3FFE3F20, 0x3FFE4350, "rom_app_data")

# ESP32 BT controller DRAM that stays reserved after NimBLE's
# esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT) (nimble_port.c). The
# reservations are in components/bt/controller/esp32/bt.c and the addresses in
# components/bt/include/esp32/include/esp_bt.h. BLE-only builds have no sync
# connections, so the BR/EDR window 0x3ffb2730..0x3ffb6388 returns to the heap.
ESP32_BT_RESERVED = [
    (0x3FFAE6E0, 0x3FFAFF10, "rom_bt_data"),
    (0x3FFB0000, 0x3FFB2730, "rom_bt_em (BTDM0, BLE, BTDM1)"),
    (0x3FFB8000, 0x3FFB9A20, "rom_bt_bss"),
    (0x3FFBDB28, 0x3FFBDB5C, "rom_bt_misc"),
]

# Per-block heap cost on the 32-bit port (sim/heap_model.cpp, blockBytes):
# TLSF header 4 bytes, comprehensive poisoning 12 bytes, 4-byte alignment.
POISON = 12
HEADER = 4


def block(size: int) -> int:
    payload = (size + POISON + 3) & ~3
    return max(payload, 12) + HEADER


def sdkconfig(env: str) -> dict:
    values = {}
    for line in (ROOT / f"sdkconfig.{env}").read_text().splitlines():
        match = re.match(r"^(CONFIG_[A-Z0-9_]+)=(.*)$", line)
        if match:
            raw = match.group(2).strip('"')
            values[match.group(1)] = 1 if raw == "y" else (int(raw) if raw.isdigit() else raw)
    return values


def tool(pattern: str, fallback: str) -> str:
    candidates = sorted((Path.home() / ".platformio/packages").glob(pattern))
    found = str(candidates[0]) if candidates else shutil.which(fallback)
    if found is None:
        raise SystemExit(f"{fallback} not found; install the PlatformIO Xtensa toolchain")
    return found


def tcb_bytes(elf: Path) -> int:
    """sizeof(StaticTask_t) from the image's own DWARF."""
    gdb = tool("tool-xtensa-esp-elf-gdb/bin/xtensa-esp*-elf-gdb", "gdb")
    output = subprocess.run([gdb, "-batch", "-ex", "print sizeof(StaticTask_t)", str(elf)],
                            capture_output=True, text=True, check=True).stdout
    match = re.search(r"= (\d+)", output)
    if match is None:
        raise SystemExit(f"{elf}: could not read sizeof(StaticTask_t)")
    return int(match.group(1))


def symbols(elf: Path, wanted: set) -> dict:
    nm = tool("toolchain-xtensa-esp-elf/bin/xtensa-esp-elf-nm", "nm")
    output = subprocess.run([nm, str(elf)], capture_output=True, text=True, check=True).stdout
    found = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in wanted:
            found[parts[2]] = int(parts[0], 16)
    missing = wanted - set(found)
    if missing:
        raise SystemExit(f"{elf}: missing symbols {sorted(missing)}")
    return found


def subtract(regions: list, reserved: list) -> tuple:
    """Free bytes and the largest contiguous span after the reservations.

    Adjacent registered regions merge into one span, as heap_caps_init merges
    contiguous regions of the same type into one TLSF heap.
    """
    spans = []
    for start, size, _ in regions:
        end = start + size
        cursor = start
        cuts = sorted((max(start, lo), min(end, hi)) for lo, hi, _ in reserved if lo < end and hi > start)
        for lo, hi in cuts:
            if lo > cursor:
                spans.append([cursor, lo])
            cursor = max(cursor, hi)
        if end > cursor:
            spans.append([cursor, end])
    merged = []
    for lo, hi in sorted(spans):
        if merged and merged[-1][1] == lo:
            merged[-1][1] = hi
        else:
            merged.append([lo, hi])
    total = sum(hi - lo for lo, hi in merged)
    largest = max((hi - lo for lo, hi in merged), default=0)
    return total, largest


def task(name: str, stack: int, tcb: int, source: str) -> dict:
    return {"term": name, "bytes": block(stack) + block(tcb), "source": source}


def attributable(width: int, cfg: dict, tcb: int) -> list:
    cores = 1 if cfg.get("CONFIG_FREERTOS_UNICORE") else 2
    extra = 0 if cfg.get("CONFIG_NEWLIB_NANO_FORMAT") else 512
    terms = []
    for core in range(cores):
        terms.append(task(f"idle{core}", cfg["CONFIG_FREERTOS_IDLE_TASK_STACKSIZE"], tcb,
                          "components/freertos/port_common.c, CONFIG_FREERTOS_IDLE_TASK_STACKSIZE"))
    if cfg.get("CONFIG_FREERTOS_USE_TIMERS"):
        terms.append(task("tmr_svc", cfg["CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH"], tcb,
                          "components/freertos/port_common.c, CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH"))
    if cores == 2:
        for core in range(cores):
            terms.append(task(f"ipc{core}", cfg["CONFIG_ESP_IPC_TASK_STACK_SIZE"], tcb,
                              "components/esp_system/esp_ipc.c, CONFIG_ESP_IPC_TASK_STACK_SIZE"))
    terms.append(task("main", cfg["CONFIG_ESP_MAIN_TASK_STACK_SIZE"], tcb,
                      "components/freertos/app_startup.c, CONFIG_ESP_MAIN_TASK_STACK_SIZE"))
    terms.append(task("esp_timer", cfg["CONFIG_ESP_TIMER_TASK_STACK_SIZE"], tcb,
                      "CONFIG_ESP_TIMER_TASK_STACK_SIZE"))
    terms.append(task("sys_evt", cfg["CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE"], tcb,
                      "CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE"))
    if cfg.get("CONFIG_ESP_WIFI_ENABLED"):
        terms.append(task("tiT", cfg["CONFIG_LWIP_TCPIP_TASK_STACK_SIZE"], tcb,
                          "CONFIG_LWIP_TCPIP_TASK_STACK_SIZE"))
        rx = cfg["CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM"]
        terms.append({"term": "wifi_static_rx", "bytes": rx * block(1600),
                      "source": "CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM x 1600 (ESP-IDF Wi-Fi buffer usage)"})
        terms.append(task("wifi_furble", 4096, tcb, "src/FurbleWiFi.cpp TASK_STACK"))
    if cfg.get("CONFIG_BT_ENABLED"):
        terms.append(task("btController", 3584 + extra, tcb,
                          "components/esp_system/include/esp_task.h ESP_TASK_BT_CONTROLLER_STACK"))
        terms.append(task("nimble_host", cfg["CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE"], tcb,
                          "CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE"))
        if not cfg.get("CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL"):
            msys = (cfg["CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT"] * cfg["CONFIG_BT_NIMBLE_MSYS_1_BLOCK_SIZE"]
                    + cfg["CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT"] * cfg["CONFIG_BT_NIMBLE_MSYS_2_BLOCK_SIZE"])
            terms.append({"term": "nimble_msys", "bytes": block(msys),
                          "source": "CONFIG_BT_NIMBLE_MSYS_1/2_BLOCK_COUNT x BLOCK_SIZE"})
            transport = (cfg["CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT"] * cfg["CONFIG_BT_NIMBLE_TRANSPORT_ACL_SIZE"]
                         + (cfg["CONFIG_BT_NIMBLE_TRANSPORT_EVT_COUNT"] + cfg["CONFIG_BT_NIMBLE_TRANSPORT_EVT_DISCARD_COUNT"])
                         * cfg["CONFIG_BT_NIMBLE_TRANSPORT_EVT_SIZE"])
            terms.append({"term": "nimble_transport", "bytes": block(transport),
                          "source": "CONFIG_BT_NIMBLE_TRANSPORT_ACL/EVT counts x sizes"})
    terms.append(task("control", 8192, tcb, "src/main.cpp control task"))
    terms.append(task("gps", 4096, tcb, "src/FurbleGPS.cpp gps task"))
    terms.append(task("companion", 3072, tcb, "src/FurbleCompanion.cpp COMPANION_TASK_STACK_BYTES"))
    if width:
        lines = 15
        buffers = 1 if width <= 160 else 2
        draw = width * lines * 2
        terms.append({"term": "lvgl_draw", "bytes": buffers * block(draw),
                      "source": "include/FurbleUIMemory.h PARTIAL_LINES, singleDrawBuffer"})
    # CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL is not a term: esp_psram.c
    # re-registers that pool with INTERNAL | DMA | 8BIT caps, so it stays in
    # heap_caps_get_free_size(INTERNAL | 8BIT). Plain malloc cannot use it.
    return terms


def derive(env: str) -> dict:
    chip, width, _, profile = ENVS[env]
    elf = ROOT / ".pio/build" / env / "firmware.elf"
    if not elf.is_file():
        raise SystemExit(f"{elf} not found; run: FURBLE_VERSION=dev FURBLE_TEST=0 pio run -e {env}")
    cfg = sdkconfig(env)
    sym = symbols(elf, {"_data_start", "_heap_start", "_iram_start", "_iram_end"})
    reserved = list(FIXED_RESERVED[chip])
    if chip == "esp32" and not cfg.get("CONFIG_FREERTOS_UNICORE"):
        reserved.append(ESP32_APP_ROM)
    if chip == "esp32" and cfg.get("CONFIG_BT_ENABLED"):
        if not cfg.get("CONFIG_BTDM_CTRL_MODE_BLE_ONLY"):
            raise SystemExit(f"{env}: only BLE-only BT controller builds are modelled")
        reserved.extend(ESP32_BT_RESERVED)
    reserved.append((sym["_data_start"], sym["_heap_start"], "dram_data (.data, .bss, BT)"))
    if chip == "esp32s3":
        # Internal SRAM is shared I/D on the S3: IRAM code also occupies DIRAM.
        offset = 0x40378000 - 0x3FC88000
        reserved.append((sym["_iram_start"] - offset, sym["_iram_end"] - offset, "iram_code"))
    at_start, largest_span = subtract(REGIONS[chip], reserved)
    tcb = tcb_bytes(elf)
    terms = attributable(width, cfg, tcb)
    charged = sum(term["bytes"] for term in terms)
    result = {
        "env": env,
        "profile": profile,
        "chip": chip,
        "elf_symbols": {name: hex(value) for name, value in sorted(sym.items())},
        "static_dram_bytes": sym["_heap_start"] - sym["_data_start"],
        "tcb_bytes": tcb,
        "heap_at_app_start": at_start,
        "largest_span_at_app_start": largest_span,
        "attributable_boot": terms,
        "attributable_bytes": charged,
        "derived_free_upper_bound": at_start - charged,
        # Boot charges land in the largest span first, so the largest block
        # can be no larger than the span and no larger than the free total.
        "derived_largest_upper_bound": min(largest_span, at_start - charged),
        "status": "derived",
    }
    if cfg.get("CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL"):
        result["internal_reserved_for_caps_only"] = cfg["CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL"]
    if env in MEASURED:
        measured = MEASURED[env]["free"]
        result["measured_free"] = measured
        result["measured_source"] = MEASURED[env]["source"]
        result["unattributed_gap"] = result["derived_free_upper_bound"] - measured
    return result


def main(argv: list) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--env", action="append", choices=sorted(ENVS))
    parser.add_argument("--output", type=Path, default=ROOT / "sim/coverage/board_heap.json")
    args = parser.parse_args(argv)
    envs = args.env or list(ENVS)
    boards = [derive(env) for env in envs]
    document = {
        "version": 1,
        "sdk": "ESP-IDF 5.5.3 (pinned PlatformIO framework-espidf)",
        "method": "tools/derive_sim_heap.py docstring",
        "boards": boards,
    }
    args.output.write_text(json.dumps(document, indent=2) + "\n")
    for board in boards:
        line = (f"{board['env']:18} static {board['static_dram_bytes']:7} at_start {board['heap_at_app_start']:7} "
                f"attributable {board['attributable_bytes']:7} derived {board['derived_free_upper_bound']:7}")
        if "measured_free" in board:
            line += f" measured {board['measured_free']} gap {board['unattributed_gap']}"
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
