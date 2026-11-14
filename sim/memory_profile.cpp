#include "memory_profile.h"

#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#include "FurbleCrashRecord.h"
#include "FurbleMQTTMemory.h"
#include "FurbleUIMemory.h"
#include "clock.h"
#include "hardware_model.h"
#include "heap_count.h"
#include "heap_model.h"

namespace {

struct component_t {
  const char *name;
  size_t bytes;
  const char *region;
  const char *source;
};

struct profile_budget_t {
  const char *profile;
  size_t internalTotal;
  size_t psramTotal;
  size_t flashKb;
  size_t mainStack;
  size_t companionStack;
  size_t mqttOwner;
  size_t mqttWorker;
  size_t irStack;
  size_t drawBuffer;
  bool singleDrawBuffer;
  size_t mbedtls;
  size_t psramReserve;
  const char *mcu;
};

const profile_budget_t BUDGETS[] = {
    {"stickc",           532480,     0,               4096,  16384, 4096, 6144,  6144, 4096, 80 * 5 * 2,   true,  20480, 0,     "esp32"  },
    {"stickc-plus",      532480,     0,               4096,  16384, 4096, 6144,  6144, 4096, 135 * 8 * 2,  true,  20480, 0,
     "esp32"                                                                                                                             },
    {"stickc-plus2",     532480,     0,               4096,  16384, 4096, 6144,  6144, 4096, 135 * 8 * 2,  true,  20480, 0,
     "esp32"                                                                                                                             },
    {"sticks3",          512 * 1024, 8 * 1024 * 1024, 8192,  12288, 3072, 10240, 3072, 4096, 135 * 8 * 2,
     true,                                                                                                        0,     32768, "esp32s3"},
    {"core",             532480,     0,               4096,  16384, 4096, 6144,  6144, 4096, 320 * 16 * 2, false, 20480, 0,
     "esp32"                                                                                                                             },
    {"core2",            532480,     8 * 1024 * 1024, 16384, 16384, 4096, 10240, 3072, 4096, 320 * 16 * 2, false,
     0,                                                                                                                  32768, "esp32"  },
    {"tough",            532480,     8 * 1024 * 1024, 16384, 16384, 4096, 10240, 3072, 4096, 320 * 16 * 2, false,
     0,                                                                                                                  32768, "esp32"  },
    {"headless-s3",      512 * 1024, 8 * 1024 * 1024, 8192,  16384, 3072, 10240, 4096, 4096, 0,            false, 0,
     32768,                                                                                                                     "esp32s3"},
    {"waveshare-s3-eth", 512 * 1024, 8 * 1024 * 1024, 16384, 16384, 3072, 10240, 4096, 4096, 0,
     false,                                                                                                       0,     32768, "esp32s3"},
};

const profile_budget_t &budgetFor(const std::string &profile) {
  for (const auto &budget : BUDGETS) {
    if (profile == budget.profile) {
      return budget;
    }
  }
  return BUDGETS[3];
}

std::vector<component_t> componentsFor(const profile_budget_t &budget) {
  // Every PSRAM board sets CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL, which
  // moves both NimBLE pools to PSRAM. tools/derive_sim_heap.py drops them from
  // the internal boot heap on the same condition.
  const char *nimble = budget.psramTotal > 0 ? "psram" : "internal";
  return {
      {"main_stack",             budget.mainStack,                                      "internal",     "CONFIG_ESP_MAIN_TASK_STACK_SIZE"              },
      {"event_task",             2304,                                                  "internal",     "CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE"      },
      {"timer_task",             3584,                                                  "internal",     "CONFIG_ESP_TIMER_TASK_STACK_SIZE"             },
      {"tcpip_task",             3072,                                                  "internal",     "CONFIG_LWIP_TCPIP_TASK_STACK_SIZE"            },
      {"nimble_host",            16384,                                                 "internal",     "CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE"        },
      {"nimble_msys",            12 * 256 + 24 * 320,                                   nimble,         "CONFIG_BT_NIMBLE_MSYS_1/2"                    },
      {"nimble_transport",       24 * 255 + (30 + 8) * 70,                              nimble,         "CONFIG_BT_NIMBLE_TRANSPORT_ACL/EVT"           },
      {"control_task",           8192,                                                  "internal",     "src/main.cpp xTaskCreate control"             },
      {"gps_task",               4096,                                                  "internal",     "src/FurbleGPS.cpp xTaskCreate"                },
      {"ir_task",                budget.irStack,                                        "psram",        "src/FurbleIR.cpp after PR 394"                },
      {"companion_task",         budget.companionStack,                                 "internal",     "src/FurbleCompanion.cpp xTaskCreate"          },
      {"webui_supervisor",       3584,                                                  "internal",     "FurbleWebUIMemory::SUPERVISOR_STACK_BYTES"    },
      {"webui_tls",              12288,                                                 "internal",     "FurbleWebUI::IDENTITY_STACK_BYTES"            },
      {"webui_httpd",            8192,                                                  "internal",     "FurbleWebUIMemory::HTTPD_STACK_BYTES"         },
      {"mqtt_owner",             budget.mqttOwner,                                      "internal",     "release-gate MQTT owner stack"                },
      {"mqtt_worker",            budget.mqttWorker,                                     "internal",     "release-gate MQTT worker stack"               },
      {"mqtt_queue",             Furble::MQTTMemory::EVENT_QUEUE_BYTES,
       budget.psramTotal > 0 ? "psram" : "internal",                                                    "FurbleMQTTMemory::EVENT_QUEUE_BYTES"          },
      {"mqtt_event_copies",      Furble::MQTTMemory::EVENT_COPIES_BYTES,
       budget.psramTotal > 0 ? "psram" : "internal",                                                    "FurbleMQTTMemory::EVENT_COPIES_BYTES"         },
      {"mqtt_camera",            5120,                                                  "internal",     "CameraList::savedSnapshot on connect"         },
      {"console_task",           6144,                                                  "internal",     "src/FurbleConsole.cpp after PR 394"           },
      {"crash_record",           sizeof(Furble::CrashRecord::record_t),                 "rtc",          "include/FurbleCrashRecord.h"                  },
      {"lvgl_draw",              (budget.singleDrawBuffer ? 1 : 2) * budget.drawBuffer, "internal_dma",
       "include/FurbleUIMemory.h"                                                                                                                      },
      {"wifi_static_rx",         16000,                                                 "internal",     "CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM inferred"},
      {"mbedtls_internal",       budget.mbedtls,                                        "internal",     "CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC"            },
      {"psram_internal_reserve", budget.psramReserve,                                   "internal",
       "CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL"                                                                                                         },
  };
}

std::string jsonEscape(const std::string &value) {
  std::string result;
  for (char character : value) {
    if (character == '"' || character == '\\') {
      result.push_back('\\');
    }
    result.push_back(character);
  }
  return result;
}

std::string staticMapJson(void) {
  const profile_budget_t &budget = budgetFor(Furble::Sim::hwProfile());
  std::ostringstream json;
  json << "{\"profile\":\"" << jsonEscape(Furble::Sim::hwProfile()) << "\",\"mcu\":\"" << budget.mcu
       << "\",\"internal_total\":" << budget.internalTotal
       << ",\"psram_total\":" << budget.psramTotal << ",\"flash_kb\":" << budget.flashKb
       << ",\"components\":[";
  bool first = true;
  for (const auto &component : componentsFor(budget)) {
    if (!first) {
      json << ",";
    }
    first = false;
    json << "{\"name\":\"" << component.name << "\",\"bytes\":" << component.bytes
         << ",\"region\":\"" << component.region << "\",\"source\":\""
         << jsonEscape(component.source) << "\"}";
  }
  json << "]}";
  return json.str();
}

std::string heapJson(void) {
  std::ostringstream json;
  const size_t freeBytes = Furble::Sim::heapFree();
  const size_t largest = Furble::Sim::heapLargest();
  json << "{\"free\":" << freeBytes << ",\"largest\":" << largest
       << ",\"minimum\":" << Furble::Sim::heapMinimumFree()
       << ",\"minimum_largest\":" << Furble::Sim::heapMinimumLargest()
       << ",\"alloc_failures\":" << Furble::Sim::heapAllocationFailures()
       << ",\"allocations\":" << Furble::Sim::heapAllocationCount()
       << ",\"leaks\":" << Furble::Sim::heapLeakCount()
       << ",\"psram_free\":" << Furble::Sim::heapPsramFree()
       << ",\"psram_largest\":" << Furble::Sim::heapPsramLargest() << ",\"fragmentation_permille\":"
       << (freeBytes == 0 ? 0 : (freeBytes - largest) * 1000 / freeBytes) << "}";
  return json.str();
}

}  // namespace

namespace Furble::Sim {

void memoryProfileTick(void) {
  const Furble::Sim::HostAllocScope hostScope;
  heapSampleTimeline();
}

bool memoryDump(const std::string &path, const std::string &scenario) {
  const HostAllocScope scope;
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    return false;
  }
  out << "{\"scenario\":\"" << jsonEscape(scenario) << "\",\"clock_ms\":" << clockMillis()
      << ",\"static\":" << staticMapJson() << ",\"heap\":" << heapJson() << ",\"owners\":\""
      << jsonEscape(heapOwnerReport()) << "\",\"leaks\":\"" << jsonEscape(heapLeakReport())
      << "\",\"tasks\":\"" << jsonEscape(schedulerTaskReport())
      << "\",\"timeline\":" << heapTimelineJson() << "}\n";
  return static_cast<bool>(out);
}

std::string memoryQuery(const std::string &key) {
  const profile_budget_t &budget = budgetFor(hwProfile());
  if (key == "mem.profile") {
    return hwProfile();
  }
  if (key == "mem.internal_total") {
    return std::to_string(budget.internalTotal);
  }
  if (key == "mem.psram_total") {
    return std::to_string(budget.psramTotal);
  }
  if (key == "mem.flash_kb") {
    return std::to_string(budget.flashKb);
  }
  if (key == "mem.leaks") {
    return std::to_string(heapLeakCount());
  }
  if (key == "mem.leak_report") {
    return heapLeakReport();
  }
  if (key == "mem.owners") {
    return heapOwnerReport();
  }
  if (key == "mem.timeline_samples") {
    return std::to_string(heapTimelineCount());
  }
  if (key == "mem.high_water_free") {
    return std::to_string(heapMinimumFree());
  }
  if (key == "mem.high_water_largest") {
    return std::to_string(heapMinimumLargest());
  }
  if (key == "mem.allocations") {
    return std::to_string(heapAllocationCount());
  }
  if (key == "mem.tasks") {
    return schedulerTaskReport();
  }
  if (key.compare(0, 14, "mem.component.") == 0) {
    const std::string name = key.substr(14);
    for (const auto &component : componentsFor(budget)) {
      if (name == component.name) {
        return std::to_string(component.bytes);
      }
    }
  }
  return "";
}

}  // namespace Furble::Sim
