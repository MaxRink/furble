#include "webui_memory_model.h"
#include "heap_count.h"

#include <cstdio>
#include <mutex>
#include <string>

#include <esp_log.h>

#include "FurbleMemoryPressure.h"
#include "FurbleWebUIMemory.h"
#include "clock.h"
#include "hardware_model.h"
#include "heap_model.h"

namespace {

constexpr const char *LOG_TAG = "webui";
// Production budget retry from FurbleWebUI.cpp. Keeping the value here means a
// scenario can observe the same slow retry without linking the HTTPS server.
constexpr uint32_t BUDGET_RETRY_MS = 10 * 1000;
// Pre-#392 supervisor stack, used only by the gate-disabled regression leg.
constexpr size_t PRE_GATE_SUPERVISOR_STACK_BYTES = 8192;

struct model_state_t {
  std::mutex mutex;
  bool enabled = false;
  bool gateEnabled = true;
  bool credentialsReady = false;
  bool started = false;
  bool supervisorCreated = false;
  bool supervisorBlocked = false;
  bool supervisorAllocFailed = false;
  bool serverRunning = false;
  bool serverBlocked = false;
  bool serverAllocFailed = false;
  bool failClosed = false;
  uint32_t supervisorAttempts = 0;
  uint32_t serverAttempts = 0;
  uint32_t retryAfterMs = 0;
  uint32_t reservedBytes = 0;
  uint64_t supervisorAllocation = 0;
  uint64_t serverAllocation = 0;
  std::string gateReason = "none";
  std::string gateMessage;
};

model_state_t &state(void) {
  static model_state_t instance;
  return instance;
}

void markGateLocked(model_state_t &model,
                    const char *reason,
                    const char *format,
                    size_t freeBytes,
                    size_t largest,
                    size_t freeFloor,
                    size_t largestFloor) {
  char message[192];
  std::snprintf(message, sizeof(message), format, static_cast<unsigned>(freeBytes),
                static_cast<unsigned>(largest), static_cast<unsigned>(freeFloor),
                static_cast<unsigned>(largestFloor));
  model.gateReason = reason;
  model.gateMessage = message;
  model.failClosed = true;
  ESP_LOGE(LOG_TAG, "%s", message);
}

}  // namespace

namespace Furble::Sim {

void webuiMemoryConfigure(bool enabled, bool gateEnabled, bool credentialsReady) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  model_state_t &model = state();
  model.enabled = enabled;
  model.gateEnabled = gateEnabled;
  model.credentialsReady = credentialsReady;
}

void webuiMemoryStart(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  model_state_t &model = state();
  if (model.started) {
    return;
  }
  model.started = true;
  if (!model.enabled || !model.credentialsReady) {
    return;
  }

  model.supervisorAttempts++;
  if (!model.gateEnabled) {
    // Pre-#392 shape: skip the gate and ask the allocator for the old 8 KiB
    // supervisor. On device this is where the boot loop began.
    model.supervisorAllocation =
        heapAllocTagged(PRE_GATE_SUPERVISOR_STACK_BYTES, "webui_supervisor", false, true);
    if (model.supervisorAllocation == 0) {
      model.supervisorAllocFailed = true;
      model.gateReason = "supervisor_task";
      model.failClosed = true;
      ESP_LOGE(LOG_TAG, "Could not create the %u-byte WebUI supervisor",
               static_cast<unsigned>(PRE_GATE_SUPERVISOR_STACK_BYTES));
      return;
    }
    model.reservedBytes += PRE_GATE_SUPERVISOR_STACK_BYTES;
    model.supervisorCreated = true;
    model.supervisorBlocked = false;
    model.failClosed = false;
    model.gateReason = "none";
    model.gateMessage.clear();
    return;
  }

  const auto gate = WebUIMemory::supervisorGate(heapFree(), heapLargest());
  if (!gate.open) {
    model.supervisorBlocked = true;
    markGateLocked(model, "supervisor_memory", WebUIMemory::SUPERVISOR_GATE_LOG, gate.freeBytes,
                   gate.largestBlock, gate.freeFloor, gate.largestFloor);
    return;
  }
  model.supervisorAllocation =
      heapAllocTagged(WebUIMemory::SUPERVISOR_STACK_BYTES, "webui_supervisor", false, true);
  if (model.supervisorAllocation == 0) {
    model.supervisorAllocFailed = true;
    model.gateReason = "supervisor_task";
    model.failClosed = true;
    ESP_LOGE(LOG_TAG, "Could not create the %u-byte WebUI supervisor",
             static_cast<unsigned>(WebUIMemory::SUPERVISOR_STACK_BYTES));
    return;
  }
  model.reservedBytes += WebUIMemory::SUPERVISOR_STACK_BYTES;
  model.supervisorCreated = true;
  model.supervisorBlocked = false;
  model.failClosed = false;
  model.gateReason = "none";
  model.gateMessage.clear();
}

void webuiMemoryTick(void) {
  const Furble::Sim::HostAllocScope hostScope;
  const std::lock_guard<std::mutex> lock(state().mutex);
  model_state_t &model = state();
  if (!model.started || !model.supervisorCreated) {
    return;
  }
  const uint32_t now = clockMillis();

  // Mirrors WebUI::task(): memory pressure pauses the server like a disable.
  const bool paused = MemoryPressure::pausesWebUI(MemoryPressure::current());
  if (!model.enabled || !model.credentialsReady || !hwWifiConnected() || paused) {
    if (model.serverRunning) {
      if (model.serverAllocation != 0) {
        heapFreeTagged(model.serverAllocation);
        model.serverAllocation = 0;
      }
      model.reservedBytes -= WebUIMemory::HTTPD_STACK_BYTES;
      model.serverRunning = false;
    }
    model.retryAfterMs = 0;
    return;
  }
  if (model.serverRunning) {
    return;
  }
  if (now < model.retryAfterMs) {
    return;
  }

  model.serverAttempts++;
  const auto gate = WebUIMemory::serverGate(heapFree(), heapLargest());
  if (!gate.open) {
    model.serverBlocked = true;
    markGateLocked(model, "server_memory", WebUIMemory::SERVER_GATE_LOG, gate.freeBytes,
                   gate.largestBlock, gate.freeFloor, gate.largestFloor);
    model.retryAfterMs = now + BUDGET_RETRY_MS;
    return;
  }
  model.serverAllocation =
      heapAllocTagged(WebUIMemory::HTTPD_STACK_BYTES, "webui_https", false, true);
  if (model.serverAllocation == 0) {
    model.serverAllocFailed = true;
    model.gateReason = "server_task";
    model.failClosed = true;
    ESP_LOGE(LOG_TAG, "Could not start the %u-byte HTTPS task",
             static_cast<unsigned>(WebUIMemory::HTTPD_STACK_BYTES));
    model.retryAfterMs = now + BUDGET_RETRY_MS;
    return;
  }
  model.reservedBytes += WebUIMemory::HTTPD_STACK_BYTES;
  model.serverRunning = true;
  model.serverBlocked = false;
  model.serverAllocFailed = false;
  model.supervisorBlocked = false;
  model.failClosed = false;
  model.gateReason = "none";
  model.gateMessage.clear();
}

const char *webuiSupervisorState(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  const model_state_t &model = state();
  if (!model.enabled || !model.credentialsReady) {
    return "off";
  }
  if (model.supervisorAllocFailed) {
    return "alloc_failed";
  }
  if (model.supervisorBlocked) {
    return "blocked";
  }
  return model.supervisorCreated ? "created" : "pending";
}

const char *webuiServerState(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  const model_state_t &model = state();
  if (!model.supervisorCreated) {
    return "off";
  }
  if (model.serverAllocFailed) {
    return "alloc_failed";
  }
  if (model.serverBlocked) {
    return "blocked";
  }
  return model.serverRunning ? "running" : "pending";
}

const char *webuiGateReason(void) {
  thread_local std::string copy;
  const std::lock_guard<std::mutex> lock(state().mutex);
  copy = state().gateReason;
  return copy.c_str();
}

uint32_t webuiServerAttempts(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().serverAttempts;
}

bool webuiFailClosed(void) {
  const std::lock_guard<std::mutex> lock(state().mutex);
  return state().failClosed;
}

const char *webuiGateMessage(void) {
  thread_local std::string copy;
  const std::lock_guard<std::mutex> lock(state().mutex);
  copy = state().gateMessage;
  return copy.c_str();
}

}  // namespace Furble::Sim
