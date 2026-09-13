#include <cstdint>
#include <iostream>
#include <limits>

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

  if (failures != 0) {
    return 1;
  }
  std::cout << "webui_security_test: PASS\n";
  return 0;
}
