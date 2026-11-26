// Host test for the browser provisioning encoder and response parser.
//
// Run with: node tests/web-installer/provision.test.cjs
//
// The expectations in provision-fixture.json are encoded by the browser codec
// here and by the production codec in tests/host/provision_tlv_test.cpp, so a
// format change fails on both sides. tests/test_web_installer.py runs this file
// from the Python tooling suite.

"use strict";

const path = require("node:path");
const fs = require("node:fs");
const provision = require(path.join(__dirname, "..", "..", "web-installer", "provision.js"));

const fixture = JSON.parse(
  fs.readFileSync(path.join(__dirname, "provision-fixture.json"), "utf8"),
);

let failures = 0;
let checks = 0;

function check(condition, message) {
  checks++;
  if (!condition) {
    failures++;
    process.stderr.write(`  FAIL: ${message}\n`);
  }
}

function test(name, body) {
  process.stderr.write(`test: ${name}\n`);
  body();
}

function hex(bytes) {
  return bytes.map((byte) => byte.toString(16).padStart(2, "0")).join("");
}

function expectThrows(body, needle, message) {
  try {
    body();
  } catch (error) {
    check(
      String(error.message).includes(needle),
      `${message} (message was: ${error.message})`,
    );
    return;
  }
  check(false, `${message} (nothing was thrown)`);
}

// --- The fixture the production codec also encodes ---------------------------

test("the browser encoder matches the shared multi-field fixture", () => {
  const bytes = provision.encodeBundle(fixture.fields);
  check(bytes.length === fixture.expectedByteCount, "the bundle length matches");
  check(hex(bytes) === fixture.expectedHex, "the bundle bytes match the fixture");
  const plan = provision.buildCommand(fixture.fields);
  check(plan.base64 === fixture.expectedBase64, "the base64 text matches the fixture");
  check(
    plan.command.length === fixture.expectedCommandChars,
    "the command length matches the fixture",
  );
  check(
    plan.command.startsWith("provision base64:"),
    "the command names the encoding explicitly",
  );
  check(
    plan.command.length <= provision.MAX_COMMAND_CHARS,
    "the fixture fits the console line limit",
  );
});

test("records carry the required bit and the production field order", () => {
  const bytes = provision.encodeBundle({ wifiSsid: "ab", wifiPsk: "cd", settings: [] });
  check(bytes[0] === (provision.TAG.WIFI_SSID | 0x80), "the SSID tag is required");
  check(bytes[1] === provision.TYPE.STRING, "the SSID record is a string");
  check(bytes[2] === 2, "the SSID length is one byte");
  check(bytes[5] === (provision.TAG.WIFI_PSK | 0x80), "the PSK record follows the SSID");
  const setting = provision.encodeBundle({
    settings: [{ wireId: 3, type: provision.TYPE.STRING, value: "Dark" }],
  });
  check(setting[0] === (provision.TAG.SETTING | 0x80), "the setting tag is required");
  check(setting[3] === 3, "the setting record starts with its wire id");
});

test("a blank passphrase is on the wire so an open network clears a saved one", () => {
  const bytes = provision.encodeBundle({ wifiSsid: "ab", wifiPsk: "" });
  check(bytes.length === 5 + 3, "the empty PSK record is present");
  check(bytes[5] === (provision.TAG.WIFI_PSK | 0x80), "the empty PSK record is tagged");
  check(bytes[7] === 0, "the empty PSK record carries no value");
});

test("an undefined field is not on the wire", () => {
  const bytes = provision.encodeBundle({ wifiSsid: "ab" });
  check(bytes.length === 5, "only the SSID record is encoded");
});

// --- Field limits, mirroring ProvisionTLV.h ---------------------------------

test("SSID length is checked in UTF-8 bytes", () => {
  check(provision.encodeBundle({ wifiSsid: "s".repeat(32) }).length === 35, "32 bytes are accepted");
  expectThrows(
    () => provision.encodeBundle({ wifiSsid: "s".repeat(33) }),
    "SSID must be 1 to 32 UTF-8 bytes, got 33",
    "a 33-byte SSID is rejected",
  );
  expectThrows(
    () => provision.encodeBundle({ wifiSsid: "" }),
    "SSID must be 1 to 32 UTF-8 bytes",
    "an empty SSID is rejected",
  );
  expectThrows(
    () => provision.encodeBundle({ wifiSsid: "é".repeat(17) }),
    "got 34",
    "a 17 character non-ASCII SSID is rejected at 34 bytes",
  );
});

test("passphrase length is checked in UTF-8 bytes", () => {
  check(
    provision.encodeBundle({ wifiSsid: "ab", wifiPsk: "p".repeat(63) }).length === 5 + 66,
    "63 bytes are accepted",
  );
  expectThrows(
    () => provision.encodeBundle({ wifiSsid: "net", wifiPsk: "p".repeat(64) }),
    "Passphrase must be at most 63 UTF-8 bytes, got 64",
    "a 64-byte passphrase is rejected",
  );
});

test("companion password needs at least one byte", () => {
  expectThrows(
    () => provision.encodeBundle({ companionPassword: "" }),
    "Companion password must be 1 to 63",
    "an empty companion password is rejected",
  );
  expectThrows(
    () => provision.encodeBundle({ companionPassword: "p".repeat(64) }),
    "Companion password must be 1 to 63",
    "a 64-byte companion password is rejected",
  );
});

test("MQTT fields mirror the firmware string rules", () => {
  expectThrows(
    () => provision.encodeBundle({ mqttUri: "192.0.2.10:1883" }),
    "must start with mqtt:// or mqtts://",
    "a URI without a scheme is rejected",
  );
  expectThrows(
    () => provision.encodeBundle({ mqttUri: "mqtt://host name" }),
    "whitespace",
    "a URI with a space is rejected",
  );
  expectThrows(
    () => provision.encodeBundle({ mqttBaseTopic: "furble/+/state" }),
    "wildcards",
    "a wildcard base topic is rejected",
  );
  expectThrows(
    () => provision.encodeBundle({ mqttBaseTopic: "a//b" }),
    "empty level",
    "a doubled slash base topic is rejected",
  );
  check(
    provision.encodeBundle({ mqttBaseTopic: "/furble/studio/" }).length === 3 + 15,
    "a base topic trims its slashes for the length check",
  );
});

test("TX power is a single byte", () => {
  // The firmware accepts any u8 for wire id 4; the page only offers the three
  // documented steps, so the encoder only has to reject a non-byte value.
  const bytes = provision.encodeBundle({
    settings: [{ wireId: 4, type: provision.TYPE.U8, value: 2 }],
  });
  check(bytes.length === 5, "a single byte setting is one record");
  check(bytes[3] === 4, "the record carries the wire id");
  check(bytes[4] === 2, "the record carries the value");
  expectThrows(
    () =>
      provision.encodeBundle({
        settings: [{ wireId: 4, type: provision.TYPE.U8, value: 256 }],
      }),
    "must be a single byte",
    "256 is rejected",
  );
  expectThrows(
    () =>
      provision.encodeBundle({
        settings: [{ wireId: 4, type: provision.TYPE.U8, value: 1.5 }],
      }),
    "must be a single byte",
    "a fractional value is rejected",
  );
});

// --- The console line limit -------------------------------------------------

test("an oversized command is refused before anything is sent", () => {
  const fields = {
    wifiSsid: "s".repeat(32),
    wifiPsk: "p".repeat(63),
    companionPassword: "c".repeat(63),
    settings: [{ wireId: 3, type: provision.TYPE.STRING, value: "t".repeat(40) }],
  };
  let command = null;
  let message = "";
  try {
    command = provision.buildCommand(fields);
  } catch (error) {
    message = error.message;
  }
  check(command === null, "buildCommand refuses the bundle");
  check(message.includes(String(provision.MAX_COMMAND_CHARS)), "the error names the limit");
  check(message.includes("Nothing was sent"), "the error states nothing was sent");
});

// --- Response parsing -------------------------------------------------------

const SUCCESS_LINES = [
  "provision: decoded 135 bytes as base64",
  "provision: wifi_ssid applied",
  "provision: wifi_psk applied",
  "provision: companion_password applied",
  "provision: setting 3 (theme) applied",
  "provision: setting 4 (tx_power) applied",
  "provision: 5 field(s) applied, 0 field(s) deferred",
];

test("a complete success response is a success", () => {
  const result = provision.parseProvisionResponse(SUCCESS_LINES, 135);
  check(result.ok === true, "the step succeeds");
  check(result.applied.length === 5, "every applied field is reported");
  check(result.deferred.length === 0, "nothing is deferred");
  check(result.decodedBytes === 135, "the decoded byte count is reported");
  check(result.errors.length === 0, "there is no error line");
  check(result.reason === "Applied 5 field(s).", "the reason counts the fields");
});

test("an apply error fails the step", () => {
  const result = provision.parseProvisionResponse(
    ["error: provision: bad setting (wire id 52): network setting string is invalid"],
    135,
  );
  check(result.ok === false, "the step fails");
  check(result.errors.length === 1, "the error line is kept");
  check(result.reason.includes("bad setting"), "the reason quotes the firmware error");
});

test("a truncated frame fails the step", () => {
  const result = provision.parseProvisionResponse(
    ["error: provision: truncated record at byte 130"],
    135,
  );
  check(result.ok === false, "the step fails");
  check(result.reason.includes("truncated record"), "the reason names the decode error");
});

test("a repeated frame fails the step", () => {
  const result = provision.parseProvisionResponse(
    ["error: provision: duplicate field at byte 5"],
    135,
  );
  check(result.ok === false, "the step fails");
  check(result.reason.includes("duplicate field"), "the reason names the duplicate");
});

test("a clipped console line fails the step", () => {
  const result = provision.parseProvisionResponse(
    ["error: line longer than 255 bytes, command not run"],
    135,
  );
  check(result.ok === false, "the step fails");
  check(result.reason.includes("255"), "the reason names the line limit");
});

test("a decoded byte mismatch fails the step", () => {
  const result = provision.parseProvisionResponse(SUCCESS_LINES, 200);
  check(result.ok === false, "the step fails");
  check(result.reason.includes("clipped or corrupted"), "the reason names the mismatch");
});

test("an empty bundle is not a success for the browser step", () => {
  const result = provision.parseProvisionResponse(["provision: no fields"], 0);
  check(result.ok === false, "no applied field is a failure");
});

test("a deferred field fails the step", () => {
  const lines = [
    "provision: decoded 20 bytes as base64",
    "provision: mqtt_uri parsed (not applied; backend pending #53)",
    "provision: 0 field(s) applied, 1 field(s) deferred",
  ];
  const result = provision.parseProvisionResponse(lines, 20);
  check(result.ok === false, "a deferred field is not a success");
  check(result.deferred.length === 1, "the deferred field is named");
  check(result.reason.includes("deferred"), "the reason says the backend is missing");
});

test("a timeout fails the step", () => {
  const result = provision.parseProvisionResponse([], 135, {
    timedOut: true,
    timeoutMs: 7000,
  });
  check(result.ok === false, "the step fails");
  check(result.reason.includes("7000"), "the reason names the timeout");
  check(result.reason.includes("Nothing is confirmed"), "the reason claims nothing");
});

test("a malformed response fails the step", () => {
  const result = provision.parseProvisionResponse(["furble console ready, type 'help'"], 135);
  check(result.ok === false, "the step fails");
  check(result.reason.includes("without a provisioning result"), "the reason names the shape");
  check(result.unexpected.length === 1, "the unknown line is reported");
});

test("the command echo never reaches the result", () => {
  const plan = provision.buildCommand(fixture.fields);
  const lines = [plan.command].concat(SUCCESS_LINES);
  const result = provision.parseProvisionResponse(lines, 135);
  check(result.ok === true, "the echo does not fail the step");
  check(
    JSON.stringify(result).includes(fixture.fields.wifiPsk) === false,
    "the passphrase is not echoed into the result",
  );
  check(
    JSON.stringify(result).includes(fixture.fields.companionPassword) === false,
    "the companion password is not echoed into the result",
  );
});

// --- The WebSerial session --------------------------------------------------

function createFakePort(onWrite) {
  const encoder = new TextEncoder();
  const decoder = new TextDecoder();
  const pending = [];
  const waiters = [];
  const writes = [];
  let finished = false;

  function emit(text) {
    if (!text) {
      return;
    }
    pending.push(encoder.encode(text));
    flush();
  }

  function flush() {
    while (waiters.length && pending.length) {
      waiters.shift()({ value: pending.shift(), done: false });
    }
    if (finished) {
      while (waiters.length) {
        waiters.shift()({ value: undefined, done: true });
      }
    }
  }

  return {
    writes: writes,
    closed: false,
    opened: false,
    failOpen: 0,
    async open(options) {
      if (this.failOpen > 0) {
        this.failOpen--;
        throw new Error("Failed to open serial port.");
      }
      this.opened = true;
      this.openOptions = options;
    },
    async close() {
      this.closed = true;
    },
    readable: {
      getReader() {
        return {
          read() {
            if (pending.length) {
              return Promise.resolve({ value: pending.shift(), done: false });
            }
            if (finished) {
              return Promise.resolve({ value: undefined, done: true });
            }
            return new Promise((resolve) => waiters.push(resolve));
          },
          async cancel() {
            finished = true;
            flush();
          },
          releaseLock() {},
        };
      },
    },
    writable: {
      getWriter() {
        return {
          async write(data) {
            const text = decoder.decode(data);
            writes.push(text);
            if (onWrite) {
              onWrite(text, emit);
            }
          },
          releaseLock() {},
        };
      },
    },
  };
}

async function asyncTests() {
  process.stderr.write("test: the WebSerial session reports success, errors and timeouts\n");
  const successWrites = [];
  const successPort = createFakePort((text, emit) => {
    successWrites.push(text);
    if (text === "\r") {
      emit("furble console ready, type 'help'\r\nfurble> ");
      return;
    }
    emit(text + "\n" + SUCCESS_LINES.join("\n") + "\r\nfurble> ");
  });
  const success = await provision.runProvision(successPort, fixture.fields, {
    timeoutMs: 2000,
    promptTimeoutMs: 2000,
    openDelayMs: 1,
  });
  check(success.ok === true, "the session reports the device success");
  check(successPort.closed === true, "the port is handed back");
  check(
    successPort.writes.some((text) => text.startsWith("provision base64:")),
    "the command is written to the port",
  );
  check(
    successPort.writes.some((text) => text.includes(fixture.expectedBase64)),
    "the written command carries the encoded blob",
  );

  const errorPort = createFakePort((text, emit) => {
    if (text === "\r") {
      emit("furble> ");
      return;
    }
    emit(text + "\n" + "error: provision: duplicate field at byte 5\r\nfurble> ");
  });
  const failed = await provision.runProvision(errorPort, fixture.fields, {
    timeoutMs: 2000,
    promptTimeoutMs: 2000,
    openDelayMs: 1,
  });
  check(failed.ok === false, "an error response fails the session");
  check(failed.reason.includes("duplicate field"), "the session reports the firmware error");
  check(errorPort.closed === true, "the port is handed back after a failure");

  const releasePort = createFakePort(() => {});
  const release = await provision.runProvision(releasePort, fixture.fields, {
    timeoutMs: 100,
    promptTimeoutMs: 120,
    openDelayMs: 1,
  });
  check(release.ok === false, "a release build fails the session");
  check(release.reason.includes("debug build"), "the session asks for the debug build");
  check(releasePort.closed === true, "the port is handed back without a console");

  const silentPort = createFakePort((text, emit) => {
    if (text === "\r") {
      emit("furble> ");
    }
  });
  const silent = await provision.runProvision(silentPort, fixture.fields, {
    timeoutMs: 120,
    promptTimeoutMs: 2000,
    openDelayMs: 1,
  });
  check(silent.ok === false, "no result fails the session");
  check(silent.reason.includes("did not report a result"), "the session reports the timeout");

  const slowPort = createFakePort((text, emit) => {
    if (text === "\r") {
      emit("furble> ");
    }
  });
  slowPort.failOpen = 2;
  const slow = await provision.runProvision(slowPort, fixture.fields, {
    timeoutMs: 120,
    promptTimeoutMs: 2000,
    openDelayMs: 1,
    openAttempts: 3,
  });
  check(slow.ok === false, "the reopen retry still runs the session");
  check(slowPort.opened === true, "the port opens after the installer releases it");

  const blockedPort = createFakePort((text, emit) => {
    if (text === "\r") {
      emit("furble> ");
    }
  });
  blockedPort.failOpen = 3;
  let openError = "";
  try {
    await provision.runProvision(blockedPort, fixture.fields, {
      timeoutMs: 100,
      promptTimeoutMs: 2000,
      openDelayMs: 1,
      openAttempts: 3,
    });
  } catch (error) {
    openError = error.message;
  }
  check(openError.includes("Close the installer dialog"), "a held port is reported clearly");
}

// --- The installer panel itself --------------------------------------------

// Minimal DOM good enough for the panel script in index.html. Every element the
// script looks up is created on demand, so the test fails loudly if the page
// starts depending on something new.
function createPanelDom() {
  const elements = new Map();
  const documentListeners = {};
  const boardListeners = [];
  const state = { selected: null };

  function element(id) {
    let text = "";
    const node = {
      id: id,
      children: [],
      listeners: {},
      value: "",
      checked: false,
      disabled: false,
      style: {},
      classList: {
        classes: new Set(),
        add(name) {
          this.classes.add(name);
        },
        remove(name) {
          this.classes.delete(name);
        },
        contains(name) {
          return this.classes.has(name);
        },
        toggle(name, force) {
          if (force === undefined) {
            if (this.classes.has(name)) {
              this.classes.delete(name);
            } else {
              this.classes.add(name);
            }
          } else if (force) {
            this.classes.add(name);
          } else {
            this.classes.delete(name);
          }
        },
      },
      addEventListener(event, callback) {
        node.listeners[event] = callback;
      },
      appendChild(child) {
        node.children.push(child);
      },
      dispatch(event, detail) {
        if (node.listeners[event]) {
          return node.listeners[event]({ detail: detail });
        }
        return undefined;
      },
      click() {
        if (node.listeners.click) {
          return node.listeners.click();
        }
        return undefined;
      },
    };
    Object.defineProperty(node, "textContent", {
      get() {
        return text;
      },
      set(value) {
        text = String(value);
        node.children.length = 0;
      },
    });
    elements.set(id, node);
    return node;
  }

  ["provisionUnsupported", "provisionNeedsDebug", "provisionSupported", "provMqttGroup"].forEach(
    (id) => {
      element(id).classList.add("hidden");
    },
  );

  const document = {
    getElementById(id) {
      return elements.get(id) || element(id);
    },
    querySelector(selector) {
      return selector === 'input[name="type"]:checked' ? state.selected : null;
    },
    querySelectorAll(selector) {
      return {
        forEach(callback) {
          if (selector === 'input[name="type"]') {
            boardListeners.push(callback);
          }
        },
      };
    },
    addEventListener(event, callback) {
      documentListeners[event] = callback;
    },
    createElement() {
      return element("anonymous-" + elements.size);
    },
  };

  function selectBoard(value) {
    state.selected = { value: value };
    boardListeners.forEach((register) => register({ addEventListener() {} }));
  }

  return {
    document: document,
    element: (id) => elements.get(id) || element(id),
    ready() {
      if (documentListeners.DOMContentLoaded) {
        documentListeners.DOMContentLoaded();
      }
    },
    selectBoard: selectBoard,
    visible: (id) => !elements.get(id).classList.contains("hidden"),
    boardListeners: boardListeners,
  };
}

function panelScript() {
  const html = fs.readFileSync(
    path.join(__dirname, "..", "..", "web-installer", "index.html"),
    "utf8",
  );
  const match = /<script id="provision-script">([\s\S]*?)<\/script>/.exec(html);
  if (!match) {
    throw new Error("index.html has no provisioning panel script");
  }
  return match[1];
}

async function panelTests() {
  process.stderr.write("test: the installer panel gates on the debug build and the board\n");
  const vm = require("node:vm");
  const dom = createPanelDom();
  const serial = {
    ports: [],
    requested: 0,
    async getPorts() {
      return serial.ports;
    },
    async requestPort() {
      serial.requested++;
      return serial.ports[0];
    },
  };
  const context = {
    document: dom.document,
    navigator: { serial: serial },
    FurbleProvision: provision,
    Error: Error,
    Number: Number,
    String: String,
    Promise: Promise,
  };
  vm.createContext(context);
  vm.runInContext(panelScript(), context);
  dom.ready();

  check(dom.visible("provisionUnsupported") === false, "a supporting browser shows no warning");
  check(dom.visible("provisionSupported") === false, "the form waits for a debug build");
  check(dom.visible("provisionNeedsDebug") === true, "a release build is refused up front");

  dom.element("debug").checked = true;
  dom.element("provisionNeedsDebug").classList.add("hidden");
  dom.selectBoard("m5stick-c");
  dom.element("debug").dispatch("change");
  check(dom.visible("provisionSupported") === true, "the form appears for a debug build");
  check(dom.visible("provMqttGroup") === false, "a 4 MB board hides the MQTT fields");
  check(dom.element("provSend").disabled === false, "the send button is enabled");

  dom.selectBoard("m5stick-s3");
  dom.element("debug").dispatch("change");
  check(dom.visible("provMqttGroup") === true, "an 8 MB board offers the MQTT fields");

  dom.element("provSsid").value = "panel-net";
  dom.element("provPsk").value = "panel-secret";
  dom.element("provTheme").value = "Dark";
  dom.element("provTxPower").value = "2";
  dom.element("provMqttUri").value = "mqtt://192.0.2.20:1883";
  dom.element("provMqttBase").value = "furble/panel";
  dom.element("provCompanion").value = "panel-companion";

  const expected = provision.buildCommand({
    wifiSsid: "panel-net",
    wifiPsk: "panel-secret",
    companionPassword: "panel-companion",
    mqttUri: "mqtt://192.0.2.20:1883",
    mqttBaseTopic: "furble/panel",
    settings: [
      { wireId: provision.WIRE_ID.THEME, type: provision.TYPE.STRING, value: "Dark" },
      { wireId: provision.WIRE_ID.TX_POWER, type: provision.TYPE.U8, value: 2 },
    ],
  });
  const commands = [];
  const port = createFakePort((full, emit) => {
    if (full === "\r") {
      emit("furble> ");
      return;
    }
    // The console echoes the line without its terminating carriage return.
    const text = full.replace(/\r$/, "");
    commands.push(text);
    emit(
      text +
        "\nprovision: decoded " +
        expected.bytes.length +
        " bytes as base64\n" +
        "provision: wifi_ssid applied\n" +
        "provision: wifi_psk applied\n" +
        "provision: companion_password applied\n" +
        "provision: setting 3 (theme) applied\n" +
        "provision: setting 4 (tx_power) applied\n" +
        "provision: 5 field(s) applied, 0 field(s) deferred\r\nfurble> ",
    );
  });
  serial.ports = [port];
  await dom.element("provSend").click();

  check(commands.length === 1, "the panel sends exactly one command");
  check(commands[0] === expected.command, "the panel collects every enabled field");
  check(serial.requested === 0, "a single granted port needs no second prompt");
  check(dom.element("provSend").disabled === false, "the send button comes back");
  check(port.closed === true, "the panel hands the port back");
  check(
    dom.element("provStatus").textContent === "Applied 5 field(s).",
    "the panel reports the applied count",
  );
  const applied = dom.element("provResult").children.map((child) => child.textContent);
  check(applied.length === 5, "the panel lists every applied field");
  check(applied.includes("applied: wifi_ssid"), "the panel names the SSID field");
  check(applied.includes("applied: setting 3 (theme)"), "the panel names the setting");
  check(
    JSON.stringify(applied).includes("panel-secret") === false,
    "the panel never renders the passphrase",
  );
  check(
    JSON.stringify(applied).includes("panel-companion") === false,
    "the panel never renders the companion password",
  );

  // A firmware rejection is surfaced verbatim and adds no success line.
  const errorPort = createFakePort((full, emit) => {
    if (full === "\r") {
      emit("furble> ");
      return;
    }
    emit(full.replace(/\r$/, "") + "\nerror: provision: duplicate field at byte 12\r\nfurble> ");
  });
  serial.ports = [errorPort];
  await dom.element("provSend").click();
  check(
    dom.element("provStatus").textContent.includes("duplicate field"),
    "the panel shows the firmware rejection",
  );
  check(dom.element("provResult").children.length === 1, "the panel lists the error line");
  check(
    dom.element("provResult").children[0].className === "failure",
    "the error line is marked as a failure",
  );

  // A missing SSID is refused before any port is touched.
  dom.element("provSsid").value = "";
  const untouched = createFakePort(() => {});
  serial.ports = [untouched];
  await dom.element("provSend").click();
  check(
    dom.element("provStatus").textContent.includes("Enter the WiFi SSID"),
    "the panel asks for the SSID before opening the port",
  );
  check(untouched.opened === false, "an empty form opens no port");

  // The finished install state points at the next step.
  dom.element("provSsid").value = "panel-net";
  dom.element("installButton").dispatch("state-changed", { state: "FINISHED" });
  check(
    dom.element("provStatus").textContent.includes("Install finished"),
    "the panel reacts to the finished install",
  );

  // Without Web Serial the panel explains itself instead of failing later.
  const offlineDom = createPanelDom();
  const offlineContext = { document: offlineDom.document, navigator: {}, FurbleProvision: provision };
  vm.createContext(offlineContext);
  vm.runInContext(panelScript(), offlineContext);
  offlineDom.ready();
  check(
    offlineDom.visible("provisionUnsupported") === true,
    "a browser without Web Serial is told so",
  );
}

test("the board MQTT capability matches the compiled profiles", () => {
  check(provision.boardHasMqtt("m5stick-s3") === true, "the StickS3 has MQTT");
  check(provision.boardHasMqtt("m5stack-core2") === true, "the Core2 has MQTT");
  check(provision.boardHasMqtt("waveshare-s3-eth") === true, "the Waveshare has MQTT");
  check(provision.boardHasMqtt("m5stick-c") === false, "the 4 MB StickC has no MQTT");
  check(provision.boardHasMqtt("m5stick-c-plus") === false, "the 4 MB Plus has no MQTT");
  check(provision.boardHasMqtt("m5stack-core") === false, "the 4 MB Core has no MQTT");
});

asyncTests()
  .then(panelTests)
  .catch((error) => {
    failures++;
    process.stderr.write(`  FAIL: the session tests threw: ${error.stack}\n`);
  })
  .finally(() => {
    process.stderr.write(`${checks} checks, ${failures} failure(s)\n`);
    if (failures !== 0) {
      process.exitCode = 1;
    }
  });
