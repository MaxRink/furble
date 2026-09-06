// Host implementation of every double the console command suite needs.
//
// Three groups live here:
//
//   1. The platform model: a std::thread and std::condition_variable model of
//      the FreeRTOS queues, tasks and task run time statistics, plus the small
//      esp_timer, esp_system, esp_heap_caps, esp_pm and esp_log surfaces the
//      console reports from.
//   2. The console transport and dispatcher: a faithful double of the ESP-IDF
//      command table, its help command and its tokenizing esp_console_run(),
//      and a USB-Serial/JTAG receive queue a test types into. The production
//      command handlers run unmodified against these.
//   3. The subsystem doubles: GPS, BtDebug, IR, Feedback, SD, Companion,
//      TimeKeeper, Scan, Platform and the UI request queue. Each records what
//      it was asked to do and serves a test controlled result, so a command's
//      parsing and dispatch run for real and the test asserts the call reached
//      the boundary.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "freertos/FreeRTOS.h"

#include "console_doubles.h"

#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "FurbleCompanion.h"
#include "FurbleFeedback.h"
#include "FurblePlatform.h"
#include "FurbleSD.h"
#include "M5Unified.h"
#include "Scan.h"

// --- FreeRTOS queue model ---------------------------------------------------

struct FurbleHostQueue {
  std::mutex mutex;
  std::condition_variable cond;
  std::deque<std::vector<uint8_t>> items;
  size_t item_size = 0;
  size_t max_len = 0;
};

// --- Host task shutdown -----------------------------------------------------
//
// See furbleHostStopTasks() in freertos/FreeRTOS.h for what this is for.

namespace {

std::atomic<bool> g_StopTasks {false};

// Thrown to unwind a task thread out of the blocking primitive it is parked in.
// xTaskCreate() catches it at the top of the task function.
struct StopTask {};

// Set on the threads this shim creates, and only on those. The main thread also
// calls into the blocking primitives (Control::disconnect() runs there and
// sleeps in vTaskDelay), and it must never be unwound: it owns the shutdown.
thread_local bool g_OnShimTask = false;

std::mutex g_QueuesMutex;
std::vector<QueueHandle_t> g_Queues;

// Unwind the calling task if shutdown has begun. Every call site is a
// suspension point in the production code, so the stack unwinds through
// ordinary RAII: the shim's own unique_lock releases the queue on the way out,
// and no production lock is held across a blocking primitive.
void stopPoint(void) {
  if (g_OnShimTask && g_StopTasks.load()) {
    throw StopTask {};
  }
}

}  // namespace

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
  auto *queue = new FurbleHostQueue();
  queue->item_size = static_cast<size_t>(item_size);
  queue->max_len = static_cast<size_t>(length);
  {
    std::lock_guard<std::mutex> lock(g_QueuesMutex);
    g_Queues.push_back(queue);
  }
  return queue;
}

void vQueueDelete(QueueHandle_t queue) {
  {
    std::lock_guard<std::mutex> lock(g_QueuesMutex);
    g_Queues.erase(std::remove(g_Queues.begin(), g_Queues.end(), queue), g_Queues.end());
  }
  delete queue;
}

static BaseType_t queuePush(QueueHandle_t queue, const void *item, bool front) {
  if (queue == nullptr) {
    return pdFALSE;
  }
  std::vector<uint8_t> copy(queue->item_size);
  std::memcpy(copy.data(), item, queue->item_size);

  std::lock_guard<std::mutex> lock(queue->mutex);
  if (queue->items.size() >= queue->max_len) {
    return pdFALSE;
  }
  if (front) {
    queue->items.push_front(std::move(copy));
  } else {
    queue->items.push_back(std::move(copy));
  }
  queue->cond.notify_one();
  return pdTRUE;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks_to_wait) {
  (void)ticks_to_wait;
  return queuePush(queue, item, false);
}

BaseType_t xQueueSendToFront(QueueHandle_t queue, const void *item, TickType_t ticks_to_wait) {
  (void)ticks_to_wait;
  return queuePush(queue, item, true);
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *buffer, TickType_t ticks_to_wait) {
  if (queue == nullptr) {
    return pdFALSE;
  }
  stopPoint();
  std::unique_lock<std::mutex> lock(queue->mutex);
  if (queue->items.empty()) {
    if (ticks_to_wait == 0) {
      return pdFALSE;
    }
    const auto wait = std::chrono::milliseconds(static_cast<uint32_t>(ticks_to_wait));
    queue->cond.wait_for(lock, wait,
                         [queue] { return !queue->items.empty() || g_StopTasks.load(); });
    stopPoint();
    if (queue->items.empty()) {
      return pdFALSE;
    }
  }
  std::memcpy(buffer, queue->items.front().data(), queue->item_size);
  queue->items.pop_front();
  return pdTRUE;
}

BaseType_t xQueueReset(QueueHandle_t queue) {
  if (queue == nullptr) {
    return pdFALSE;
  }
  std::lock_guard<std::mutex> lock(queue->mutex);
  queue->items.clear();
  return pdTRUE;
}

// --- FreeRTOS task model ----------------------------------------------------

struct FurbleHostTask {
  std::thread thread;
  std::string name;
  UBaseType_t priority = 0;
  UBaseType_t number = 0;
};

namespace {

std::mutex g_TasksMutex;
std::vector<FurbleHostTask *> g_Tasks;
UBaseType_t g_NextTaskNumber = 1;

// The mock BLE layer owns esp_timer_get_time(), so share its clock rather than
// running a second one.
int64_t nowUs(void) {
  return esp_timer_get_time();
}

}  // namespace

BaseType_t xTaskCreate(TaskFunction_t task_code,
                       const char *name,
                       uint32_t stack_depth,
                       void *parameters,
                       UBaseType_t priority,
                       TaskHandle_t *created_task) {
  (void)stack_depth;

  // A task created after furbleHostStopTasks() has copied the task list is
  // never joined, so it would outlive main() exactly as a detached task did.
  std::lock_guard<std::mutex> lock(g_TasksMutex);
  if (g_StopTasks.load()) {
    return pdFAIL;
  }

  auto *task = new FurbleHostTask();
  task->name = (name != nullptr) ? name : "unnamed";
  task->priority = priority;
  task->number = g_NextTaskNumber++;
  g_Tasks.push_back(task);
  // Created under g_TasksMutex, so a concurrent shutdown either sees the task
  // and joins it or is rejected above. Left joinable for that join.
  task->thread = std::thread([task_code, parameters] {
    g_OnShimTask = true;
    try {
      task_code(parameters);
    } catch (const StopTask &) {
      // Host shutdown unwinding a task that is immortal on device.
    }
  });
  if (created_task != nullptr) {
    *created_task = task;
  }
  return pdPASS;
}

void vTaskDelete(TaskHandle_t task) {
  // Production only passes NULL (delete the calling task). The task function
  // returns immediately after, which ends the std::thread.
  (void)task;
}

void vTaskDelay(TickType_t ticks_to_delay) {
  std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<uint32_t>(ticks_to_delay)));
  stopPoint();
}

TickType_t xTaskGetTickCount(void) {
  return static_cast<TickType_t>(nowUs() / 1000);
}

UBaseType_t uxTaskGetNumberOfTasks(void) {
  std::lock_guard<std::mutex> lock(g_TasksMutex);
  return static_cast<UBaseType_t>(g_Tasks.size() + 1 + ConsoleHost::misc().syntheticTasks);
}

UBaseType_t uxTaskGetSystemState(TaskStatus_t *array,
                                 UBaseType_t array_size,
                                 uint32_t *total_run_time) {
  std::lock_guard<std::mutex> lock(g_TasksMutex);
  const size_t count = g_Tasks.size() + 1 + ConsoleHost::misc().syntheticTasks;

  // Mirrors FreeRTOS: the call reports nothing at all when the array cannot
  // hold every task, which is the overflow the console has to detect.
  if (array == nullptr || static_cast<size_t>(array_size) < count) {
    if (total_run_time != nullptr) {
      *total_run_time = static_cast<uint32_t>(nowUs());
    }
    return 0;
  }

  const uint32_t elapsed = static_cast<uint32_t>(nowUs());
  size_t index = 0;
  array[index].xHandle = nullptr;
  array[index].pcTaskName = "main";
  array[index].xTaskNumber = 0;
  array[index].uxCurrentPriority = 1;
  array[index].ulRunTimeCounter = elapsed / 2;
  array[index].usStackHighWaterMark = 512;
  index++;

  for (const auto *task : g_Tasks) {
    array[index].xHandle = const_cast<FurbleHostTask *>(task);
    array[index].pcTaskName = task->name.c_str();
    array[index].xTaskNumber = task->number;
    array[index].uxCurrentPriority = task->priority;
    array[index].ulRunTimeCounter = elapsed / (4 + task->number);
    array[index].usStackHighWaterMark = static_cast<uint32_t>(256 + task->number);
    index++;
  }

  if (total_run_time != nullptr) {
    *total_run_time = elapsed;
  }
  return static_cast<UBaseType_t>(index);
}

// --- esp_timer, esp_system, esp_heap_caps, esp_pm, esp_log ------------------

const char *esp_err_to_name(esp_err_t error) {
  switch (error) {
    case ESP_OK:
      return "ESP_OK";
    case ESP_ERR_NO_MEM:
      return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG:
      return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE:
      return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_NOT_FOUND:
      return "ESP_ERR_NOT_FOUND";
    default:
      return "ESP_FAIL";
  }
}

esp_reset_reason_t esp_reset_reason(void) {
  return static_cast<esp_reset_reason_t>(ConsoleHost::misc().resetReason);
}

uint32_t esp_get_free_heap_size(void) {
  return 123456;
}

uint32_t esp_get_minimum_free_heap_size(void) {
  return 65432;
}

const char *esp_get_idf_version(void) {
  return "v5.4-host";
}

void esp_restart(void) {}

void heap_caps_get_info(multi_heap_info_t *info, uint32_t capabilities) {
  if (info == nullptr) {
    return;
  }
  *info = {};
  info->total_free_bytes = 100000 + capabilities;
  info->largest_free_block = 50000 + capabilities;
  info->minimum_free_bytes = 40000 + capabilities;
}

esp_err_t esp_pm_lock_create(esp_pm_lock_type_t type,
                             int argument,
                             const char *name,
                             esp_pm_lock_handle_t *handle) {
  (void)type;
  (void)argument;
  (void)name;
  if (handle != nullptr) {
    *handle = reinterpret_cast<esp_pm_lock_handle_t>(1);
  }
  return ESP_OK;
}

esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t handle) {
  (void)handle;
  return ESP_OK;
}

esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t handle) {
  (void)handle;
  return ESP_OK;
}

esp_err_t esp_pm_lock_delete(esp_pm_lock_handle_t handle) {
  (void)handle;
  return ESP_OK;
}

esp_err_t esp_pm_configure(const void *config) {
  (void)config;
  return ESP_OK;
}

esp_err_t esp_pm_get_configuration(void *config) {
  (void)config;
  return ESP_OK;
}

void esp_pm_dump_locks(void *stream) {
  (void)stream;
}

void esp_log_level_set(const char *tag, esp_log_level_t level) {
  auto &state = ConsoleHost::misc();
  state.lastLogTag = (tag != nullptr) ? tag : "";
  state.lastLogLevel = static_cast<int>(level);
  state.logLevelSets++;
}

// --- Console transport ------------------------------------------------------

namespace {

std::mutex g_InputMutex;
std::condition_variable g_InputCond;
std::deque<uint8_t> g_Input;

std::string g_CapturePath;

}  // namespace

esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *config) {
  (void)config;
  ConsoleHost::misc().usbDriverInstalls++;
  return ESP_OK;
}

int usb_serial_jtag_read_bytes(void *buffer, uint32_t length, uint32_t ticks_to_wait) {
  if (buffer == nullptr || length == 0) {
    return 0;
  }
  stopPoint();
  std::unique_lock<std::mutex> lock(g_InputMutex);
  if (g_Input.empty()) {
    g_InputCond.wait_for(lock, std::chrono::milliseconds(ticks_to_wait),
                         [] { return !g_Input.empty() || g_StopTasks.load(); });
    stopPoint();
    if (g_Input.empty()) {
      return 0;
    }
  }
  static_cast<uint8_t *>(buffer)[0] = g_Input.front();
  g_Input.pop_front();
  return 1;
}

void usb_serial_jtag_vfs_use_driver(void) {
  ConsoleHost::misc().vfsUseDriverCalls++;
}

int uart_write_bytes(uart_port_t port, const void *source, size_t length) {
  (void)port;
  auto &state = ConsoleHost::uart();
  state.writes.emplace_back(static_cast<const char *>(source), length);
  return state.shortWrite ? static_cast<int>(length) - 1 : static_cast<int>(length);
}

int uart_read_bytes(uart_port_t port, void *buffer, uint32_t length, uint32_t ticks_to_wait) {
  (void)port;
  (void)buffer;
  (void)length;
  std::this_thread::sleep_for(std::chrono::milliseconds(ticks_to_wait));
  return 0;
}

esp_err_t uart_driver_install(uart_port_t port,
                              int rx_buffer_size,
                              int tx_buffer_size,
                              int queue_size,
                              void *queue,
                              int flags) {
  (void)port;
  (void)rx_buffer_size;
  (void)tx_buffer_size;
  (void)queue_size;
  (void)queue;
  (void)flags;
  ConsoleHost::uart().installs++;
  return ESP_OK;
}

// --- esp_console dispatcher -------------------------------------------------

namespace {

std::mutex g_CommandsMutex;
std::vector<esp_console_cmd_t> g_Commands;
std::vector<ConsoleHost::RegisteredCommand> g_CommandRecord;

const esp_console_cmd_t *findCommand(const std::string &name) {
  std::lock_guard<std::mutex> lock(g_CommandsMutex);
  for (const auto &entry : g_Commands) {
    if (name == entry.command) {
      return &entry;
    }
  }
  return nullptr;
}

/**
 * Split a command line the way esp_console_split_argv() does.
 *
 * Whitespace separates arguments and a double quoted run is one argument, so
 * a provisioning blob or a quoted NMEA body arrives as a single argv entry.
 */
std::vector<std::string> splitArgv(const char *line) {
  std::vector<std::string> argv;
  std::string current;
  bool inWord = false;
  bool inQuotes = false;

  for (const char *c = line; *c != '\0'; c++) {
    if (!inQuotes && (*c == ' ' || *c == '\t')) {
      if (inWord) {
        argv.push_back(current);
        current.clear();
        inWord = false;
      }
      continue;
    }
    if (*c == '"') {
      inQuotes = !inQuotes;
      inWord = true;
      continue;
    }
    current.push_back(*c);
    inWord = true;
  }
  if (inWord) {
    argv.push_back(current);
  }
  return argv;
}

int helpCommand(int argc, char **argv) {
  (void)argc;
  (void)argv;
  std::lock_guard<std::mutex> lock(g_CommandsMutex);
  for (const auto &entry : g_Commands) {
    printf("%s  %s\n", entry.command, (entry.help != nullptr) ? entry.help : "");
  }
  return 0;
}

}  // namespace

esp_err_t esp_console_init(const esp_console_config_t *config) {
  (void)config;
  std::lock_guard<std::mutex> lock(g_CommandsMutex);
  g_Commands.clear();
  g_CommandRecord.clear();
  return ESP_OK;
}

esp_err_t esp_console_deinit(void) {
  std::lock_guard<std::mutex> lock(g_CommandsMutex);
  g_Commands.clear();
  g_CommandRecord.clear();
  return ESP_OK;
}

esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd) {
  if (cmd == nullptr || cmd->command == nullptr || cmd->func == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  std::lock_guard<std::mutex> lock(g_CommandsMutex);
  g_Commands.push_back(*cmd);
  g_CommandRecord.push_back({cmd->command, (cmd->help != nullptr) ? cmd->help : ""});
  return ESP_OK;
}

esp_err_t esp_console_register_help_command(void) {
  const esp_console_cmd_t help = {
      "help",  "Print the list of registered commands", nullptr, helpCommand, nullptr, nullptr,
      nullptr,
  };
  return esp_console_cmd_register(&help);
}

esp_err_t esp_console_run(const char *cmdline, int *cmd_ret) {
  if (cmdline == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  const std::vector<std::string> parts = splitArgv(cmdline);
  if (parts.empty()) {
    return ESP_ERR_INVALID_ARG;
  }

  const esp_console_cmd_t *command = findCommand(parts[0]);
  if (command == nullptr) {
    return ESP_ERR_NOT_FOUND;
  }

  std::vector<char *> argv;
  argv.reserve(parts.size() + 1);
  for (const auto &part : parts) {
    argv.push_back(const_cast<char *>(part.c_str()));
  }
  argv.push_back(nullptr);

  const int ret = command->func(static_cast<int>(parts.size()), argv.data());
  if (cmd_ret != nullptr) {
    *cmd_ret = ret;
  }
  return ESP_OK;
}

// --- Subsystem doubles ------------------------------------------------------

M5HostConsole M5;

namespace ConsoleHost {

namespace {

UIState g_UI;
GPSState g_GPS;
UartState g_Uart;
BtDebugState g_BtDebug;
IRState g_IR;
MiscState g_Misc;
TimeState g_Time;

}  // namespace

const std::vector<RegisteredCommand> &commands(void) {
  return g_CommandRecord;
}

std::vector<std::string> commandNames(void) {
  std::vector<std::string> names;
  names.reserve(g_CommandRecord.size());
  for (const auto &entry : g_CommandRecord) {
    names.push_back(entry.name);
  }
  return names;
}

bool hasCommand(const std::string &name) {
  const auto names = commandNames();
  return std::find(names.begin(), names.end(), name) != names.end();
}

UIState &ui(void) {
  return g_UI;
}
GPSState &gps(void) {
  return g_GPS;
}
UartState &uart(void) {
  return g_Uart;
}
BtDebugState &btDebug(void) {
  return g_BtDebug;
}
IRState &ir(void) {
  return g_IR;
}
MiscState &misc(void) {
  return g_Misc;
}
TimeState &time(void) {
  return g_Time;
}

void resetDoubles(void) {
  const size_t installs = g_Misc.usbDriverInstalls;
  const size_t vfsCalls = g_Misc.vfsUseDriverCalls;

  g_UI = UIState();
  g_GPS = GPSState();
  g_Uart = UartState();
  g_BtDebug = BtDebugState();
  g_IR = IRState();
  g_Misc = MiscState();
  g_Time = TimeState();

  // Transport setup happens once at Console::init(), so keep its counters.
  g_Misc.usbDriverInstalls = installs;
  g_Misc.vfsUseDriverCalls = vfsCalls;
}

}  // namespace ConsoleHost

namespace Furble {

std::mutex g_IMUMutex;

void UI::notifyGestureSettingsChanged(void) {
  ConsoleHost::ui().gestureNotifications++;
}

bool UI::sendRequest(Request request, int32_t arg) {
  auto &state = ConsoleHost::ui();
  if (!state.queueAvailable) {
    return false;
  }
  state.requests.push_back({request, arg});
  return true;
}

Scan &Scan::getInstance(void) {
  static Scan instance;
  return instance;
}

bool Scan::isActive(void) const {
  return m_Active;
}

void Scan::setActive(bool active) {
  m_Active = active;
}

GPS &GPS::getInstance(void) {
  static GPS instance;
  return instance;
}

bool GPS::isEnabled(void) const {
  return ConsoleHost::gps().enabled;
}

void GPS::reloadSetting(void) {
  ConsoleHost::gps().reloadSettingCalls++;
}

void GPS::reloadLogSettings(void) {
  ConsoleHost::gps().reloadLogSettingsCalls++;
}

GPS::status_t GPS::getStatusSnapshot(void) const {
  return ConsoleHost::gps().status;
}

GPS::cycle_status_t GPS::getCycleStatusSnapshot(void) const {
  return ConsoleHost::gps().cycle;
}

GPS::receiver_status_t GPS::getReceiverStatus(void) const {
  return ConsoleHost::gps().receiver;
}

GPS::source_t GPS::getSource(void) const {
  return ConsoleHost::gps().source;
}

Furble::GPS::Fix Furble::GPS::getFix(void) const {
  return ConsoleHost::gps().fix;
}

uint32_t Furble::GPS::getHoldLimitMs(void) const {
  return ConsoleHost::gps().holdLimitMs;
}

uint32_t Furble::GPS::getHoldRemainingMs(void) const {
  return ConsoleHost::gps().holdRemainingMs;
}

GPS::receiver_state_t GPS::getReceiverState(void) const {
  return ConsoleHost::gps().receiverState;
}

uint32_t GPS::getDetectedBaud(void) const {
  return ConsoleHost::gps().detectedBaud;
}

const char *GPS::receiverStateName(receiver_state_t state) {
  switch (state) {
    case receiver_state_t::UNKNOWN:
      return "unknown";
    case receiver_state_t::DETECTING:
      return "detecting";
    case receiver_state_t::PRESENT:
      return "present";
    case receiver_state_t::ABSENT:
      return "absent";
  }
  return "unknown";
}

void GPS::setSatelliteCapture(bool capture) {
  ConsoleHost::gps().satCapture = capture;
}

bool GPS::satelliteCaptureEnabled(void) const {
  return ConsoleHost::gps().satCapture;
}

GPS::satellite_report_t GPS::getSatelliteReport(void) {
  return ConsoleHost::gps().satellites;
}

void GPS::pollMonHw(void) {
  ConsoleHost::gps().monHwPolls++;
}

GPS::monhw_report_t GPS::getMonHw(void) {
  return ConsoleHost::gps().monhw;
}

const char *GPS::sourceName(source_t source) {
  switch (source) {
    case SOURCE_NONE:
      return "none";
    case SOURCE_UART:
      return "uart";
    case SOURCE_COMPANION:
      return "companion";
  }
  return "unknown";
}

bool GPS::sendBinary(uint8_t class_id, uint8_t message_id, const std::vector<uint8_t> &payload) {
  auto &state = ConsoleHost::gps();
  std::vector<uint8_t> frame = {class_id, message_id};
  frame.insert(frame.end(), payload.begin(), payload.end());
  state.binaryFrames.push_back(frame);
  return state.binaryResult;
}

bool GPS::sendAidIni(void) {
  auto &state = ConsoleHost::gps();
  state.aidCalls++;
  return state.aidResult;
}

std::vector<GPS::config_status_t> GPS::getConfigStatus(void) const {
  return ConsoleHost::gps().config;
}

const char *GPS::configStateName(config_state_t state) {
  switch (state) {
    case CONFIG_QUEUED:
      return "queued";
    case CONFIG_SENT:
      return "sent";
    case CONFIG_ACKED:
      return "acked";
    case CONFIG_NACKED:
      return "nacked";
    case CONFIG_TIMEOUT:
      return "timeout";
    case CONFIG_FALLBACK:
      return "fallback";
  }
  return "unknown";
}

bool BtDebug::startScan(uint32_t seconds, bool duplicates) {
  auto &state = ConsoleHost::btDebug();
  state.startScanCalls++;
  state.lastScanSeconds = seconds;
  state.lastScanDuplicates = duplicates;
  return state.scanResult;
}

bool BtDebug::stopScan(void) {
  auto &state = ConsoleHost::btDebug();
  state.stopScanCalls++;
  return state.stopScanResult;
}

bool BtDebug::startExplore(const char *address, PairMode mode, bool keep) {
  auto &state = ConsoleHost::btDebug();
  state.startExploreCalls++;
  state.lastExploreAddress = (address != nullptr) ? address : "";
  state.lastPairMode = mode;
  state.lastExploreKeep = keep;
  return state.exploreResult;
}

bool BtDebug::stopExplore(bool keep) {
  auto &state = ConsoleHost::btDebug();
  state.stopExploreCalls++;
  state.lastStopExploreKeep = keep;
  return state.stopExploreResult;
}

bool BtDebug::readExplore(void) {
  auto &state = ConsoleHost::btDebug();
  state.readExploreCalls++;
  return state.readExploreResult;
}

bool BtDebug::pairConfirm(bool accept) {
  auto &state = ConsoleHost::btDebug();
  state.pairConfirmCalls++;
  state.lastPairAccept = accept;
  return state.pairResult;
}

bool BtDebug::pairKey(uint32_t key) {
  auto &state = ConsoleHost::btDebug();
  state.pairKeyCalls++;
  state.lastPairKey = key;
  return state.pairResult;
}

bool BtDebug::isExploreRunning(void) {
  return false;
}

IR &IR::getInstance(void) {
  static IR instance;
  return instance;
}

bool IR::isSupported(void) const {
  return ConsoleHost::ir().supported;
}

void IR::fire(void) {
  auto &state = ConsoleHost::ir();
  state.fires++;
  state.lastHadProtocol = false;
}

void IR::fire(protocol_t protocol) {
  auto &state = ConsoleHost::ir();
  state.fires++;
  state.lastHadProtocol = true;
  state.lastProtocol = protocol;
}

Feedback &Feedback::getInstance(void) {
  static Feedback instance;
  return instance;
}

void Feedback::reload(void) {
  ConsoleHost::misc().feedbackReloads++;
}

CompanionGatt &CompanionGatt::getInstance(void) {
  static CompanionGatt instance;
  return instance;
}

void CompanionGatt::reloadSetting(void) {
  ConsoleHost::misc().companionReloads++;
}

void CompanionGatt::reloadPassword(void) {
  ConsoleHost::misc().companionPasswordReloads++;
}

SD &SD::getInstance(void) {
  static SD instance;
  return instance;
}

bool SD::isSupported(void) const {
  return ConsoleHost::misc().sdSupported;
}

TimeKeeper &TimeKeeper::getInstance(void) {
  static TimeKeeper instance;
  return instance;
}

TimeKeeper::status_t TimeKeeper::status(void) const {
  return ConsoleHost::time().status;
}

void TimeKeeper::flush(void) {
  ConsoleHost::time().flushes++;
}

Platform &Platform::getInstance(void) {
  static Platform instance;
  return instance;
}

void Platform::restart(void) {
  m_Restarts++;
}

void Platform::dumpPMLocks(void) {
  m_DumpPMLocks++;
  printf("pm.locks: host double\n");
}

bool Platform::prepareFlash(void) {
  if (m_PrepareFails) {
    return false;
  }
  m_FlashReady = true;
  return true;
}

bool Platform::cancelFlashPreparation(void) {
  if (m_CancelFails) {
    return false;
  }
  m_FlashReady = false;
  return true;
}

}  // namespace Furble

// --- Capture and line feeding ----------------------------------------------

namespace ConsoleHost {

namespace {

constexpr const char *PROMPT = "furble> ";

bool endsWithPrompt(const std::string &text) {
  const size_t length = std::strlen(PROMPT);
  return (text.size() >= length) && (text.compare(text.size() - length, length, PROMPT) == 0);
}

}  // namespace

void startCapture(const std::string &path) {
  g_CapturePath = path;
  if (freopen(path.c_str(), "w+", stdout) == nullptr) {
    fprintf(stderr, "console suite: could not redirect stdout to %s\n", path.c_str());
    __builtin_abort();
  }
}

long captureOffset(void) {
  fflush(stdout);
  return ftell(stdout);
}

std::string capturedSince(long offset) {
  fflush(stdout);
  std::ifstream in(g_CapturePath, std::ios::binary);
  if (!in) {
    return {};
  }
  in.seekg(offset);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void feedBytes(const std::string &bytes) {
  {
    std::lock_guard<std::mutex> lock(g_InputMutex);
    for (const char byte : bytes) {
      g_Input.push_back(static_cast<uint8_t>(byte));
    }
  }
  g_InputCond.notify_all();
}

bool waitForInputDrained(int timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    {
      std::lock_guard<std::mutex> lock(g_InputMutex);
      if (g_Input.empty()) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  std::lock_guard<std::mutex> lock(g_InputMutex);
  return g_Input.empty();
}

std::string runLine(const std::string &line) {
  const long start = captureOffset();
  feedBytes(line + "\n");

  // The console task prints its prompt once the command has returned, so the
  // prompt is the completion signal. Generous, because 'perf tasks' samples
  // over a full second and 'shutter hold' waits out its own delay.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  std::string out;
  while (std::chrono::steady_clock::now() < deadline) {
    out = capturedSince(start);
    if (endsWithPrompt(out)) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  // Strip the echo of the typed line and the trailing prompt, leaving exactly
  // what the command printed.
  const size_t newline = out.find('\n');
  if (newline != std::string::npos) {
    out = out.substr(newline + 1);
  }
  if (endsWithPrompt(out)) {
    out.erase(out.size() - std::strlen(PROMPT));
  }
  return out;
}

}  // namespace ConsoleHost

// --- Host task shutdown -----------------------------------------------------

void furbleHostStopTasks(void) {
  g_StopTasks.store(true);

  // Wake every primitive a task can be parked in. Each wait predicate also
  // reads the stop flag, so a task cannot miss shutdown between the store above
  // and its own wakeup.
  {
    const std::lock_guard<std::mutex> lock(g_QueuesMutex);
    for (auto *queue : g_Queues) {
      queue->cond.notify_all();
    }
  }
  g_InputCond.notify_all();

  // Copy the task list before joining. The console task reads it under
  // g_TasksMutex to answer "perf tasks", so joining while holding that mutex
  // would deadlock against the thread being joined.
  std::vector<FurbleHostTask *> tasks;
  {
    const std::lock_guard<std::mutex> lock(g_TasksMutex);
    tasks = g_Tasks;
  }
  for (auto *task : tasks) {
    if (task->thread.joinable()) {
      task->thread.join();
    }
  }
}
