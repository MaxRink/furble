// Host coverage for the documented `cmd/ota` handler and its status record.
//
// The transport, manifest source, context and detail doubles are scripted per
// case. The production parser, precondition policy, engine, retained policy
// and JSON formatter all run unchanged, so the complete update, every refusal,
// duplicate QoS-1 delivery and a rollback after a failed health check are
// exercised without a network, a broker or flash.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "FurbleOTA.h"
#include "FurbleOTAAdmission.h"
#include "FurbleOTACommand.h"
#include "FurbleOTAStatus.h"

namespace OTA = Furble::OTA;
namespace Command = Furble::OTA::Command;

namespace {

int g_Failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
}

void checkContains(const std::string &haystack, const std::string &needle, const char *message) {
  if (haystack.find(needle) == std::string::npos) {
    std::cerr << "  FAIL: " << message << " (missing '" << needle << "' in " << haystack << ")\n";
    g_Failures++;
  }
}

struct Step {
  OTA::DownloadResult result = OTA::DownloadResult::InProgress;
  size_t bytes = 0;
};

class MockTransport final: public OTA::Transport {
 public:
  size_t totalBytes = 1000;
  bool checkSucceeds = true;
  std::vector<Step> plan;
  bool verifySucceeds = true;
  bool applySucceeds = true;
  std::string detail = "none";

  size_t checkCalls = 0;
  size_t downloadCalls = 0;
  size_t verifyCalls = 0;
  size_t applyCalls = 0;
  size_t abortCalls = 0;
  std::vector<size_t> checkOffsets;

  bool check(const char *, size_t resumeOffset, size_t &total) override {
    checkCalls++;
    checkOffsets.push_back(resumeOffset);
    planIndex = 0;
    total = totalBytes;
    return checkSucceeds;
  }

  OTA::DownloadProgress download() override {
    downloadCalls++;
    OTA::DownloadProgress progress;
    progress.totalBytes = totalBytes;
    if (planIndex >= plan.size()) {
      progress.result = OTA::DownloadResult::Failed;
      return progress;
    }
    const Step step = plan[planIndex++];
    progress.result = step.result;
    progress.bytesDownloaded = step.bytes;
    return progress;
  }

  bool verify() override {
    verifyCalls++;
    return verifySucceeds;
  }

  bool apply() override {
    applyCalls++;
    return applySucceeds;
  }

  void abort() override { abortCalls++; }

 private:
  size_t planIndex = 0;
};

class FakeSource final: public Command::Source {
 public:
  bool succeeds = true;
  std::string version = "1.3.0";
  size_t imageSize = 1000;
  size_t slotCapacity = 4096;
  size_t calls = 0;
  std::string lastUrl;

  bool fetch(const char *url, Available &available) override {
    calls++;
    lastUrl = (url == nullptr) ? "" : url;
    if (!succeeds) {
      return false;
    }
    std::snprintf(available.version, sizeof(available.version), "%s", version.c_str());
    available.imageSize = imageSize;
    available.slotCapacity = slotCapacity;
    return true;
  }
};

class FakeContext final: public Command::Context {
 public:
  OTA::Conditions next;
  const char *installed = "1.2.0";
  OTA::BootReport boot;

  OTA::Conditions conditions(void) override { return next; }
  const char *installedVersion(void) override { return installed; }
  OTA::BootReport bootReport(void) override { return boot; }
};

class FakeDetail final: public Command::Detail {
 public:
  std::string value = "none";
  const char *detail(void) const override { return value.c_str(); }
};

struct Fixture {
  MockTransport transport;
  OTA::Engine engine {transport};
  FakeSource source;
  FakeContext context;
  FakeDetail detail;
  Command::Handler handler {engine, source, context, &detail};

  Fixture() {
    context.next.linkReady = true;
    context.next.batteryKnown = false;
  }

  Command::Handler::Result deliver(const std::string &payload, bool duplicate = false) {
    Command::Meta meta;
    meta.duplicate = duplicate;
    return handler.onMessage(meta, payload.data(), payload.size());
  }

  void run(void) {
    for (size_t guard = 0; (guard < 64) && engine.busy(); guard++) {
      handler.step();
    }
  }

  std::string json(void) const { return OTA::statusJson(handler.status()); }
};

void testParseRequest(void) {
  Command::Request request;

  check(Command::parseRequest("status", 6, request), "status parses");
  check(request.action == OTA::Action::Status, "status names its action");

  check(Command::parseRequest("  abort \n", 9, request), "surrounding whitespace is ignored");
  check(request.action == OTA::Action::Abort, "abort names its action");

  check(Command::parseRequest("check", 5, request), "a bare check parses");
  check((request.action == OTA::Action::Check) && (request.url[0] == '\0'),
        "a bare check carries no URL");

  const std::string url = "https://updates.example/furble.bin";
  check(Command::parseRequest(("check " + url).c_str(), 6 + url.size(), request),
        "check with a URL parses");
  check((request.action == OTA::Action::Check) && (url == request.url), "check keeps the URL");

  check(Command::parseRequest(url.c_str(), url.size(), request), "a bare URL parses");
  check((request.action == OTA::Action::Update) && (url == request.url), "a bare URL is an update");

  check(!Command::parseRequest("", 0, request), "an empty payload is invalid");
  check(!Command::parseRequest("   \n", 4, request), "whitespace is invalid");
  check(!Command::parseRequest("status now", 10, request), "status rejects extra data");
  check(!Command::parseRequest("check one two", 13, request), "check rejects a second URL");
  check(!Command::parseRequest("fetch", 5, request), "an unknown keyword is invalid");
  check(!Command::parseRequest("a\0b", 3, request), "an embedded NUL is invalid");

  std::string longPayload(Command::MAX_PAYLOAD_LENGTH + 1, 'a');
  check(!Command::parseRequest(longPayload.data(), longPayload.size(), request),
        "an oversized payload is invalid");

  std::string longUrl = "https://updates.example/";
  longUrl.append(OTA::Engine::MAX_URL_LENGTH, 'a');
  check(!Command::parseRequest(longUrl.data(), longUrl.size(), request),
        "a URL past the engine limit is invalid");
}

void testVersionComparison(void) {
  check(OTA::compareVersions("1.2.0", "1.3.0") == -1, "a newer release compares older");
  check(OTA::compareVersions("1.3.0", "1.2.0") == 1, "an older release compares newer");
  check(OTA::compareVersions("1.2.0", "1.2.0") == 0, "equal releases compare equal");
  check(OTA::compareVersions("1.9.9", "1.10.0") == -1, "components compare numerically");
  check(OTA::compareVersions("v1.2.3", "1.2.3") == 0, "a leading v is ignored");
  check(OTA::compareVersions("1.2", "1.2.0") == 0, "a missing component counts as zero");
  check(OTA::compareVersions("1.2.3-rc1", "1.2.3") == 0, "a suffix is ignored");
  check(OTA::compareVersions("1.2.3", "1.2.3+build") == 0, "a build suffix is ignored");
  check(OTA::compareVersions("1.2.3.4", "1.2.3.5") == -1, "four components compare");
  check(OTA::compareVersions("dev+gabc123", "1.2.3") == -1,
        "a development build is older than a release");
  check(OTA::compareVersions("1.2.3", "dev+gabc123") == OTA::VERSION_INVALID,
        "an invalid available version is not comparable");
  check(OTA::compareVersions("", "1.2.3") == -1, "an empty installed version is older");
  check(OTA::compareVersions("1.2.3", "") == OTA::VERSION_INVALID,
        "an empty available version is invalid");
  check(OTA::compareVersions("1.2.3.4.5", "1.2.3") == -1, "five components are not a version");
  check(OTA::versionIsNewer(OTA::compareVersions("1.2.0", "1.2.1")),
        "a newer comparison is an update");
  check(!OTA::versionIsNewer(OTA::compareVersions("1.2.1", "1.2.1")),
        "an equal comparison is not an update");
}

void testConditionPolicy(void) {
  OTA::Conditions conditions;
  conditions.linkReady = true;
  check(OTA::evaluateUpdate(conditions) == OTA::Refusal::None, "a clear device is admitted");

  conditions.updateBusy = true;
  check(OTA::evaluateUpdate(conditions) == OTA::Refusal::Busy, "a busy device is refused");
  conditions.updateBusy = false;

  conditions.cameraSessionActive = true;
  check(OTA::evaluateUpdate(conditions) == OTA::Refusal::CameraSession,
        "an active camera session is refused");
  conditions.cameraSessionActive = false;

  conditions.intervalometerRunning = true;
  check(OTA::evaluateUpdate(conditions) == OTA::Refusal::Intervalometer,
        "a running intervalometer is refused");
  conditions.intervalometerRunning = false;

  conditions.linkReady = false;
  check(OTA::evaluateUpdate(conditions) == OTA::Refusal::LinkDown, "a down link is refused");
  conditions.linkReady = true;

  conditions.slotAvailable = false;
  check(OTA::evaluateUpdate(conditions) == OTA::Refusal::SlotUnavailable,
        "a device without a second slot is refused");
  conditions.slotAvailable = true;

  conditions.batteryKnown = true;
  conditions.batteryPercent = OTA::MIN_UPDATE_BATTERY_PERCENT - 1;
  check(OTA::evaluateUpdate(conditions) == OTA::Refusal::BatteryLow, "a low battery is refused");
  conditions.charging = true;
  check(OTA::evaluateUpdate(conditions) == OTA::Refusal::None,
        "a charging device below the floor is admitted");
  conditions.charging = false;
  conditions.batteryPercent = OTA::MIN_UPDATE_BATTERY_PERCENT;
  check(OTA::evaluateUpdate(conditions) == OTA::Refusal::None, "a device at the floor is admitted");
  conditions.batteryKnown = false;
  conditions.batteryPercent = 0;
  check(OTA::evaluateUpdate(conditions) == OTA::Refusal::None,
        "a device without a gauge is admitted");

  const struct {
    OTA::Refusal refusal;
    const char *name;
  } names[] = {
      {OTA::Refusal::None,                 "none"                 },
      {OTA::Refusal::RetainedCommand,      "retained_command"     },
      {OTA::Refusal::Busy,                 "busy"                 },
      {OTA::Refusal::CameraSession,        "camera_session"       },
      {OTA::Refusal::Intervalometer,       "intervalometer"       },
      {OTA::Refusal::LinkDown,             "link_down"            },
      {OTA::Refusal::BatteryLow,           "battery_low"          },
      {OTA::Refusal::SlotUnavailable,      "slot_unavailable"     },
      {OTA::Refusal::SourceUnavailable,    "source_unavailable"   },
      {OTA::Refusal::NoNewerVersion,       "no_newer_version"     },
      {OTA::Refusal::InvalidSource,        "invalid_source"       },
      {OTA::Refusal::TransportUnavailable, "transport_unavailable"},
  };
  for (const auto &entry : names) {
    check(std::string(OTA::refusalName(entry.refusal)) == entry.name,
          "every refusal has a stable name");
  }
}

void testCheckWithoutDownload(void) {
  Fixture fixture;
  const auto result = fixture.deliver("check https://updates.example/furble.bin");

  check(result.accepted, "a check is accepted");
  check(fixture.source.calls == 1, "a check reads the manifest once");
  check(fixture.transport.checkCalls == 0, "a check does not open the image");
  check(!fixture.engine.busy(), "a check does not start an update");
  check(fixture.engine.snapshot().state == OTA::State::Idle, "a check leaves the engine idle");

  const OTA::Status status = fixture.handler.status();
  check(status.checked, "the status records the check");
  check(status.updateAvailable, "a newer available version is an update");
  check(std::string(status.availableVersion) == "1.3.0", "the available version is reported");
  checkContains(fixture.json(), "\"available\":\"1.3.0\"", "the payload carries the version");
  checkContains(fixture.json(), "\"update\":true", "the payload carries update availability");
  check(!fixture.handler.statusRetained(), "a check result is not retained");

  // A bare check reuses the URL the device was last given.
  check(fixture.deliver("check").accepted, "a bare check reuses the last URL");
  check(fixture.source.calls == 2, "the bare check reads the manifest again");
  check(fixture.source.lastUrl == "https://updates.example/furble.bin",
        "the bare check keeps the last URL");

  Fixture empty;
  const auto refused = empty.deliver("check");
  check(!refused.accepted && (refused.refusal == OTA::Refusal::InvalidSource),
        "a bare check without a URL is refused");
  check(empty.source.calls == 0, "a URL-less check never reads a manifest");

  Fixture offline;
  offline.context.next.linkReady = false;
  const auto offlineResult = offline.deliver("check https://updates.example/furble.bin");
  check(offlineResult.refusal == OTA::Refusal::LinkDown, "a check needs the link");
  check(offline.source.calls == 0, "an offline check never reads a manifest");

  Fixture older;
  older.source.version = "1.2.0";
  check(older.deliver("check https://updates.example/furble.bin").accepted,
        "a check at the current version is accepted");
  check(!older.handler.status().updateAvailable, "the current version is not an update");
}

void testUpdateRefusals(void) {
  const std::string url = "https://updates.example/furble.bin";

  struct Case {
    const char *name;
    bool retained;
    void (*configure)(Fixture &);
    OTA::Refusal expected;
  };

  const Case cases[] = {
      {"retained",       true,  nullptr,                                                         OTA::Refusal::RetainedCommand},
      {"camera",         false, [](Fixture &f) { f.context.next.cameraSessionActive = true; },
       OTA::Refusal::CameraSession                                                                                            },
      {"intervalometer", false, [](Fixture &f) { f.context.next.intervalometerRunning = true; },
       OTA::Refusal::Intervalometer                                                                                           },
      {"link",           false, [](Fixture &f) { f.context.next.linkReady = false; },            OTA::Refusal::LinkDown       },
      {"battery",        false,
       [](Fixture &f) {
         f.context.next.batteryKnown = true;
         f.context.next.batteryPercent = 10;
       },                                                                                        OTA::Refusal::BatteryLow     },
      {"slot",           false, [](Fixture &f) { f.context.next.slotAvailable = false; },
       OTA::Refusal::SlotUnavailable                                                                                          },
      {"source",         false, [](Fixture &f) { f.source.succeeds = false; },
       OTA::Refusal::SourceUnavailable                                                                                        },
      {"version",        false, [](Fixture &f) { f.source.version = "1.2.0"; },
       OTA::Refusal::NoNewerVersion                                                                                           },
      {"manifest slot",  false, [](Fixture &f) { f.source.imageSize = 5000; },
       OTA::Refusal::SlotUnavailable                                                                                          },
  };

  for (const Case &entry : cases) {
    Fixture fixture;
    if (entry.configure != nullptr) {
      entry.configure(fixture);
    }

    Command::Meta meta;
    meta.retained = entry.retained;
    const auto result = fixture.handler.onMessage(meta, url.data(), url.size());
    check(!result.accepted, entry.name);
    check(result.refusal == entry.expected, entry.name);
    check(!fixture.engine.busy(), "a refused update leaves the engine idle");
    check(fixture.transport.checkCalls == 0, "a refused update never opens the image");
    check(fixture.handler.status().outcome == OTA::Outcome::Refused,
          "a refused update records the outcome");
    check(fixture.handler.status().reason == entry.expected, "the status carries the reason");
    checkContains(fixture.json(),
                  std::string("\"reason\":\"") + OTA::refusalName(entry.expected) + "\"",
                  "the payload carries the refusal");
  }

  Fixture charging;
  charging.context.next.batteryKnown = true;
  charging.context.next.batteryPercent = 10;
  charging.context.next.charging = true;
  check(charging.deliver(url).accepted, "a charging device below the floor is admitted");

  Fixture http;
  check(http.deliver("http://updates.example/furble.bin").refusal == OTA::Refusal::InvalidSource,
        "an http URL is refused before the manifest");
  check(http.source.calls == 0, "an http URL never reads a manifest");

  Fixture invalidsource;
  invalidsource.source.version = "";
  check(invalidsource.deliver(url).refusal == OTA::Refusal::SourceUnavailable,
        "a manifest without a version is refused");
}

void testDuplicateDelivery(void) {
  Fixture fixture;
  const std::string first = "https://updates.example/furble.bin";
  const std::string second = "https://updates.example/other.bin";

  const auto accepted = fixture.deliver(first);
  check(accepted.accepted, "a clear URL starts once");
  check(fixture.engine.busy(), "the update is running");
  check(fixture.source.calls == 1, "the manifest is read once");

  const auto duplicate = fixture.deliver(first, true);
  check(!duplicate.accepted && duplicate.duplicate, "a duplicate delivery is idempotent");
  check(fixture.handler.status().outcome == OTA::Outcome::Duplicate,
        "the duplicate outcome is reported");
  check(fixture.source.calls == 1, "the duplicate does not read the manifest again");
  check(fixture.engine.snapshot().state == OTA::State::Checking,
        "the duplicate does not restart the engine");

  const auto busy = fixture.deliver(second);
  check(!busy.accepted && (busy.refusal == OTA::Refusal::Busy),
        "a different URL while busy is refused");
}

void testCompleteUpdateAndRetainedPolicy(void) {
  Fixture fixture;
  const std::string url = "https://updates.example/furble.bin";
  fixture.transport.plan = {
      {OTA::DownloadResult::InProgress, 400 },
      {OTA::DownloadResult::Complete,   1000}
  };

  check(!fixture.handler.statusRetained(), "an idle status is not retained");
  check(fixture.deliver(url).accepted, "the update is accepted");
  check(!fixture.handler.statusRetained(), "the first checking status is not retained");
  check(fixture.handler.status().state == OTA::State::Checking, "the engine is checking");

  const size_t revision = fixture.handler.revision();
  check(fixture.deliver("status").accepted, "status is accepted");
  check(fixture.handler.revision() > revision, "status forces a fresh report");
  check(fixture.handler.status().outcome == OTA::Outcome::Accepted,
        "status records an accepted command");

  fixture.run();
  check(fixture.engine.snapshot().state == OTA::State::Done, "the update completes");
  check(fixture.handler.status().progress == 100, "the status reports completion");
  check(fixture.handler.statusRetained(), "a final result is retained");
  checkContains(fixture.json(), "\"state\":\"done\"", "the payload reports done");

  // A new update must clear the retained result on the broker. The first
  // checking record is retained for that reason.
  fixture.transport.plan = {
      {OTA::DownloadResult::Complete, 1000}
  };
  fixture.source.version = "1.4.0";
  const std::string next = "https://updates.example/furble-1.4.0.bin";
  check(fixture.deliver(next).accepted, "a newer release starts a second update");
  check(fixture.handler.statusRetained(), "the first checking status after a result is retained");
  fixture.handler.step();
  check(fixture.handler.status().state != OTA::State::Checking,
        "the second update leaves the checking state");
  check(!fixture.handler.statusRetained(), "live progress is not retained");
  fixture.run();
  check(fixture.engine.snapshot().state == OTA::State::Done, "the second update completes");
}

void testFailureDetailAndResume(void) {
  Fixture fixture;
  const std::string url = "https://updates.example/furble.bin";

  // A verification failure surfaces the transport detail.
  fixture.transport.plan = {
      {OTA::DownloadResult::Complete, 1000}
  };
  fixture.transport.verifySucceeds = false;
  fixture.detail.value = "signature_invalid";
  check(fixture.deliver(url).accepted, "the failing update starts");
  fixture.run();
  check(fixture.engine.snapshot().state == OTA::State::Error, "a failed verify fails");
  check(fixture.handler.status().error == OTA::Error::VerificationFailed,
        "the status reports verify_failed");
  check(std::string(fixture.handler.status().detail) == "signature_invalid",
        "the status carries the transport detail");
  checkContains(fixture.json(), "\"detail\":\"signature_invalid\"",
                "the payload carries the transport detail");
  check(fixture.handler.statusRetained(), "a failed result is retained");

  // An interrupted transfer keeps its checkpoint and resumes on a fresh
  // command, while a redelivery of the failed command does not retry.
  Fixture interrupted;
  interrupted.transport.plan = {
      {OTA::DownloadResult::InProgress, 400},
      {OTA::DownloadResult::Failed,     0  }
  };
  interrupted.detail.value = "transfer_interrupted";
  check(interrupted.deliver(url).accepted, "the interrupted update starts");
  interrupted.run();
  check(interrupted.engine.snapshot().state == OTA::State::Error, "the transfer fails");
  check(interrupted.engine.snapshot().bytesDownloaded == 400, "the checkpoint is kept");

  const auto replay = interrupted.deliver(url, true);
  check(!replay.accepted && replay.duplicate, "a replay of the failed command is a duplicate");
  check(interrupted.transport.checkCalls == 1, "the replay does not open the image again");

  interrupted.transport.plan = {
      {OTA::DownloadResult::InProgress, 800 },
      {OTA::DownloadResult::Complete,   1000}
  };
  check(interrupted.deliver(url).accepted, "a fresh command retries the update");
  interrupted.handler.step();
  check(interrupted.transport.checkOffsets.size() == 2, "the retry opens the image again");
  check(interrupted.transport.checkOffsets[1] == 400, "the retry resumes at the checkpoint");
  interrupted.run();
  check(interrupted.engine.snapshot().state == OTA::State::Done, "the resumed update completes");

  const auto succeededReplay = interrupted.deliver(url, true);
  check(!succeededReplay.accepted && succeededReplay.duplicate,
        "a redelivery after success is a duplicate");
}

void testAbortCommand(void) {
  Fixture fixture;
  const std::string url = "https://updates.example/furble.bin";
  fixture.transport.plan = {
      {OTA::DownloadResult::InProgress, 400},
      {OTA::DownloadResult::InProgress, 800}
  };
  check(fixture.deliver(url).accepted, "the aborted update starts");
  fixture.handler.step();
  check(fixture.engine.busy(), "the update is running");

  check(fixture.deliver("abort").accepted, "abort is accepted");
  check(fixture.engine.snapshot().state == OTA::State::Error, "abort fails the engine");
  check(fixture.engine.lastError() == OTA::Error::Aborted, "the engine reports aborted");
  check(fixture.handler.status().command == OTA::Action::Abort, "the command is reported");
  check(fixture.transport.abortCalls == 1, "the transport is asked to discard once");
  check(fixture.handler.statusRetained(), "an aborted result is retained");

  check(fixture.deliver("abort").accepted, "an abort with nothing running is accepted");
  check(fixture.transport.abortCalls == 1, "an idle abort does not touch the transport");
}

void testRollbackReport(void) {
  Fixture fixture;
  const std::string url = "https://updates.example/furble.bin";
  fixture.transport.plan = {
      {OTA::DownloadResult::Complete, 1000}
  };
  check(fixture.deliver(url).accepted, "the update that will roll back starts");
  fixture.run();
  check(fixture.engine.snapshot().state == OTA::State::Done, "the update reports done");

  // The pending image is evaluated on the next boot. A failed health check
  // marks it invalid and rolls back to the previous slot.
  OTA::BootHealth health;
  health.rollbackState = OTA::RollbackState::PendingVerify;
  health.nvsReady = true;
  health.platformReady = true;
  health.bleReady = true;
  health.controlReady = false;
  health.validationDeadlineReached = false;
  const OTA::BootDecision waiting = OTA::evaluateBootHealth(health);
  check(waiting.action == OTA::BootAction::WaitForHealth,
        "a pending image waits for its health window");

  health.validationDeadlineReached = true;
  const OTA::BootDecision rolledBack = OTA::evaluateBootHealth(health);
  check(rolledBack.action == OTA::BootAction::MarkInvalidRollbackAndReboot,
        "a failed health check marks the image invalid");
  check(rolledBack.failure == OTA::BootFailure::Control, "the failed gate is named");

  fixture.context.boot = {OTA::RollbackState::PendingVerify, rolledBack.action, rolledBack.failure};
  OTA::Status pending = fixture.handler.status();
  check(OTA::statusPending(pending), "a pending image is reported");
  checkContains(OTA::statusJson(pending), "\"slot\":\"pending_verify\"",
                "the payload reports the pending slot");
  checkContains(OTA::statusJson(pending), "\"boot\":\"mark_invalid_rollback_and_reboot\"",
                "the payload reports the rollback action");
  checkContains(OTA::statusJson(pending), "\"failure\":\"control\"",
                "the payload reports the failed gate");
  checkContains(OTA::formatStatus(pending), "slot: pending_verify",
                "the console report names the pending slot");

  // On the next boot the running image is the previous slot and the rejected
  // image is recorded as aborted.
  fixture.context.boot = {OTA::RollbackState::Aborted, OTA::BootAction::NoAction,
                          OTA::BootFailure::None};
  const OTA::Status after = fixture.handler.status();
  check(!OTA::statusPending(after), "the rolled-back image is no longer pending");
  checkContains(OTA::statusJson(after), "\"slot\":\"aborted\"",
                "the payload reports the rollback loudly");
  checkContains(OTA::formatStatus(after), "slot: aborted", "the console report names the rollback");
  check(fixture.handler.statusRetained(), "the retained result carries the rollback");
}

void testStatusFormatAndStore(void) {
  OTA::Status status;
  status.state = OTA::State::Downloading;
  status.error = OTA::Error::None;
  status.progress = 42;
  status.bytesDownloaded = 4200;
  status.totalBytes = 10000;
  status.reason = OTA::Refusal::None;
  status.command = OTA::Action::Update;
  status.outcome = OTA::Outcome::Accepted;
  std::snprintf(status.installedVersion, sizeof(status.installedVersion), "%s", "1.0\"x");
  std::snprintf(status.availableVersion, sizeof(status.availableVersion), "%s", "1.1.0");
  status.updateAvailable = true;
  status.checked = true;
  status.busy = true;

  const std::string json = OTA::statusJson(status);
  checkContains(json, "\"state\":\"downloading\"", "the payload reports the state");
  checkContains(json, "\"error\":\"none\"", "the payload reports the engine error");
  checkContains(json, "\"progress\":42", "the payload reports progress");
  checkContains(json, "\"bytes\":4200", "the payload reports the byte count");
  checkContains(json, "\"total\":10000", "the payload reports the total");
  checkContains(json, "\"reason\":\"none\"", "the payload reports the reason");
  checkContains(json, "\"detail\":\"none\"", "an empty detail is reported as none");
  checkContains(json, "\"command\":\"update\"", "the payload reports the command");
  checkContains(json, "\"result\":\"accepted\"", "the payload reports the outcome");
  checkContains(json, "\"installed\":\"1.0\\\"x\"", "a quote in a version is escaped");
  checkContains(json, "\"available\":\"1.1.0\"", "the payload reports the version");
  checkContains(json, "\"update\":true", "the payload reports availability");
  checkContains(json, "\"checked\":true", "the payload reports the check");
  checkContains(json, "\"busy\":true", "the payload reports the engine state");
  checkContains(json, "\"slot\":\"undefined\"", "an unread slot is reported as undefined");
  checkContains(json, "\"boot\":\"no_action\"", "an unevaluated boot is reported as a no-op");
  checkContains(json, "\"failure\":\"none\"", "an unevaluated boot has no failure");

  const std::string text = OTA::formatStatus(status);
  checkContains(text, "ota: downloading", "the console report starts with the state");
  checkContains(text, "progress: 42 percent, 4200 of 10000 bytes",
                "the console report carries progress");
  checkContains(text, "installed: 1.0\"x", "the console report carries the version");
  checkContains(text, "update yes", "the console report carries availability");

  OTA::StatusStore::getInstance().set(status);
  const OTA::Status stored = OTA::StatusStore::getInstance().get();
  check((stored.state == OTA::State::Downloading) && (stored.progress == 42),
        "the status store round trips a record");
  check(OTA::statusTerminal(status) == false, "a downloading status is not terminal");
  status.state = OTA::State::Done;
  check(OTA::statusTerminal(status), "a done status is terminal");
  status.state = OTA::State::Error;
  check(OTA::statusTerminal(status), "an error status is terminal");
}

}  // namespace

int main() {
  testParseRequest();
  testVersionComparison();
  testConditionPolicy();
  testCheckWithoutDownload();
  testUpdateRefusals();
  testDuplicateDelivery();
  testCompleteUpdateAndRetainedPolicy();
  testFailureDetailAndResume();
  testAbortCommand();
  testRollbackReport();
  testStatusFormatAndStore();

  if (g_Failures != 0) {
    std::cout << "ota mqtt handler harness: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "ota mqtt handler harness: PASS\n";
  return 0;
}
