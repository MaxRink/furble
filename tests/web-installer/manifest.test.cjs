// Host test for the installer page's chip-family check.
//
// Run with: node tests/web-installer/manifest.test.cjs
//
// The page's own script is extracted from web-installer/index.html and run in a
// vm against a fake DOM and a fake fetch, so the fail-closed paths are executed
// rather than grepped out of the source. tests/test_web_installer.py runs this
// file from the Python tooling suite.

"use strict";

const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const PAGE_PATH = path.join(__dirname, "..", "..", "web-installer", "index.html");
const PAGE = fs.readFileSync(PAGE_PATH, "utf8");

// --- Runner -----------------------------------------------------------------

let checks = 0;
let failures = 0;

function check(condition, message) {
  checks++;
  if (!condition) {
    failures++;
    process.stderr.write(`  FAIL: ${message}\n`);
  }
}

function equal(actual, expected, message) {
  check(
    actual === expected,
    `${message} (expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)})`,
  );
}

function includes(haystack, needle, message) {
  check(
    typeof haystack === "string" && haystack.includes(needle),
    `${message} (${JSON.stringify(haystack)} does not contain ${JSON.stringify(needle)})`,
  );
}

const asyncTests = [];

function test(name, body) {
  process.stderr.write(`test: ${name}\n`);
  try {
    body();
  } catch (error) {
    failures++;
    process.stderr.write(`  FAIL: ${name} threw: ${error.stack}\n`);
  }
}

function testAsync(name, body) {
  asyncTests.push({ name, body });
}

// --- The page script ---------------------------------------------------------

function pageScript() {
  // The manifest check is the page's only attribute-free inline script. The
  // head loads a module, the provisioning panel loads a file and the BT dump
  // console carries an id, so this match is unique.
  const match = PAGE.match(/<script>\n([\s\S]*?)<\/script>/);
  if (match === null) {
    throw new Error("web-installer/index.html has no plain inline script");
  }
  const script = match[1];
  for (const symbol of ["verifyFamily", "updateManifest", "installActivate"]) {
    if (!script.includes(symbol)) {
      throw new Error(`index.html: the inline script no longer holds ${symbol}`);
    }
  }
  return script;
}

// --- A fake DOM and fetch, enough for the page's two globals -----------------

function element(extra) {
  return Object.assign(
    {
      textContent: "",
      disabled: false,
      checked: false,
      listeners: Object.create(null),
      addEventListener(type, handler) {
        (this.listeners[type] || (this.listeners[type] = [])).push(handler);
      },
      dispatch(type) {
        for (const handler of this.listeners[type] || []) {
          handler({ type, target: this });
        }
      },
    },
    extra,
  );
}

function radio(value, family) {
  const dataset = {};
  if (family !== undefined) {
    dataset.chipFamily = family;
  }
  return element({ value, dataset });
}

// The radios the page actually publishes, read from the markup so a board that
// forgets its data-chip-family fails here as well as in tests/test_web_installer.py.
function publishedRadios() {
  const pattern =
    /<input type="radio" name="type" value="([^"]+)" data-chip-family="([^"]+)"\/>/g;
  const radios = [];
  let match;
  while ((match = pattern.exec(PAGE)) !== null) {
    radios.push(radio(match[1], match[2]));
  }
  return radios;
}

function createPage(radios) {
  const installActivate = element();
  const flashStatus = element();
  const debug = element();
  const button = element({
    manifest: "",
    classList: {
      removed: [],
      remove(name) {
        this.removed.push(name);
      },
    },
  });
  const byId = { installActivate, flashStatus, debug };
  const document = {
    getElementById(id) {
      return byId[id] || null;
    },
    querySelector(selector) {
      if (selector === 'input[name="type"]:checked') {
        return radios.find((candidate) => candidate.checked) || null;
      }
      if (selector === "esp-web-install-button") {
        return button;
      }
      throw new Error(`the page asked for an unexpected selector: ${selector}`);
    },
    querySelectorAll(selector) {
      if (selector === 'input[name="type"]') {
        return radios;
      }
      throw new Error(`the page asked for an unexpected selector: ${selector}`);
    },
  };
  return { document, radios, installActivate, flashStatus, debug, button };
}

function runPage(page, fetch) {
  const context = { document: page.document, console };
  if (fetch !== undefined) {
    context.fetch = fetch;
  }
  vm.createContext(context);
  vm.runInContext(pageScript(), context);
  return context;
}

function selectBoard(page, board) {
  const chosen = page.radios.find((candidate) => candidate.value === board);
  if (chosen === undefined) {
    throw new Error(`no radio for ${board}`);
  }
  for (const candidate of page.radios) {
    candidate.checked = candidate === chosen;
  }
  chosen.dispatch("change");
  return chosen;
}

// Let every promise the page is waiting on settle before asserting.
async function settle() {
  for (let turn = 0; turn < 20; turn++) {
    await Promise.resolve();
  }
}

function singleResponse(response) {
  const calls = [];
  const fetch = async (url, init) => {
    calls.push({ url, init });
    if (typeof response === "function") {
      return response();
    }
    return response;
  };
  return { fetch, calls };
}

function manifest(document) {
  return { ok: true, status: 200, json: async () => document };
}

const ESP32_S3_IMAGE = manifest({
  builds: [{ chipFamily: "ESP32-S3", parts: [] }],
});
const ESP32_IMAGE = manifest({ builds: [{ chipFamily: "ESP32", parts: [] }] });

// --- Synchronous checks ------------------------------------------------------

test("every published radio names its chip family", () => {
  const radios = publishedRadios();
  equal(radios.length, 6, "the page publishes one radio per release board");
  for (const candidate of radios) {
    check(
      typeof candidate.dataset.chipFamily === "string" &&
        candidate.dataset.chipFamily.length > 0,
      `${candidate.value} carries data-chip-family`,
    );
  }
});

// --- The fail-closed paths ---------------------------------------------------

testAsync("a radio without data-chip-family refuses without fetching", async () => {
  const page = createPage([radio("m5stick-c"), radio("m5stick-s3", "ESP32-S3")]);
  const { fetch, calls } = singleResponse(ESP32_IMAGE);
  runPage(page, fetch);
  selectBoard(page, "m5stick-c");
  await settle();
  equal(page.installActivate.disabled, true, "Connect stays disabled");
  includes(page.flashStatus.textContent, "Refusing to flash", "the page refuses");
  includes(
    page.flashStatus.textContent,
    "does not declare a chip family",
    "the refusal names the missing attribute",
  );
  equal(calls.length, 0, "the manifest is never fetched");
});

testAsync("an empty data-chip-family refuses without fetching", async () => {
  const page = createPage([radio("m5stick-s3", "")]);
  const { fetch, calls } = singleResponse(ESP32_S3_IMAGE);
  runPage(page, fetch);
  selectBoard(page, "m5stick-s3");
  await settle();
  equal(page.installActivate.disabled, true, "Connect stays disabled");
  includes(page.flashStatus.textContent, "Refusing to flash", "the page refuses");
  equal(calls.length, 0, "the manifest is never fetched");
});

testAsync("an unreachable manifest keeps Connect disabled", async () => {
  const page = createPage(publishedRadios());
  const { fetch } = singleResponse(() => {
    throw new Error("network down");
  });
  runPage(page, fetch);
  selectBoard(page, "m5stick-s3");
  await settle();
  equal(page.installActivate.disabled, true, "Connect stays disabled");
  equal(
    page.flashStatus.textContent,
    "Refusing to flash: network down.",
    "the page reports the transport failure",
  );
});

testAsync("a manifest that answers 404 keeps Connect disabled", async () => {
  const page = createPage(publishedRadios());
  const { fetch } = singleResponse({ ok: false, status: 404, json: async () => ({}) });
  runPage(page, fetch);
  selectBoard(page, "m5stick-c");
  await settle();
  equal(page.installActivate.disabled, true, "Connect stays disabled");
  includes(
    page.flashStatus.textContent,
    "the manifest request returned 404",
    "the page reports the status",
  );
});

testAsync("a manifest with no builds keeps Connect disabled", async () => {
  const page = createPage(publishedRadios());
  const { fetch } = singleResponse(manifest({ builds: [] }));
  runPage(page, fetch);
  selectBoard(page, "m5stick-s3");
  await settle();
  equal(page.installActivate.disabled, true, "Connect stays disabled");
  includes(page.flashStatus.textContent, "no chip family", "the page names the gap");
});

testAsync("a manifest with two families keeps Connect disabled", async () => {
  const page = createPage(publishedRadios());
  const { fetch } = singleResponse(
    manifest({ builds: [{ chipFamily: "ESP32" }, { chipFamily: "ESP32-S3" }] }),
  );
  runPage(page, fetch);
  selectBoard(page, "m5stick-s3");
  await settle();
  equal(page.installActivate.disabled, true, "Connect stays disabled");
  includes(
    page.flashStatus.textContent,
    "declares ESP32 and ESP32-S3",
    "the page names both families",
  );
});

testAsync("a manifest for the wrong family keeps Connect disabled", async () => {
  const page = createPage(publishedRadios());
  const { fetch } = singleResponse(ESP32_S3_IMAGE);
  runPage(page, fetch);
  selectBoard(page, "m5stick-c");
  await settle();
  equal(page.installActivate.disabled, true, "Connect stays disabled");
  includes(
    page.flashStatus.textContent,
    "declares ESP32-S3, but m5stick-c is ESP32",
    "the page names both the manifest and the board",
  );
});

testAsync("the matching single-family manifest enables Connect", async () => {
  const page = createPage(publishedRadios());
  const { fetch, calls } = singleResponse(ESP32_S3_IMAGE);
  runPage(page, fetch);
  selectBoard(page, "m5stick-s3");
  await settle();
  equal(page.installActivate.disabled, false, "Connect is enabled");
  equal(
    page.flashStatus.textContent,
    "ESP32-S3 image ready for m5stick-s3.",
    "the page reports the verified image",
  );
  equal(calls.length, 1, "one manifest request");
  equal(calls[0].url, "./manifest_m5stick-s3.json", "the published manifest path");
  equal(calls[0].init.cache, "no-store", "the manifest is not cached");
  check(page.button.classList.removed.includes("invisible"), "the button is shown");
});

testAsync("the debug checkbox selects the debug manifest", async () => {
  const page = createPage(publishedRadios());
  const { fetch, calls } = singleResponse(ESP32_IMAGE);
  runPage(page, fetch);
  selectBoard(page, "m5stick-c");
  await settle();
  page.debug.checked = true;
  page.debug.dispatch("change");
  await settle();
  equal(
    calls[calls.length - 1].url,
    "./manifest_m5stick-c-debug.json",
    "the debug manifest path",
  );
  equal(page.installActivate.disabled, false, "Connect is enabled");
});

testAsync("a browser without fetch still enables Connect", async () => {
  // The one deliberate fail-open: without fetch there is no ESP Web Tools
  // either, so refusing here would only disable the button in a browser that
  // cannot flash at all.
  const page = createPage(publishedRadios());
  runPage(page);
  selectBoard(page, "m5stick-s3");
  await settle();
  equal(page.installActivate.disabled, false, "Connect is enabled");
  equal(page.flashStatus.textContent, "", "the page says nothing");
});

testAsync("a stale manifest response cannot re-enable Connect", async () => {
  // m5stick-c is requested first and answers late with a success; m5stick-s3 is
  // selected second and answers immediately with a 404. The failure must stand
  // when the stale success lands, and the status line must not be overwritten.
  const page = createPage(publishedRadios());
  let releaseSlow;
  const slow = new Promise((resolve) => {
    releaseSlow = resolve;
  });
  const fetch = async (url) => {
    if (url === "./manifest_m5stick-c.json") {
      await slow;
      return ESP32_IMAGE;
    }
    return { ok: false, status: 404, json: async () => ({}) };
  };
  runPage(page, fetch);
  selectBoard(page, "m5stick-c");
  await settle();
  selectBoard(page, "m5stick-s3");
  await settle();
  equal(page.installActivate.disabled, true, "the failed selection keeps Connect disabled");
  const refusal = page.flashStatus.textContent;
  includes(refusal, "the manifest request returned 404", "the current board failed");

  releaseSlow();
  await settle();
  equal(
    page.installActivate.disabled,
    true,
    "the stale success does not re-enable Connect",
  );
  equal(
    page.flashStatus.textContent,
    refusal,
    "the stale success does not overwrite the refusal",
  );
});

testAsync("a stale refusal cannot disable the current selection", async () => {
  // The same generation check in the other direction: a late failure for the
  // previous board must not disable Connect for the board that passed.
  const page = createPage(publishedRadios());
  let releaseSlow;
  const slow = new Promise((resolve) => {
    releaseSlow = resolve;
  });
  const fetch = async (url) => {
    if (url === "./manifest_m5stick-c.json") {
      await slow;
      throw new Error("network down");
    }
    return ESP32_S3_IMAGE;
  };
  runPage(page, fetch);
  selectBoard(page, "m5stick-c");
  await settle();
  selectBoard(page, "m5stick-s3");
  await settle();
  equal(page.installActivate.disabled, false, "the current board passed");
  equal(
    page.flashStatus.textContent,
    "ESP32-S3 image ready for m5stick-s3.",
    "the current board's verdict is shown",
  );

  releaseSlow();
  await settle();
  equal(
    page.installActivate.disabled,
    false,
    "the stale refusal does not disable Connect",
  );
  equal(
    page.flashStatus.textContent,
    "ESP32-S3 image ready for m5stick-s3.",
    "the stale refusal does not overwrite the verdict",
  );
});

(async () => {
  for (const { name, body } of asyncTests) {
    process.stderr.write(`test: ${name}\n`);
    try {
      await body();
    } catch (error) {
      failures++;
      process.stderr.write(`  FAIL: ${name} threw: ${error.stack}\n`);
    }
  }
})()
  .catch((error) => {
    failures++;
    process.stderr.write(`  FAIL: the page tests threw: ${error.stack}\n`);
  })
  .finally(() => {
    process.stderr.write(`${checks} checks, ${failures} failure(s)\n`);
    if (failures !== 0) {
      process.exitCode = 1;
    }
  });
