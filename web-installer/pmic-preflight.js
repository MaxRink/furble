// Browser equivalent of tools/flash_prepare.py for the StickS3 web installer.
// ESP Web Tools owns its own serial connection, so this preflight deliberately
// closes the port before handing control back to the install-button element.

export const PMIC_ACKNOWLEDGEMENTS = Object.freeze([
  "flash.ready: true",
  "flash.watchdog: disabled",
  "flash.download_recovery: unlocked",
]);

export function collectAcknowledgement(line, seen) {
  const normalized = line.trim();
  if (PMIC_ACKNOWLEDGEMENTS.includes(normalized)) {
    seen.add(normalized);
  }
  return normalized;
}

function missingAcknowledgements(seen) {
  return PMIC_ACKNOWLEDGEMENTS.filter((ack) => !seen.has(ack));
}

async function readWithTimeout(reader, timeoutMs) {
  let timer;
  try {
    return await Promise.race([
      reader.read(),
      new Promise((resolve) => {
        timer = setTimeout(() => resolve({ timedOut: true }), timeoutMs);
      }),
    ]);
  } finally {
    clearTimeout(timer);
  }
}

/**
 * Run the PMIC safety handshake on a connected developer-console image.
 *
 * The serial API is injectable so the protocol can be tested without a
 * browser or hardware. The returned port is always closed before resolving.
 */
export async function runPmicPreflight(
  serialApi,
  { timeoutMs = 5000, onLine = () => {} } = {},
) {
  const port = await serialApi.requestPort();
  let reader;
  let writer;
  const seen = new Set();
  try {
    await port.open({ baudRate: 115200, bufferSize: 8192 });
    writer = port.writable.getWriter();
    await writer.write(new TextEncoder().encode("flash prepare\n"));
    writer.releaseLock();
    writer = undefined;

    reader = port.readable.getReader();
    const decoder = new TextDecoder();
    let pending = "";
    const deadline = performance.now() + timeoutMs;
    while (performance.now() < deadline) {
      const result = await readWithTimeout(
        reader,
        Math.max(1, deadline - performance.now()),
      );
      if (result.timedOut || result.done) {
        break;
      }
      pending += decoder.decode(result.value, { stream: true });
      let newline;
      while ((newline = pending.indexOf("\n")) >= 0) {
        const line = pending.slice(0, newline).replace(/\r$/, "");
        pending = pending.slice(newline + 1);
        onLine(collectAcknowledgement(line, seen));
      }
    }
    pending += decoder.decode();
    if (pending) {
      onLine(collectAcknowledgement(pending, seen));
    }
    const missing = missingAcknowledgements(seen);
    if (missing.length) {
      throw new Error(
        `PMIC preflight incomplete (${missing.join(", ")}). ` +
          "Remove battery power, restore it, hold the StickS3 side button " +
          "until the green LED flashes, then retry.",
      );
    }
  } finally {
    if (reader) {
      try {
        await reader.cancel();
      } finally {
        reader.releaseLock();
      }
    }
    if (writer) {
      writer.releaseLock();
    }
    try {
      await port.close();
    } catch (_error) {
      // A port that is already closed is safe to hand back to ESP Web Tools.
    }
  }
}

if (typeof document !== "undefined") {
  const installActivate = document.getElementById("installActivate");
  const pmicStatus = document.getElementById("pmicStatus");
  const debug = document.getElementById("debug");
  let preflightBusy = false;
  let preflightPassed = false;

  function selectedPlatform() {
    return document.querySelector('input[name="type"]:checked')?.value;
  }

  function needsPmicPreflight() {
    return selectedPlatform() === "m5stick-s3";
  }

  function setStatus(message, isError = false) {
    pmicStatus.textContent = message;
    pmicStatus.style.color = isError ? "#b00020" : "";
  }

  function refreshStatus() {
    preflightPassed = false;
    if (needsPmicPreflight()) {
      setStatus(
        "M5StickS3 selected. Before flashing, the installer will verify " +
          "PMIC watchdog and long-press recovery safety.",
      );
    } else {
      setStatus("");
    }
  }

  async function prepareStickS3() {
    if (!("serial" in navigator)) {
      throw new Error(
        "This browser does not provide Web Serial. Use Chrome or Edge on a desktop.",
      );
    }
    setStatus("Select the StickS3 serial port for PMIC preflight...");
    await runPmicPreflight(navigator.serial, {
      onLine: (line) => setStatus(`PMIC: ${line}`),
    });
  }

  installActivate.addEventListener("click", async (event) => {
    // The ESP Web Tools element listens on this slotted button too. Stop the
    // first event while the preflight runs, then replay it after success so the
    // stock installer still owns chip detection and flashing.
    if (!needsPmicPreflight() || preflightPassed) {
      return;
    }
    event.preventDefault();
    event.stopPropagation();
    if (preflightBusy) {
      return;
    }
    preflightBusy = true;
    try {
      await prepareStickS3();
      preflightPassed = true;
      setStatus(
        "PMIC preflight passed. Select the same serial port again to install.",
      );
      installActivate.click();
    } catch (error) {
      setStatus(error instanceof Error ? error.message : String(error), true);
    } finally {
      preflightBusy = false;
    }
  });

  document.querySelectorAll('input[name="type"]').forEach((radio) => {
    radio.addEventListener("change", refreshStatus);
  });
  debug.addEventListener("change", refreshStatus);
  refreshStatus();
}

// Expose the protocol for browser-console diagnostics and lightweight tests.
if (typeof window !== "undefined") {
  window.FurblePMICPreflight = { collectAcknowledgement, runPmicPreflight };
}
