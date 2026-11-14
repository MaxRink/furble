// Host coverage for the simulator WebUI admission model.
//
// The model must clear a stale fail-closed state after a later admission
// succeeds, and the query strings must not hand out a pointer into the guarded
// model state.

#include <cstdarg>
#include <cstring>
#include <iostream>
#include <string>

#include "FurbleMemoryPressure.h"
#include "FurbleWebUIMemory.h"
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
int g_EscalateHookCalls = 0;

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

  // Exact allocation failure, then a successful retry. The retry must clear
  // serverAllocFailed or the state accessor keeps reporting alloc_failed while
  // the server is actually running.
  webuiMemoryConfigure(false, true, true);
  webuiMemoryTick();
  check(std::strcmp(webuiServerState(), "pending") == 0, "disabling did not stop the server");
  webuiMemoryConfigure(true, true, true);
  heapSet(60000, 30000);
  heapSetAllocFailureOnce(Furble::WebUIMemory::HTTPD_STACK_BYTES, "webui_https");
  setClockMillis(30000);
  webuiMemoryTick();
  check(std::strcmp(webuiServerState(), "alloc_failed") == 0,
        "the forced allocation failure did not report alloc_failed");
  setClockMillis(41000);
  webuiMemoryTick();
  check(std::strcmp(webuiServerState(), "running") == 0,
        "the successful retry did not clear serverAllocFailed");
  check(!webuiFailClosed(), "the successful retry kept the stale fail-closed flag");

  // Connect admission runs the production governor against the modelled
  // heap. The escalate hook, which wakes the WebUI supervisor on hardware,
  // fires only when admit() raises the level.
  {
    namespace Pressure = Furble::MemoryPressure;
    Pressure::setEscalateHook([]() { g_EscalateHookCalls++; });
    heapSet(60000, 30000);
    Pressure::admit(Pressure::CONNECT_TARGET_BYTES);
    check(g_EscalateHookCalls == 0, "admission with headroom fired the escalate hook");
    check(Pressure::current() == Pressure::NORMAL, "admission with headroom changed the level");
    heapSet(14135, 12372);
    Pressure::admit(Pressure::CONNECT_TARGET_BYTES);
    check(g_EscalateHookCalls == 1, "a StickS3 WebUI connect did not fire the escalate hook");
    check(Pressure::current() == Pressure::AMBER, "a StickS3 WebUI connect did not enter AMBER");
    Pressure::admit(Pressure::CONNECT_TARGET_BYTES);
    check(g_EscalateHookCalls == 1, "a second admission at the same level fired the hook");
    heapSet(10000, 9000);
    Pressure::admit(Pressure::CONNECT_TARGET_BYTES);
    check(g_EscalateHookCalls == 2, "a rise from AMBER to RED did not fire the hook");
    check(Pressure::current() == Pressure::RED, "a connect that reaches RED did not enter RED");
    Pressure::setEscalateHook(nullptr);
  }

  if (g_Failures != 0) {
    std::cerr << "sim webui memory model: " << g_Failures << " FAILED\n";
    return 1;
  }
  std::cout << "sim webui memory model: PASS\n";
  return 0;
}
