// Host coverage for the transport-independent OTA lifecycle.
//
// The mock below is deliberately small. It records the lifecycle calls,
// exposes absolute byte checkpoints, and lets each test choose the result of
// every download poll. No ESP-IDF or network dependency is needed.

#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "FurbleOTA.h"

namespace OTA = Furble::OTA;

namespace {

int g_Failures = 0;

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

struct Poll {
  OTA::DownloadResult result;
  size_t bytesDownloaded;
  size_t totalBytes = 0;
};

class MockTransport final: public OTA::Transport {
 public:
  size_t totalBytes = 100;
  bool checkSucceeds = true;
  bool verifySucceeds = true;
  bool applySucceeds = true;
  std::vector<Poll> plan;
  std::vector<size_t> checkOffsets;
  std::vector<std::string> checkUrls;
  size_t checkCalls = 0;
  size_t downloadCalls = 0;
  size_t verifyCalls = 0;
  size_t applyCalls = 0;
  size_t abortCalls = 0;

  bool check(const char *url, size_t resumeOffset, size_t &total) override {
    checkCalls++;
    checkOffsets.push_back(resumeOffset);
    checkUrls.emplace_back(url == nullptr ? "" : url);
    planIndex = 0;
    total = totalBytes;
    return checkSucceeds;
  }

  OTA::DownloadProgress download() override {
    downloadCalls++;
    OTA::DownloadProgress progress;
    if (planIndex >= plan.size()) {
      progress.result = OTA::DownloadResult::Failed;
      return progress;
    }

    const Poll poll = plan[planIndex++];
    progress.result = poll.result;
    progress.bytesDownloaded = poll.bytesDownloaded;
    progress.totalBytes = poll.totalBytes == 0 ? totalBytes : poll.totalBytes;
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

bool runToTerminal(OTA::Engine &engine) {
  size_t steps = 0;
  while (engine.busy() && (steps++ < 32)) {
    engine.step();
  }
  return !engine.busy();
}

bool testCleanSuccessAndBusyRejection() {
  std::cout << "test: a clean download verifies, applies, and reaches done\n";
  const int before = g_Failures;

  MockTransport transport;
  transport.plan = {
      {OTA::DownloadResult::InProgress, 25 },
      {OTA::DownloadResult::InProgress, 60 },
      {OTA::DownloadResult::Complete,   100},
  };
  OTA::Engine engine(transport);

  check(engine.begin("https://example.invalid/firmware.bin"), "begin accepts a URL");
  check(engine.snapshot().state == OTA::State::Checking, "begin enters checking");
  check(!engine.begin("https://example.invalid/duplicate.bin"),
        "a second begin is rejected while running");
  check(engine.lastError() == OTA::Error::Busy, "duplicate begin reports busy");
  check(engine.snapshot().state == OTA::State::Checking,
        "duplicate begin does not disrupt the active update");

  check(engine.step(), "checking step succeeds");
  check(engine.snapshot().state == OTA::State::Downloading, "successful check enters downloading");
  check(engine.step(), "first download chunk is accepted");
  check(engine.step(), "second download chunk is accepted");
  check(engine.step(), "final download chunk is accepted");
  check(engine.snapshot().state == OTA::State::Verifying, "complete download enters verifying");
  check(engine.step(), "verification step succeeds");
  check(engine.snapshot().state == OTA::State::Applying, "successful verification enters applying");
  check(engine.step(), "apply step succeeds");
  check(!engine.busy(), "clean update reaches a terminal state");
  check(engine.snapshot().state == OTA::State::Done, "clean update reaches done");
  check(engine.snapshot().error == OTA::Error::None, "clean update has no error");
  check(engine.snapshot().progress == 100, "clean update reports 100 percent");
  check(engine.snapshot().bytesDownloaded == 100, "clean update reports all bytes");
  check(engine.lastError() == OTA::Error::None, "successful completion clears transient errors");
  check(transport.checkCalls == 1, "transport check runs once");
  check(transport.downloadCalls == 3, "transport is polled once per planned chunk");
  check(transport.verifyCalls == 1, "transport verify runs once");
  check(transport.applyCalls == 1, "transport apply runs once");
  check(transport.abortCalls == 0, "successful update is not aborted");
  check(transport.checkUrls.front() == "https://example.invalid/firmware.bin",
        "transport receives the requested URL");

  return g_Failures == before;
}

bool testMidDownloadFailure() {
  std::cout << "test: a mid-download transport failure reaches error\n";
  const int before = g_Failures;

  MockTransport transport;
  transport.plan = {
      {OTA::DownloadResult::InProgress, 32},
      {OTA::DownloadResult::Failed,     32},
  };
  OTA::Engine engine(transport);

  check(engine.begin("https://example.invalid/broken.bin"), "failure case begins");
  check(engine.step(), "failure case completes checking");
  check(engine.step(), "first download chunk is accepted");
  check(engine.snapshot().progress == 32, "failure case preserves its checkpoint");
  check(!engine.step(), "failed download step returns false");
  check(engine.snapshot().state == OTA::State::Error, "failed download reaches error");
  check(engine.snapshot().error == OTA::Error::DownloadFailed,
        "failed download reports download_failed");
  check(transport.abortCalls == 1, "failed download aborts the transport");
  check(transport.verifyCalls == 0, "failed download never verifies");
  check(transport.applyCalls == 0, "failed download never applies");

  return g_Failures == before;
}

bool testVerificationFailure() {
  std::cout << "test: a verification failure reaches error without applying\n";
  const int before = g_Failures;

  MockTransport transport;
  transport.verifySucceeds = false;
  transport.plan = {
      {OTA::DownloadResult::Complete, 100}
  };
  OTA::Engine engine(transport);

  check(engine.begin("https://example.invalid/unsigned.bin"), "verify case begins");
  check(runToTerminal(engine), "verify failure reaches a terminal state");
  check(engine.snapshot().state == OTA::State::Error, "verify failure reaches error");
  check(engine.snapshot().error == OTA::Error::VerificationFailed,
        "verify failure reports verification_failed");
  check(transport.verifyCalls == 1, "transport verify runs once on failure");
  check(transport.applyCalls == 0, "verification failure never applies the image");
  check(transport.abortCalls == 1, "verification failure aborts the transport");

  return g_Failures == before;
}

bool testProgressIsMonotonic() {
  std::cout << "test: reported progress never moves backwards\n";
  const int before = g_Failures;

  MockTransport transport;
  transport.plan = {
      {OTA::DownloadResult::InProgress, 7  },
      {OTA::DownloadResult::InProgress, 34 },
      {OTA::DownloadResult::InProgress, 65 },
      {OTA::DownloadResult::Complete,   100},
  };
  OTA::Engine engine(transport);
  check(engine.begin("https://example.invalid/monotonic.bin"), "progress case begins");

  uint8_t previous = engine.snapshot().progress;
  size_t steps = 0;
  while (engine.busy() && (steps++ < 32)) {
    check(engine.step(), "progress step is accepted");
    const uint8_t current = engine.snapshot().progress;
    check(current >= previous, "progress is monotonic");
    previous = current;
  }
  check(engine.snapshot().state == OTA::State::Done, "progress case reaches done");
  check(previous == 100, "progress case ends at 100 percent");

  return g_Failures == before;
}

bool testResumeFromCheckpoint() {
  std::cout << "test: a later attempt resumes from the failed checkpoint\n";
  const int before = g_Failures;

  MockTransport transport;
  transport.plan = {
      {OTA::DownloadResult::InProgress, 32},
      {OTA::DownloadResult::Failed,     32},
  };
  OTA::Engine engine(transport);
  check(engine.begin("https://example.invalid/resume.bin"), "resume case begins");
  check(engine.step(), "resume case completes checking");
  check(engine.step(), "resume case records the first chunk");
  check(!engine.step(), "resume case records the failed chunk");
  const size_t checkpoint = engine.snapshot().bytesDownloaded;
  check(checkpoint == 32, "resume case exposes the failed checkpoint");

  transport.plan = {
      {OTA::DownloadResult::InProgress, 64 },
      {OTA::DownloadResult::Complete,   100},
  };
  check(engine.begin("https://example.invalid/resume.bin", checkpoint),
        "a new attempt accepts the checkpoint");
  check(runToTerminal(engine), "resumed update reaches a terminal state");
  check(engine.snapshot().state == OTA::State::Done, "resumed update reaches done");
  check(engine.snapshot().progress == 100, "resumed update reports 100 percent");
  check(transport.checkCalls == 2, "resume uses two transport checks total");
  check(transport.checkOffsets.size() == 2, "resume records both offsets");
  check(transport.checkOffsets[0] == 0, "first attempt starts at zero");
  check(transport.checkOffsets[1] == checkpoint, "second attempt starts at checkpoint");

  return g_Failures == before;
}

bool testAbortPreservesCheckpoint() {
  std::cout << "test: abort stops the transfer and preserves its checkpoint\n";
  const int before = g_Failures;

  MockTransport transport;
  transport.plan = {
      {OTA::DownloadResult::InProgress, 28},
      {OTA::DownloadResult::InProgress, 56},
  };
  OTA::Engine engine(transport);
  check(engine.begin("https://example.invalid/abort.bin"), "abort case begins");
  check(engine.step(), "abort case completes checking");
  check(engine.step(), "abort case records its checkpoint");
  engine.abort();

  check(!engine.busy(), "aborted update is no longer busy");
  check(engine.snapshot().state == OTA::State::Error, "aborted update reaches error");
  check(engine.snapshot().error == OTA::Error::Aborted, "abort reports aborted");
  check(engine.snapshot().bytesDownloaded == 28, "abort preserves the checkpoint");
  check(transport.abortCalls == 1, "abort calls the transport once");
  check(!engine.step(), "an aborted update does not continue downloading");

  return g_Failures == before;
}

bool testInvalidRequestsNeverReachTransport() {
  std::cout << "test: invalid requests fail before touching the transport\n";
  const int before = g_Failures;

  MockTransport transport;
  OTA::Engine engine(transport);
  check(!engine.begin(nullptr), "null URL is rejected");
  check(engine.snapshot().error == OTA::Error::InvalidRequest, "null URL reports invalid request");
  check(!engine.begin(""), "empty URL is rejected");
  const std::string tooLong(OTA::Engine::MAX_URL_LENGTH, 'x');
  check(!engine.begin(tooLong.c_str()), "URL at the storage bound is rejected");
  check(transport.checkCalls == 0, "invalid requests never check the transport");
  check(transport.abortCalls == 0, "invalid requests never abort an unopened transport");

  return g_Failures == before;
}

bool testCheckAndResumeFailuresAbort() {
  std::cout << "test: check and resume validation failures abort safely\n";
  const int before = g_Failures;

  MockTransport checkFailure;
  checkFailure.checkSucceeds = false;
  OTA::Engine first(checkFailure);
  check(first.begin("source"), "check failure case begins");
  check(!first.step(), "failed check returns false");
  check(first.snapshot().error == OTA::Error::CheckFailed, "failed check reports check_failed");
  check(checkFailure.abortCalls == 1, "failed check aborts the transport");

  MockTransport invalidResume;
  invalidResume.totalBytes = 40;
  OTA::Engine second(invalidResume);
  check(second.begin("source", 41), "resume validation is deferred until size is known");
  check(!second.step(), "resume beyond the image is rejected");
  check(second.snapshot().error == OTA::Error::InvalidResume,
        "oversized checkpoint reports invalid_resume");
  check(invalidResume.abortCalls == 1, "invalid resume aborts the transport");

  return g_Failures == before;
}

bool testDownloadInvariants() {
  std::cout << "test: inconsistent transport progress is rejected\n";
  const int before = g_Failures;

  const auto expectDownloadFailure = [](std::vector<Poll> plan, const char *message) {
    MockTransport transport;
    transport.plan = std::move(plan);
    OTA::Engine engine(transport);
    check(engine.begin("source"), "invariant case begins");
    check(engine.step(), "invariant case completes checking");
    while (engine.busy()) {
      if (!engine.step()) {
        break;
      }
    }
    check(engine.snapshot().error == OTA::Error::DownloadFailed, message);
    check(transport.abortCalls == 1, "invariant failure aborts the transport");
  };

  expectDownloadFailure(
      {
          {OTA::DownloadResult::InProgress, 50},
          {OTA::DownloadResult::InProgress, 49}
  },
      "regressing byte count fails");
  expectDownloadFailure(
      {
          {OTA::DownloadResult::InProgress, 50, 101}
  },
      "changing total size fails");
  expectDownloadFailure(
      {
          {OTA::DownloadResult::InProgress, 101}
  },
      "byte count beyond total fails");
  expectDownloadFailure(
      {
          {OTA::DownloadResult::Complete, 99}
  },
      "early completion fails");

  return g_Failures == before;
}

bool testUnknownSizeAndApplyFailure() {
  std::cout << "test: unknown sizes and apply failures remain deterministic\n";
  const int before = g_Failures;

  MockTransport unknownSize;
  unknownSize.totalBytes = 0;
  unknownSize.plan = {
      {OTA::DownloadResult::InProgress, 25,  100},
      {OTA::DownloadResult::Complete,   100, 100},
  };
  OTA::Engine first(unknownSize);
  check(first.begin("source"), "unknown-size case begins");
  check(runToTerminal(first), "unknown-size case terminates");
  check(first.snapshot().state == OTA::State::Done, "transport may reveal size during download");
  check(first.snapshot().totalBytes == 100, "revealed size is retained");

  MockTransport applyFailure;
  applyFailure.applySucceeds = false;
  applyFailure.plan = {
      {OTA::DownloadResult::Complete, 100}
  };
  OTA::Engine second(applyFailure);
  check(second.begin("source"), "apply failure case begins");
  check(runToTerminal(second), "apply failure reaches a terminal state");
  check(second.snapshot().error == OTA::Error::ApplyFailed, "apply failure is reported");
  check(applyFailure.abortCalls == 1, "apply failure aborts the transport");

  MockTransport idleAbort;
  OTA::Engine third(idleAbort);
  third.abort();
  check(third.snapshot().state == OTA::State::Idle, "idle abort is a no-op");
  check(idleAbort.abortCalls == 0, "idle abort does not touch the transport");

  return g_Failures == before;
}

}  // namespace

int main() {
  testCleanSuccessAndBusyRejection();
  testMidDownloadFailure();
  testVerificationFailure();
  testProgressIsMonotonic();
  testResumeFromCheckpoint();
  testAbortPreservesCheckpoint();
  testInvalidRequestsNeverReachTransport();
  testCheckAndResumeFailuresAbort();
  testDownloadInvariants();
  testUnknownSizeAndApplyFailure();

  if (g_Failures != 0) {
    std::cout << "ota state machine harness: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "ota state machine harness: PASS\n";
  return 0;
}
