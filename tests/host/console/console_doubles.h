// Test control surface for the host console doubles.
//
// Everything the console suite needs to drive src/FurbleConsole.cpp lives here:
// the registered command table the production init() built, a typed record of
// every call each double received, and the transport plus stdout capture that
// let a test type a line at the real console task and read what it printed.
#ifndef FURBLE_HOST_CONSOLE_DOUBLES_H
#define FURBLE_HOST_CONSOLE_DOUBLES_H

#include <cstdint>
#include <string>
#include <vector>

#include "FurbleBtDebug.h"
#include "FurbleGPS.h"
#include "FurbleIR.h"
#include "FurbleTimeKeeper.h"
#include "FurbleUI.h"

namespace ConsoleHost {

/** One entry of the table esp_console_cmd_register() captured at init(). */
struct RegisteredCommand {
  std::string name;
  std::string help;
};

const std::vector<RegisteredCommand> &commands(void);
std::vector<std::string> commandNames(void);
bool hasCommand(const std::string &name);

/** A queued UI task operation, recorded instead of executed. */
struct UIRequest {
  Furble::UI::Request request;
  int32_t arg;
};

struct UIState {
  std::vector<UIRequest> requests;
  bool queueAvailable = true;

  /**
   * Outcome token the UI task double answers a request with.
   *
   * The real handler ends every workflow answer with 'result: <token>' and the
   * console takes it as its exit status, so the double serves one too. Null
   * models a UI task which has not answered yet.
   */
  const char *answer = "ok";

  /**
   * Line the double prints ahead of the token, and the switch for printing.
   *
   * Null prints nothing at all, which is what the rest of the suite wants: it
   * asserts on what the command printed, not on the UI task's half of the
   * answer. A test which cares about the wait sets both this and a delay.
   */
  const char *answerLine = nullptr;

  /**
   * Milliseconds the double takes to answer, modelling the real UI task.
   *
   * Zero answers inside sendRequest(). Anything else answers from another
   * thread, so a verb which does not wait returns before the answer is
   * printed and before its token exists.
   */
  int answerDelayMs = 0;
};

UIState &ui(void);

/** Join a delayed UI answer, so nothing prints into the next capture. */
void joinUIAnswer(void);

struct GPSState {
  bool enabled = true;
  Furble::GPS::status_t status = {};
  Furble::GPS::cycle_status_t cycle = {};
  Furble::GPS::receiver_status_t receiver = {};
  Furble::GPS::source_t source = Furble::GPS::SOURCE_NONE;
  std::vector<Furble::GPS::config_status_t> config;
  bool binaryResult = true;
  bool aidResult = true;
  size_t reloadSettingCalls = 0;
  size_t reloadLogSettingsCalls = 0;
  size_t aidCalls = 0;
  std::vector<std::vector<uint8_t>> binaryFrames;
};

GPSState &gps(void);

struct UartState {
  std::vector<std::string> writes;
  /** Report a short write, which is the 'uart write failed' branch. */
  bool shortWrite = false;
  size_t installs = 0;
};

UartState &uart(void);

struct BtDebugState {
  bool scanResult = true;
  bool stopScanResult = true;
  bool exploreResult = true;
  bool stopExploreResult = true;
  bool readExploreResult = true;
  bool pairResult = true;

  uint32_t lastScanSeconds = 0;
  bool lastScanDuplicates = false;
  std::string lastExploreAddress;
  Furble::BtDebug::PairMode lastPairMode = Furble::BtDebug::PairMode::NONE;
  bool lastExploreKeep = false;
  bool lastStopExploreKeep = false;
  bool lastPairAccept = false;
  uint32_t lastPairKey = 0;

  size_t startScanCalls = 0;
  size_t stopScanCalls = 0;
  size_t startExploreCalls = 0;
  size_t stopExploreCalls = 0;
  size_t readExploreCalls = 0;
  size_t pairConfirmCalls = 0;
  size_t pairKeyCalls = 0;
};

BtDebugState &btDebug(void);

struct IRState {
  bool supported = true;
  size_t fires = 0;
  bool lastHadProtocol = false;
  Furble::IR::protocol_t lastProtocol = Furble::IR::protocol_t::NIKON;
};

IRState &ir(void);

struct MiscState {
  size_t feedbackReloads = 0;
  size_t companionReloads = 0;
  bool sdSupported = true;
  size_t usbDriverInstalls = 0;
  size_t vfsUseDriverCalls = 0;
  std::string lastLogTag;
  int lastLogLevel = -1;
  size_t logLevelSets = 0;
  /**
   * Pad the reported task count so the snapshot overflows.
   *
   * uxTaskGetSystemState() returns zero when the caller's array is too small,
   * which is the branch 'perf tasks' reports as an error. Zero disables the
   * padding.
   */
  size_t syntheticTasks = 0;
  /** Last reset cause reported to the 'status' command. */
  int resetReason = 1;
};

MiscState &misc(void);

struct TimeState {
  Furble::TimeKeeper::status_t status = {};
  size_t flushes = 0;
};

TimeState &time(void);

/** Redirect stdout into a captured file. Call once, before Console::init(). */
void startCapture(const std::string &path);

/** Everything printed since the given capture offset. */
std::string capturedSince(long offset);

/** Current capture offset. */
long captureOffset(void);

/**
 * Type a line at the console task and return what the command printed.
 *
 * Feeds the bytes through the transport double exactly as a host script would,
 * so the production line editor, esp_console_run() and the command handler all
 * run. Returns once the task has printed its next prompt, with the echoed line
 * and the trailing prompt stripped.
 */
std::string runLine(const std::string &line);

/** Feed raw bytes without waiting for a prompt, for line editor tests. */
void feedBytes(const std::string &bytes);

/** Wait until the transport has consumed every fed byte. */
bool waitForInputDrained(int timeout_ms);

/**
 * Park the console task so the process can exit safely.
 *
 * The console task is detached and loops forever, exactly as it does on
 * device. Returning from main() while it is blocked on the transport would let
 * the runtime destroy the globals it is waiting on. This wakes it at a known
 * point inside the transport read and leaves it sleeping there, touching
 * nothing else, for the rest of the process. Returns false if it did not park
 * within the timeout.
 */
bool parkConsoleTask(int timeout_ms);

/** Reset every recorded call and injected result to its default. */
void resetDoubles(void);

}  // namespace ConsoleHost

#endif
