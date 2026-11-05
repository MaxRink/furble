// Host coverage for the simulator WebUI admission model.
//
// The model must clear a stale fail-closed state after a later admission
// succeeds, and the query strings must not hand out a pointer into the guarded
// model state.

#include <cstdarg>
#include <cstring>
#include <iostream>
#include <string>

#include "clock.h"
#include "heap_model.h"
#include "webui_memory_model.h"

// The heap model logs through the simulator crash capture hook.
extern "C" void furble_sim_crash_log_capture(const char *, const char *, va_list) {}

namespace Furble::Sim {

// The model only needs the network-up seam from the hardware model.
bool hwWifiConnected() {
  return true;
}

}  // namespace Furble::Sim

namespace {

int g_Failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
}

}  // namespace

int main(void) {
  using namespace Furble::Sim;

  heapSet(60000, 30000);
  heapActivate();
  webuiMemoryConfigure(true, true, true);
  webuiMemoryStart();
  check(std::strcmp(webuiSupervisorState(), "created") == 0,
        "the supervisor starts with enough heap");

  // Force the server gate closed, then release the pressure. The success path
  // must clear supervisorBlocked/serverBlocked/failClosed and the old message.
  heapSet(10000, 2000);
  setClockMillis(1000);
  webuiMemoryTick();
  check(std::strcmp(webuiServerState(), "blocked") == 0, "the server gate blocks low heap");
  check(webuiFailClosed(), "a blocked server reports fail closed");
  const std::string blockedMessage = webuiGateMessage();
  check(!blockedMessage.empty(), "the blocked gate keeps the production sentence");

  heapSet(60000, 30000);
  setClockMillis(12000);
  webuiMemoryTick();
  check(std::strcmp(webuiServerState(), "running") == 0, "the server starts after heap recovers");
  check(!webuiFailClosed(), "a successful admission clears the stale fail-closed flag");
  check(std::strcmp(webuiGateReason(), "none") == 0, "a successful admission clears the reason");
  check(std::strcmp(webuiGateMessage(), "") == 0, "a successful admission clears the message");
  check(webuiGateReason() != nullptr && webuiGateMessage() != nullptr,
        "the query accessors return stable copies");

  if (g_Failures != 0) {
    std::cerr << "sim webui memory model: " << g_Failures << " FAILED\n";
    return 1;
  }
  std::cout << "sim webui memory model: PASS\n";
  return 0;
}
