#include <cstdint>
#include <iostream>
#include <limits>

#include "FurbleWebUIMemory.h"
#include "FurbleWebUIProtocol.h"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    failures++;
  }
}

}  // namespace

int main() {
  using namespace Furble::WebUIProtocol;

  check(isJSONContentType("application/json"), "plain JSON content type was rejected");
  check(isJSONContentType("application/json; charset=utf-8"), "JSON charset was rejected");
  check(!isJSONContentType("text/plain"), "text/plain was accepted as JSON");
  check(!isJSONContentType("application/jsonx"), "a JSON prefix was accepted");

  check(sameOrigin("furble.local", "https://furble.local"), "same origin was rejected");
  check(sameOrigin("192.0.2.4", ""), "a non-browser client was rejected");
  check(!sameOrigin("furble.local", "http://furble.local"), "an insecure origin was accepted");
  check(!sameOrigin("furble.local", "https://evil.example"), "a cross origin was accepted");

  uint32_t value = 0;
  check(unsignedInteger(62, 255, value) && (value == 62), "a wire id was not decoded");
  check(!unsignedInteger(1.5, UINT32_MAX, value), "a fractional number was narrowed");
  check(!unsignedInteger(-1, UINT32_MAX, value), "a negative number was narrowed");
  check(!unsignedInteger(std::numeric_limits<double>::infinity(), UINT32_MAX, value),
        "infinity was narrowed");
  check(!unsignedInteger(4294967296.0, UINT32_MAX, value), "an overflowing number was narrowed");

  check(holdExpired(1000, 1000, false), "the current elapsed hold did not expire");
  check(!holdExpired(0, 2000, false), "manual release did not invalidate an old hold wake");
  check(!holdExpired(3000, 2000, false), "an old wake released a newer timed hold");
  check(!holdExpired(1000, 2000, true), "an active replacement hold was released");

  using namespace Furble::WebUIMemory;
  check(serverFits(SERVER_INTERNAL_FREE_FLOOR, SERVER_INTERNAL_LARGEST_FLOOR),
        "the exact server memory floor was rejected");
  check(!serverFits(SERVER_INTERNAL_FREE_FLOOR - 1, SERVER_INTERNAL_LARGEST_FLOOR),
        "the server started one byte below the free floor");
  check(!serverFits(SERVER_INTERNAL_FREE_FLOOR, SERVER_INTERNAL_LARGEST_FLOOR - 1),
        "the server started below the largest-block floor");
  check(supervisorFits(SUPERVISOR_INTERNAL_FREE_FLOOR, SUPERVISOR_INTERNAL_LARGEST_FLOOR),
        "the exact supervisor memory floor was rejected");
  check(!supervisorFits(SUPERVISOR_INTERNAL_FREE_FLOOR - 1, SUPERVISOR_INTERNAL_LARGEST_FLOOR),
        "the supervisor started one byte below the free floor");
  check(!supervisorFits(SUPERVISOR_INTERNAL_FREE_FLOOR, SUPERVISOR_INTERNAL_LARGEST_FLOOR - 1),
        "the supervisor started below the largest-block floor");
  check(identityFits(IDENTITY_INTERNAL_FREE_FLOOR, IDENTITY_INTERNAL_LARGEST_FLOOR),
        "the exact identity memory floor was rejected");
  check(!identityFits(IDENTITY_INTERNAL_FREE_FLOOR - 1, IDENTITY_INTERNAL_LARGEST_FLOOR),
        "the identity task started one byte below the free floor");
  check(!identityFits(IDENTITY_INTERNAL_FREE_FLOOR, IDENTITY_INTERNAL_LARGEST_FLOOR - 1),
        "the identity task started below the largest-block floor");
  static_assert(IDENTITY_INTERNAL_FREE_FLOOR == 20864, "identity free floor drifted");
  static_assert(IDENTITY_INTERNAL_LARGEST_FLOOR == 12672, "identity largest floor drifted");
  static_assert(IDENTITY_INTERNAL_LARGEST_FLOOR > IDENTITY_STACK_BYTES,
                "the largest block must hold the identity stack and its overhead");
  // The post-#394 StickS3 pair minus the supervisor still admits a runtime
  // first enable.
  check(identityFits(27351 - SUPERVISOR_STACK_BYTES, 25588 - SUPERVISOR_STACK_BYTES),
        "the post-#394 StickS3 runtime enable was refused");
  static_assert(HTTPD_STACK_BYTES >= 6144,
                "a TLS handshake needs at least 6 KiB of internal stack");
  static_assert(SERVER_INTERNAL_FREE_FLOOR > HTTPD_STACK_BYTES,
                "the free floor must cover more than the httpd stack");

  if (failures != 0) {
    return 1;
  }
  std::cout << "webui_security_test: PASS\n";
  return 0;
}
