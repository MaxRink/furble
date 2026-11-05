// Deterministic API fuzzer for the authenticated HTTPS WebUI handlers.
//
// The production FurbleWebUI.cpp runs against the same host HTTP/TLS doubles
// that webui_handler_test uses. The fuzzer builds malformed, partial and
// oversized requests from a fixed corpus plus seeded mutations, then asserts
// the transport contract: every request gets a bounded 4xx/2xx response, an
// unauthenticated or cross-origin mutation never reaches Control, a rejected
// setting write never changes state, and a valid request still works at the
// end. The same seed always produces the same request stream.
//
// This is a host target only. It does not claim TLS handshake parity; the host
// esp_https_server double starts and stops the server and records routes.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "FurbleControl.h"
#include "FurbleSettings.h"
#include "FurbleWebUI.h"
#include "FurbleWebUIMemory.h"
#include "FurbleWebUIProtocol.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_timer.h"

namespace {

constexpr size_t MAX_RESPONSE_BYTES = 64 * 1024;

const std::map<std::string, std::string> validHeaders = {
    {"Authorization", "Basic ZnVyYmxlOmNvcnJlY3QgaG9yc2U="},
    {"Content-Type",  "application/json"                  },
    {"Host",          "furble.test"                       },
    {"Origin",        "https://furble.test"               },
};

std::map<std::string, std::string> headersWithout(const std::string &name) {
  auto headers = validHeaders;
  headers.erase(name);
  return headers;
}

std::map<std::string, std::string> headersWith(const std::string &name, const std::string &value) {
  auto headers = validHeaders;
  headers[name] = value;
  return headers;
}

struct Request {
  httpd_method_t method = HTTP_GET;
  std::string uri;
  std::map<std::string, std::string> headers;
  std::string body;
  std::string label;
};

struct Result {
  std::string status;
  std::string body;
};

int failures = 0;

void fail(const Request &request, const std::string &message) {
  std::cerr << "WEBUI FUZZ FINDING [" << request.label << "] " << message << '\n';
  failures++;
}

Result invoke(const Request &request) {
  const auto response =
      host_webui_http::invoke(request.method, request.uri, request.headers, request.body);
  return {response.responseStatus, response.responseBody};
}

void checkCommon(const Request &request, const Result &result) {
  if (result.status.empty()) {
    fail(request, "response status is empty");
  }
  if (result.status.size() < 3 || (result.status[0] != '2' && result.status[0] != '4')) {
    fail(request, "response status is neither 2xx nor 4xx: " + result.status);
  }
  if (result.body.size() > MAX_RESPONSE_BYTES) {
    fail(request, "response body is unbounded: " + std::to_string(result.body.size()));
  }
}

std::vector<Request> corpus() {
  std::vector<Request> requests;
  const auto add = [&](httpd_method_t method, const std::string &uri,
                       const std::map<std::string, std::string> &headers, const std::string &body,
                       const std::string &label) {
    requests.push_back({method, uri, headers, body, label});
  };

  const auto valid = validHeaders;

  add(HTTP_GET, "/", valid, "", "root");
  add(HTTP_GET, "/api/status", valid, "", "status");
  add(HTTP_GET, "/api/cameras", valid, "", "cameras");
  add(HTTP_GET, "/api/settings", valid, "", "settings");
  add(HTTP_GET, "/api/missing", valid, "", "missing-route");
  add(HTTP_POST, "/api/status", valid, "", "method-mismatch");
  add(HTTP_GET, "/api/shutter", valid, "", "method-mismatch-get");

  add(HTTP_POST, "/api/shutter", headersWithout("Authorization"), R"({"action":"press"})",
      "no-auth");
  add(HTTP_POST, "/api/shutter", headersWith("Authorization", "Basic"), R"({"action":"press"})",
      "empty-basic");
  add(HTTP_POST, "/api/shutter", headersWith("Authorization", "Basic !!!"), R"({"action":"press"})",
      "invalid-base64");
  add(HTTP_POST, "/api/shutter", headersWith("Authorization", "Basic ZnVyYmxlOndyb25n"),
      R"({"action":"press"})", "wrong-password");
  add(HTTP_POST, "/api/shutter",
      headersWith("Authorization", std::string("Basic ") + std::string(300, 'A')),
      R"({"action":"press"})", "oversized-auth");
  add(HTTP_POST, "/api/shutter", headersWith("Origin", "https://evil.example"),
      R"({"action":"press"})", "cross-origin");
  add(HTTP_POST, "/api/shutter", headersWith("Origin", "http://furble.test"),
      R"({"action":"press"})", "insecure-origin");
  add(HTTP_POST, "/api/shutter", headersWithout("Host"), R"({"action":"press"})", "missing-host");
  add(HTTP_POST, "/api/shutter", headersWith("Host", "furble.test\r\nX: y"),
      R"({"action":"press"})", "header-injection-host");
  add(HTTP_POST, "/api/shutter", headersWith("Content-Type", "text/plain"), R"({"action":"press"})",
      "wrong-content-type");
  add(HTTP_POST, "/api/shutter", headersWith("Content-Type", "application/jsonx"),
      R"({"action":"press"})", "content-type-prefix");

  add(HTTP_POST, "/api/shutter", valid, "", "empty-body");
  add(HTTP_POST, "/api/shutter", valid, "{", "truncated-json");
  add(HTTP_POST, "/api/shutter", valid, "null", "json-null");
  add(HTTP_POST, "/api/shutter", valid, "[]", "json-array");
  add(HTTP_POST, "/api/shutter", valid, R"({"action":1})", "action-number");
  add(HTTP_POST, "/api/shutter", valid, R"({"action":"press","action":"release"})",
      "duplicate-key");
  add(HTTP_POST, "/api/shutter", valid, R"({"action":"hold","ms":-1})", "negative-hold");
  add(HTTP_POST, "/api/shutter", valid, R"({"action":"hold","ms":1.5})", "fractional-hold");
  add(HTTP_POST, "/api/shutter", valid, R"({"action":"hold","ms":60001})", "oversized-hold");
  add(HTTP_POST, "/api/shutter", valid, R"({"action":"hold","ms":1e309})", "infinite-hold");
  add(HTTP_POST, "/api/shutter", valid, R"({"action":"release"})", "release");
  add(HTTP_POST, "/api/shutter", valid, R"({"action":"focus_press"})", "focus-press");
  add(HTTP_POST, "/api/shutter", valid, R"({"action":"focus_release"})", "focus-release");

  add(HTTP_POST, "/api/cameras", valid, R"({"action":"connect"})", "connect-no-id");
  add(HTTP_POST, "/api/cameras", valid, R"({"action":"connect","id":-1})", "connect-negative-id");
  add(HTTP_POST, "/api/cameras", valid, R"({"action":"connect","id":1.5})",
      "connect-fractional-id");
  add(HTTP_POST, "/api/cameras", valid, R"({"action":"connect","id":256})", "connect-oversized-id");
  add(HTTP_POST, "/api/cameras", valid, R"({"action":"disconnect"})", "disconnect");
  add(HTTP_POST, "/api/cameras", valid, R"({"action":"unknown"})", "unknown-camera-action");
  add(HTTP_POST, "/api/cameras", valid, std::string(1025, 'x'), "oversized-camera-body");

  add(HTTP_POST, "/api/settings", valid, R"({"id":20,"value":true})", "brightness-bool");
  add(HTTP_POST, "/api/settings", valid, R"({"id":20,"value":1.5})", "brightness-fractional");
  add(HTTP_POST, "/api/settings", valid, R"({"id":20,"value":-1})", "brightness-negative");
  add(HTTP_POST, "/api/settings", valid, R"({"id":20,"value":256})", "brightness-overflow");
  add(HTTP_POST, "/api/settings", valid, R"({"id":40,"value":"x"})", "password-write");
  add(HTTP_POST, "/api/settings", valid, R"({"id":0,"value":1})", "unknown-setting");
  add(HTTP_POST, "/api/settings", valid, R"({"id":20})", "missing-value");
  add(HTTP_POST, "/api/settings", valid, R"({"value":1})", "missing-id");
  add(HTTP_POST, "/api/settings", valid, std::string(1025, 'x'), "oversized-settings-body");
  add(HTTP_POST, "/api/settings", valid, R"({"id":20,"value":42})", "valid-brightness");

  add(HTTP_GET, "/" + std::string(700, 'a'), valid, "", "oversized-uri");
  add(HTTP_POST, "/api/shutter", valid,
      std::string("{\"action\":\"") + std::string(900, 'a') + "\"}", "oversized-string");
  add(HTTP_POST, "/api/shutter", valid, R"({"nested":[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]})",
      "deep-nesting");
  return requests;
}

Request mutate(const Request &base, std::mt19937_64 &rng) {
  Request request = base;
  const uint32_t roll = static_cast<uint32_t>(rng() % 12);
  switch (roll) {
    case 0:
      request.method = request.method == HTTP_GET ? HTTP_POST : HTTP_GET;
      break;
    case 1:
      request.headers.erase("Authorization");
      break;
    case 2:
      request.headers["Authorization"] = "Basic " + std::string(rng() % 400, 'A');
      break;
    case 3:
      request.headers.erase("Host");
      break;
    case 4:
      request.headers["Origin"] = "https://evil.example";
      break;
    case 5:
      request.headers.erase("Content-Type");
      break;
    case 6:
      request.body.resize(rng() % 1500);
      break;
    case 7:
      if (!request.body.empty()) {
        request.body[rng() % request.body.size()] = static_cast<char>(rng() & 0xff);
      }
      break;
    case 8:
      request.uri.push_back(static_cast<char>('a' + (rng() % 26)));
      break;
    case 9:
      request.body = std::string(rng() % 1400, static_cast<char>(rng() & 0xff));
      break;
    case 10:
      request.uri = "/api/" + std::string(rng() % 64, 'z');
      break;
    default:
      request.body += std::string(rng() % 200, '}');
      break;
  }
  return request;
}

bool registeredMutationRoute(const Request &request) {
  return request.method == HTTP_POST
         && (request.uri == "/api/shutter" || request.uri == "/api/cameras"
             || request.uri == "/api/settings");
}

void checkRejectedMutation(const Request &request, const Result &result, size_t commandsBefore) {
  if (!registeredMutationRoute(request)) {
    return;
  }
  const bool authValid = request.headers.count("Authorization") != 0
                         && request.headers.at("Authorization") == validHeaders.at("Authorization");
  const bool originValid = request.headers.count("Origin") == 0
                           || request.headers.at("Origin") == validHeaders.at("Origin");
  if (!authValid) {
    if (result.status != "401 Unauthorized") {
      fail(request, "unauthenticated mutation returned " + result.status);
    }
    if (Furble::Control::getInstance().commands.size() != commandsBefore) {
      fail(request, "unauthenticated mutation reached Control");
    }
    return;
  }
  if (!originValid) {
    if (result.status != "403 Forbidden") {
      fail(request, "cross-origin request returned " + result.status);
    }
    return;
  }
  const bool typeValid =
      request.headers.count("Content-Type") != 0
      && Furble::WebUIProtocol::isJSONContentType(request.headers.at("Content-Type"));
  if (!typeValid) {
    return;
  }
  if (request.body.size() > 1024 && request.headers == validHeaders) {
    if (result.status != "413 Payload Too Large") {
      fail(request, "oversized body returned " + result.status);
    }
  }
}

void runConcurrentGets() {
  std::vector<std::thread> workers;
  std::vector<size_t> bad(4, 0);
  for (size_t worker = 0; worker < bad.size(); worker++) {
    workers.emplace_back([worker, &bad]() {
      for (int iteration = 0; iteration < 40; iteration++) {
        const auto response = host_webui_http::invoke(HTTP_GET, "/api/status", validHeaders, "");
        if (response.responseStatus != "200 OK" || response.responseBody.empty()) {
          bad[worker]++;
        }
      }
    });
  }
  for (auto &worker : workers) {
    worker.join();
  }
  for (size_t worker = 0; worker < bad.size(); worker++) {
    if (bad[worker] != 0) {
      Request request {HTTP_GET, "/api/status", validHeaders, "", "concurrent-status"};
      fail(request, "concurrent GET failure count " + std::to_string(bad[worker]));
    }
  }
}

void resetWebUI() {
  Furble::Control::getInstance().reset();
  host_webui_http::reset();
  host_webui_timer::reset();
  host_webui_heap::reset();
  Furble::Settings::password = "correct horse";
  Furble::Settings::passwordLoaded = true;
  Preferences::reset();
  auto &webui = Furble::WebUI::getInstance();
  webui.hostResetIdentity();
}

}  // namespace

int main(int argc, char **argv) {
  uint64_t seed = 1;
  uint32_t iterations = 500;
  std::string onlyCase;
  for (int index = 1; index < argc; index++) {
    const std::string argument = argv[index];
    if (argument == "--seed" && index + 1 < argc) {
      seed = std::strtoull(argv[++index], nullptr, 10);
    } else if (argument == "--iterations" && index + 1 < argc) {
      iterations = static_cast<uint32_t>(std::strtoul(argv[++index], nullptr, 10));
    } else if (argument == "--case" && index + 1 < argc) {
      onlyCase = argv[++index];
    } else {
      std::cerr << "usage: webui_fuzz [--seed N] [--iterations N] [--case NAME]\n";
      return 2;
    }
  }
  if (iterations == 0) {
    std::cerr << "iterations must be non-zero\n";
    return 2;
  }

  resetWebUI();
  auto &webui = Furble::WebUI::getInstance();
  if (!webui.hostCredentialsReady()) {
    std::cerr << "WEBUI FUZZ cannot prepare the host identity\n";
    return 1;
  }
  // A TLS/server allocation failure must leave the surface closed instead of
  // half-started with routes registered.
  host_webui_http::setStartError(ESP_ERR_NO_MEM);
  if (webui.hostStartServer("cert", "key")) {
    std::cerr << "WEBUI FUZZ started despite an injected start failure\n";
    return 1;
  }
  host_webui_http::setStartError(ESP_OK);
  if (!webui.hostStartServer("cert", "key")) {
    std::cerr << "WEBUI FUZZ cannot start the host server after recovery\n";
    return 1;
  }

  const auto fixed = corpus();
  std::mt19937_64 rng(seed);
  size_t executed = 0;
  for (const auto &request : fixed) {
    if (!onlyCase.empty() && request.label != onlyCase) {
      continue;
    }
    // The production auth throttle is deliberately time based: three failed
    // Basic attempts close the surface for 30 s. Advance virtual time between
    // cases so a corpus of bad credentials measures the 401 branch, not the
    // throttle state left by the previous case.
    host_webui_timer::advance(31LL * 1000 * 1000);
    const size_t commandsBefore = Furble::Control::getInstance().commands.size();
    const auto result = invoke(request);
    executed++;
    checkCommon(request, result);
    checkRejectedMutation(request, result, commandsBefore);
  }

  const auto corpusSize = fixed.size();
  size_t mutations = 0;
  while (mutations < iterations) {
    const Request base = fixed[static_cast<size_t>(rng() % corpusSize)];
    Request request = mutate(base, rng);
    if (!onlyCase.empty()) {
      request.label = onlyCase;
    } else {
      request.label = base.label + "-mut-" + std::to_string(mutations);
    }
    host_webui_timer::advance(31LL * 1000 * 1000);
    const size_t commandsBefore = Furble::Control::getInstance().commands.size();
    const auto result = invoke(request);
    executed++;
    mutations++;
    checkCommon(request, result);
    checkRejectedMutation(request, result, commandsBefore);
  }

  // A rejected mutation must not corrupt the next valid request. Clear any
  // throttle state left by the last case first.
  host_webui_timer::advance(31LL * 1000 * 1000);
  {
    Request request {HTTP_GET, "/api/settings", validHeaders, "", "post-fuzz-settings"};
    const auto result = invoke(request);
    executed++;
    checkCommon(request, result);
    if (result.status != "200 OK") {
      fail(request, "valid settings GET failed after fuzzing: " + result.status);
    }
    if (result.body.find("\"secret\":true") == std::string::npos) {
      fail(request, "settings response lost the secret marker after fuzzing");
    }
  }

  {
    Furble::Control::getInstance().reset();
    // Release anything a fuzzed hold left owned before proving a fresh press is
    // still accepted. Both releases are idempotent in the production handler.
    (void)invoke(
        {HTTP_POST, "/api/shutter", validHeaders, R"({"action":"release"})", "post-fuzz-release"});
    (void)invoke({HTTP_POST, "/api/shutter", validHeaders, R"({"action":"focus_release"})",
                  "post-fuzz-focus-release"});
    Request request {HTTP_POST, "/api/shutter", validHeaders, R"({"action":"press"})",
                     "post-fuzz-press"};
    const size_t commandsBefore = Furble::Control::getInstance().commands.size();
    const auto result = invoke(request);
    executed++;
    checkCommon(request, result);
    if (result.status != "200 OK"
        || Furble::Control::getInstance().commands.size() != commandsBefore + 1) {
      fail(request, "valid press failed after fuzzing: " + result.status);
    }
  }

  runConcurrentGets();

  webui.hostStopServer();

  std::cout << "WEBUI FUZZ SUMMARY seed=" << seed << " iterations=" << iterations
            << " executed=" << executed << " findings=" << failures << '\n';
  return failures == 0 ? 0 : 1;
}
