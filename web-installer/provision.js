// Browser side of the plans/114 flasher provisioning step.
//
// The web installer collects WiFi credentials and a few furble settings,
// encodes one provisioning blob with the production TLV layout, and sends it as
// a single `provision` console line over Web Serial. The firmware parser is the
// authority: every field is validated again on the device, so this file only
// catches a mistake before the port is written and never claims a result on its
// own. Any error line, any timeout and any deferred field fails the step.
//
// The record layout, the tag numbers and the field limits mirror
// lib/furble/protocol/ProvisionTLV.h, and the command line limit mirrors
// src/FurbleConsole.cpp. tests/web-installer/provision.test.cjs pins this
// encoder to tests/web-installer/provision-fixture.json, which
// tests/host/provision_tlv_test.cpp also encodes with the production codec.
//
// Loaded as a classic script so the page needs no module MIME type, and
// exported for the Node test the same way the BT dump script is.

var FurbleProvision = (function () {
  "use strict";

  // lib/furble/protocol/ProvisionTLV.h
  var REQUIRED_TAG_BIT = 0x80;
  var TAG = {
    WIFI_SSID: 0x01,
    WIFI_PSK: 0x02,
    SETTING: 0x03,
    COMPANION_PASSWORD: 0x04,
    MQTT_URI: 0x05,
    MQTT_USERNAME: 0x06,
    MQTT_PASSWORD: 0x07,
    MQTT_BASE_TOPIC: 0x08
  };
  var TYPE = { BOOL: 0, U8: 1, U32: 2, STRING: 3, BLOB: 4 };
  var LIMITS = {
    blobBytes: 4096,
    records: 64,
    ssidBytes: 32,
    pskBytes: 63,
    companionPasswordBytes: 63,
    mqttFieldBytes: 255,
    settingStringBytes: 63
  };

  // src/FurbleSettings.cpp: the stable wire ids this step can write. Theme and
  // TX power have no dedicated provisioning tag, so they travel as SETTING
  // records. WiFi, MQTT and the companion password use their dedicated tags.
  var WIRE_ID = { THEME: 3, TX_POWER: 4 };

  // src/FurbleConsole.cpp: esp_console_run() copies the line into a 256-byte
  // buffer with strlcpy(), so 255 characters is the longest line that survives
  // intact. The console refuses a longer line instead of clipping it, and this
  // encoder refuses it before opening the port.
  var MAX_COMMAND_CHARS = 255;
  var COMMAND_PREFIX = "provision base64:";

  // The one handshake line the debug console prints without a newline.
  var PROMPT = "furble> ";

  // boards/ and platformio.ini: MQTT is compiled only for the 8 MB and 16 MB
  // profiles. A 4 MB board has no MQTT settings, so the page omits them rather
  // than claiming the firmware applied them.
  var BOARD_HAS_MQTT = {
    "m5stack-core2": true,
    "m5stick-s3": true,
    "waveshare-s3-eth": true
  };

  var POLL_MS = 750;

  function sleep(ms) {
    return new Promise(function (resolve) {
      setTimeout(resolve, ms);
    });
  }

  function utf8Bytes(text) {
    return Array.from(new TextEncoder().encode(String(text)));
  }

  function toBase64(bytes) {
    var binary = "";
    for (var index = 0; index < bytes.length; index++) {
      binary += String.fromCharCode(bytes[index]);
    }
    return btoa(binary);
  }

  function checkLength(name, bytes, minimum, maximum) {
    if (bytes.length < minimum || bytes.length > maximum) {
      var expected =
        minimum === 0
          ? "at most " + maximum
          : minimum + " to " + maximum;
      throw new Error(
        name + " must be " + expected + " UTF-8 bytes, got " + bytes.length + ".",
      );
    }
  }

  function record(tag, type, value) {
    if (value.length > 255) {
      throw new Error("A provisioning record cannot carry more than 255 bytes.");
    }
    return [tag | REQUIRED_TAG_BIT, type, value.length].concat(value);
  }

  function settingRecord(wireId, type, value) {
    return record(TAG.SETTING, type, [wireId].concat(value));
  }

  function present(value) {
    return value !== undefined && value !== null;
  }

  // Mirrors Settings::validMQTTString() for the two fields the page can check
  // without the broker: the URI scheme and the base topic shape.
  function validateMqttUri(text) {
    var bytes = utf8Bytes(text);
    checkLength("MQTT URI", bytes, 1, LIMITS.mqttFieldBytes);
    if (!/^mqtts?:\/\/.+/.test(text)) {
      throw new Error("MQTT URI must start with mqtt:// or mqtts://.");
    }
    if (/[\u0000-\u001f]/.test(text) || /\s/.test(text)) {
      throw new Error("MQTT URI cannot contain whitespace or control characters.");
    }
  }

  function validateMqttBase(text) {
    var bytes = utf8Bytes(text);
    checkLength("MQTT base topic", bytes, 1, LIMITS.mqttFieldBytes);
    if (/[\u0000-\u001f]/.test(text)) {
      throw new Error("MQTT base topic cannot contain control characters.");
    }
    var trimmed = text.replace(/^\/+/, "").replace(/\/+$/, "");
    if (trimmed.length > 64) {
      throw new Error("MQTT base topic must be at most 64 characters without leading or trailing slashes.");
    }
    if (/[+#]/.test(trimmed) || trimmed.indexOf("//") >= 0) {
      throw new Error("MQTT base topic cannot contain wildcards or an empty level.");
    }
  }

  /**
   * Encode one provisioning bundle.
   *
   * Field order matches ProvisionTLV::encode(): WiFi SSID, WiFi PSK, companion
   * password, MQTT URI, username, password, base topic, then the SETTING
   * records. A field left undefined is not on the wire. A blank passphrase is
   * on the wire as an empty value, which clears a saved one for an open
   * network.
   */
  function encodeBundle(fields) {
    if (!fields || typeof fields !== "object") {
      throw new Error("No provisioning fields to encode.");
    }
    var output = [];

    function append(recordBytes) {
      output = output.concat(recordBytes);
    }

    if (present(fields.wifiSsid)) {
      var ssid = utf8Bytes(fields.wifiSsid);
      checkLength("SSID", ssid, 1, LIMITS.ssidBytes);
      append(record(TAG.WIFI_SSID, TYPE.STRING, ssid));
    }
    if (present(fields.wifiPsk)) {
      var psk = utf8Bytes(fields.wifiPsk);
      checkLength("Passphrase", psk, 0, LIMITS.pskBytes);
      append(record(TAG.WIFI_PSK, TYPE.STRING, psk));
    }
    if (present(fields.companionPassword)) {
      var companion = utf8Bytes(fields.companionPassword);
      checkLength("Companion password", companion, 1, LIMITS.companionPasswordBytes);
      append(record(TAG.COMPANION_PASSWORD, TYPE.STRING, companion));
    }
    if (present(fields.mqttUri)) {
      validateMqttUri(String(fields.mqttUri));
      append(record(TAG.MQTT_URI, TYPE.STRING, utf8Bytes(fields.mqttUri)));
    }
    if (present(fields.mqttUsername)) {
      var username = utf8Bytes(fields.mqttUsername);
      checkLength("MQTT username", username, 0, LIMITS.mqttFieldBytes);
      append(record(TAG.MQTT_USERNAME, TYPE.STRING, username));
    }
    if (present(fields.mqttPassword)) {
      var mqttPassword = utf8Bytes(fields.mqttPassword);
      checkLength("MQTT password", mqttPassword, 0, LIMITS.mqttFieldBytes);
      append(record(TAG.MQTT_PASSWORD, TYPE.STRING, mqttPassword));
    }
    if (present(fields.mqttBaseTopic)) {
      validateMqttBase(String(fields.mqttBaseTopic));
      append(record(TAG.MQTT_BASE_TOPIC, TYPE.STRING, utf8Bytes(fields.mqttBaseTopic)));
    }

    var settings = fields.settings || [];
    for (var index = 0; index < settings.length; index++) {
      var setting = settings[index];
      if (!setting || typeof setting.wireId !== "number") {
        throw new Error("A setting record is missing its wire id.");
      }
      if (setting.type === TYPE.STRING) {
        var text = utf8Bytes(setting.value);
        checkLength(
          "Setting " + setting.wireId,
          text,
          0,
          LIMITS.settingStringBytes,
        );
        append(settingRecord(setting.wireId, TYPE.STRING, text));
      } else if (setting.type === TYPE.U8) {
        var byte = Number(setting.value);
        if (!Number.isInteger(byte) || byte < 0 || byte > 255) {
          throw new Error("Setting " + setting.wireId + " must be a single byte.");
        }
        append(settingRecord(setting.wireId, TYPE.U8, [byte]));
      } else {
        throw new Error(
          "Setting " + setting.wireId + " has a type this page cannot encode.",
        );
      }
    }

    if (output.length > LIMITS.blobBytes) {
      throw new Error(
        "The provisioning bundle is " + output.length + " bytes, more than the " +
          LIMITS.blobBytes + " byte limit.",
      );
    }
    var records = 0;
    for (var offset = 0; offset < output.length; offset += 3 + output[offset + 2]) {
      records++;
    }
    if (records > LIMITS.records) {
      throw new Error(
        "The provisioning bundle holds " + records + " records, more than the " +
          LIMITS.records + " record limit.",
      );
    }
    return output;
  }

  /**
   * Build the one console line for a bundle.
   *
   * Throws before any port write when a field is out of range or the line is
   * longer than the console can carry. Nothing is ever truncated.
   */
  function buildCommand(fields) {
    var bytes = encodeBundle(fields);
    var base64 = toBase64(bytes);
    var command = COMMAND_PREFIX + base64;
    if (command.length > MAX_COMMAND_CHARS) {
      throw new Error(
        "The provisioning command needs " + command.length + " characters, more " +
          "than the console's " + MAX_COMMAND_CHARS + " character limit. Shorten " +
          "the SSID, the passphrase or the optional fields. Nothing was sent.",
      );
    }
    return { command: command, bytes: bytes, base64: base64 };
  }

  function boardHasMqtt(board) {
    return BOARD_HAS_MQTT[board] === true;
  }

  /**
   * Parse the console output of one `provision` command.
   *
   * Success needs the documented summary line, the decoded byte count matching
   * what was sent, at least one applied field and no deferred field. A timeout,
   * an error line, an unknown shape or a byte count mismatch is a failure.
   */
  function parseProvisionResponse(lines, expectedBytes, options) {
    var opts = options || {};
    var result = {
      ok: false,
      reason: "",
      applied: [],
      deferred: [],
      errors: [],
      unexpected: [],
      decodedBytes: null,
      summary: null,
    };
    var summary = null;

    (lines || []).forEach(function (raw) {
      var line = String(raw).replace(/\r$/, "").trim();
      if (!line) {
        return;
      }
      // The console echoes the typed line. It carries the credentials, so it is
      // dropped here and never reaches the page.
      if (
        line.indexOf("provision base64:") === 0 ||
        line.indexOf("provision hex:") === 0
      ) {
        return;
      }
      if (line.indexOf("error:") === 0 || line.indexOf("failed:") === 0) {
        result.errors.push(line);
        return;
      }
      if (line.indexOf("provision: ") !== 0) {
        result.unexpected.push(line);
        return;
      }
      var body = line.slice("provision: ".length);
      var match = /^decoded (\d+) bytes as (hex|base64)$/.exec(body);
      if (match) {
        result.decodedBytes = Number(match[1]);
        return;
      }
      if (body === "no fields") {
        summary = { applied: 0, deferred: 0 };
        return;
      }
      match = /^(\d+) field\(s\) applied, (\d+) field\(s\) deferred$/.exec(body);
      if (match) {
        summary = { applied: Number(match[1]), deferred: Number(match[2]) };
        return;
      }
      match = /^setting (\d+) \(([^)]*)\) applied$/.exec(body);
      if (match) {
        result.applied.push("setting " + match[1] + " (" + match[2] + ")");
        return;
      }
      match = /^(.+) parsed \(not applied; backend pending #\d+\)$/.exec(body);
      if (match) {
        result.deferred.push(match[1]);
        return;
      }
      match = /^(wifi_ssid|wifi_psk|companion_password) applied$/.exec(body);
      if (match) {
        result.applied.push(match[1]);
        return;
      }
      result.unexpected.push(line);
    });

    if (result.errors.length) {
      result.reason = result.errors[0];
      return result;
    }
    if (opts.timedOut) {
      result.reason =
        "The device did not report a result within " +
        (opts.timeoutMs || 0) +
        " ms. Nothing is confirmed as applied.";
      return result;
    }
    if (summary === null) {
      result.reason =
        "The device answered without a provisioning result. Nothing is confirmed as applied.";
      return result;
    }
    result.summary = summary;
    if (result.decodedBytes === null) {
      result.reason =
        "The device did not report how many bytes it decoded. Nothing is confirmed as applied.";
      return result;
    }
    if (typeof expectedBytes === "number" && result.decodedBytes !== expectedBytes) {
      result.reason =
        "The device decoded " +
        result.decodedBytes +
        " bytes but " +
        expectedBytes +
        " were sent. The console line was clipped or corrupted.";
      return result;
    }
    if (summary.deferred > 0) {
      result.reason =
        "The firmware applied " +
        summary.applied +
        " field(s) and deferred " +
        summary.deferred +
        " because the backend is not in this build.";
      return result;
    }
    if (summary.applied < 1) {
      result.reason = "The device reported no applied fields.";
      return result;
    }
    result.ok = true;
    result.reason = "Applied " + summary.applied + " field(s).";
    return result;
  }

  // A line oriented reader over one open port. The read loop runs for the whole
  // session, so no chunk is ever lost between a timeout and the next read, and
  // the console prompt is the only completion signal.
  function createSession(reader, writer, onLine) {
    var decoder = new TextDecoder();
    var encoder = new TextEncoder();
    var pending = "";
    var lines = [];
    var promptSeen = false;
    var closed = false;
    var promptWaiter = null;

    function settlePrompt(ok) {
      promptSeen = ok;
      var waiter = promptWaiter;
      if (waiter) {
        promptWaiter = null;
        waiter(ok);
      }
    }

    function feed(text) {
      pending += text;
      var index;
      while ((index = pending.indexOf("\n")) >= 0) {
        var line = pending.slice(0, index).replace(/\r$/, "");
        pending = pending.slice(index + 1);
        lines.push(line);
        onLine(line);
      }
      if (
        pending.length >= PROMPT.length &&
        pending.slice(pending.length - PROMPT.length) === PROMPT
      ) {
        pending = pending.slice(0, pending.length - PROMPT.length);
        settlePrompt(true);
      }
    }

    async function readLoop() {
      try {
        while (true) {
          var result = await reader.read();
          if (result.done) {
            break;
          }
          feed(decoder.decode(result.value, { stream: true }));
        }
      } catch (error) {
        // A cancelled or lost port ends the session rather than spinning.
      }
      closed = true;
      settlePrompt(false);
    }

    var loop = readLoop();

    function reset() {
      lines = [];
      pending = "";
      promptSeen = false;
    }

    function waitForPromptOnce(timeoutMs) {
      if (promptSeen) {
        return Promise.resolve(true);
      }
      if (closed) {
        return Promise.resolve(false);
      }
      return new Promise(function (resolve) {
        var timer = setTimeout(function () {
          promptWaiter = null;
          resolve(false);
        }, timeoutMs);
        promptWaiter = function (ok) {
          clearTimeout(timer);
          resolve(ok && !closed);
        };
      });
    }

    // The console reprints its prompt after every carriage return, so a poll
    // asks again instead of assuming the boot banner arrived first.
    async function waitForPrompt(timeoutMs) {
      reset();
      var deadline = Date.now() + timeoutMs;
      while (Date.now() < deadline) {
        await writer.write(encoder.encode("\r"));
        var slice = Math.min(POLL_MS, deadline - Date.now());
        if (await waitForPromptOnce(slice)) {
          return true;
        }
        if (closed) {
          return false;
        }
      }
      return false;
    }

    async function send(command, timeoutMs) {
      reset();
      await writer.write(encoder.encode(command + "\r"));
      var answered = await waitForPromptOnce(timeoutMs);
      return { lines: lines, timedOut: !answered };
    }

    return {
      waitForPrompt: waitForPrompt,
      send: send,
      finished: function () {
        return loop;
      },
    };
  }

  async function openPort(port, options) {
    var attempts = options.openAttempts || 5;
    var delayMs = options.openDelayMs || 400;
    var lastError;
    for (var attempt = 1; attempt <= attempts; attempt++) {
      try {
        await port.open({ baudRate: options.baudRate || 115200 });
        return;
      } catch (error) {
        lastError = error;
        if (attempt < attempts) {
          await sleep(delayMs);
        }
      }
    }
    throw new Error(
      "Could not open the serial port. Close the installer dialog or any other " +
        "serial tool and try again." +
        (lastError && lastError.message ? " (" + lastError.message + ")" : ""),
    );
  }

  /**
   * Open the port, prove the console is there, send one provisioning line and
   * close the port again.
   *
   * The port is always handed back, even on failure, so the installer can run
   * again. A missing prompt means no console, which is what a release build
   * looks like.
   */
  async function runProvision(port, fields, options) {
    var opts = options || {};
    var onLine = opts.onLine || function () {};
    var timeoutMs = opts.timeoutMs || 15000;
    var promptTimeoutMs = opts.promptTimeoutMs || 8000;
    var plan = buildCommand(fields);
    var opened = false;
    var reader;
    var writer;
    var session;
    try {
      await openPort(port, opts);
      opened = true;
      writer = port.writable.getWriter();
      reader = port.readable.getReader();
      session = createSession(reader, writer, onLine);
      if (!(await session.waitForPrompt(promptTimeoutMs))) {
        return {
          ok: false,
          reason:
            "No furble> prompt. Provisioning needs the console, which is only in " +
            "the debug build, so flash the debug build first.",
          applied: [],
          deferred: [],
          errors: [],
          unexpected: [],
          decodedBytes: null,
          summary: null,
        };
      }
      var collected = await session.send(plan.command, timeoutMs);
      return parseProvisionResponse(collected.lines, plan.bytes.length, {
        timedOut: collected.timedOut,
        timeoutMs: timeoutMs,
      });
    } finally {
      if (reader) {
        try {
          await reader.cancel();
        } catch (error) {
          // A cancelled or lost reader is already closed.
        }
        if (session) {
          try {
            // Let the read loop observe the cancel before the lock is released.
            await session.finished();
          } catch (error) {
            // The loop already ended.
          }
        }
        try {
          reader.releaseLock();
        } catch (error) {
          // The lock is gone when the port was lost.
        }
      }
      if (writer) {
        try {
          writer.releaseLock();
        } catch (error) {
          // The lock is gone when the port was lost.
        }
      }
      if (opened) {
        try {
          await port.close();
        } catch (error) {
          // The port may already be closed by a device reset.
        }
      }
    }
  }

  return {
    TAG: TAG,
    TYPE: TYPE,
    LIMITS: LIMITS,
    WIRE_ID: WIRE_ID,
    MAX_COMMAND_CHARS: MAX_COMMAND_CHARS,
    COMMAND_PREFIX: COMMAND_PREFIX,
    PROMPT: PROMPT,
    BOARD_HAS_MQTT: BOARD_HAS_MQTT,
    encodeBundle: encodeBundle,
    buildCommand: buildCommand,
    boardHasMqtt: boardHasMqtt,
    parseProvisionResponse: parseProvisionResponse,
    runProvision: runProvision
  };
})();

if (typeof module !== "undefined" && module.exports) {
  module.exports = FurbleProvision;
}
