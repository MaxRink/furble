// Host coverage for the HTTPS delivery adapter behind OTA::Transport.
//
// The client double scripts open, every download poll and the verification
// outcome, so a complete update, a wrong size, a bad digest or signature, an
// interrupted transfer with a resumed retry, and an apply failure all run
// without a socket, a TLS stack or a flash partition.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "FurbleOTA.h"
#include "FurbleOTAHTTPS.h"

namespace OTA = Furble::OTA;
namespace HTTPS = Furble::OTA::HTTPS;

namespace {

int g_Failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
}

struct Step {
  bool failed = false;
  bool complete = false;
  size_t bytes = 0;
};

class ScriptedClient final: public HTTPS::Client {
 public:
  size_t imageSize = 1000;
  size_t slotCapacity = 4096;
  bool openSucceeds = true;
  Verify verifyResult = Verify::Ok;
  bool applySucceeds = true;
  std::vector<Step> plan;

  size_t openCalls = 0;
  size_t pollCalls = 0;
  size_t finishCalls = 0;
  size_t applyCalls = 0;
  size_t discardCalls = 0;
  std::vector<size_t> offsets;
  std::string lastUrl;

  bool open(const char *url, size_t resumeOffset, Open &result) override {
    openCalls++;
    offsets.push_back(resumeOffset);
    lastUrl = (url == nullptr) ? "" : url;
    result.imageSize = imageSize;
    result.slotCapacity = slotCapacity;
    planIndex = 0;
    return openSucceeds;
  }

  Poll poll() override {
    pollCalls++;
    Poll result;
    if (planIndex >= plan.size()) {
      result.failed = true;
      return result;
    }
    const Step step = plan[planIndex++];
    result.failed = step.failed;
    result.complete = step.complete;
    result.bytesRetrieved = step.bytes;
    return result;
  }

  Verify finish() override {
    finishCalls++;
    return verifyResult;
  }

  bool apply() override {
    applyCalls++;
    return applySucceeds;
  }

  void discard() override { discardCalls++; }

 private:
  size_t planIndex = 0;
};

void runToCompletion(OTA::Engine &engine) {
  for (size_t guard = 0; (guard < 64) && engine.busy(); guard++) {
    engine.step();
  }
}

void testCompleteUpdate(void) {
  ScriptedClient client;
  client.plan = {
      {false, false, 250 },
      {false, false, 700 },
      {false, true,  1000}
  };
  HTTPS::Adapter adapter(client);
  OTA::Engine engine(adapter);

  check(engine.begin("https://updates.example/furble.bin"), "the update starts");
  check(engine.snapshot().state == OTA::State::Checking, "the engine starts by checking");
  runToCompletion(engine);

  check(engine.snapshot().state == OTA::State::Done, "the complete update reaches done");
  check(engine.snapshot().progress == 100, "the complete update reports 100 percent");
  check(engine.snapshot().totalBytes == 1000, "the announced image length is the total");
  check(engine.snapshot().bytesDownloaded == 1000, "every byte is accounted for");
  check(client.lastUrl == "https://updates.example/furble.bin", "the URL reaches the client");
  check(client.openCalls == 1, "the client opens once");
  check(client.pollCalls == 3, "the client polls once per chunk");
  check(client.finishCalls == 1, "the image is verified once");
  check(client.applyCalls == 1, "the image is applied once");
  check(client.discardCalls == 0, "a clean update is not discarded");
  check(adapter.reason() == HTTPS::Reason::None, "a clean update reasons none");
}

void testSchemeAndSizePolicy(void) {
  {
    ScriptedClient client;
    HTTPS::Adapter adapter(client);
    OTA::Engine engine(adapter);

    check(engine.begin("http://updates.example/furble.bin"), "an http request begins");
    runToCompletion(engine);
    check(engine.snapshot().state == OTA::State::Error, "an http URL fails the check");
    check(engine.lastError() == OTA::Error::CheckFailed, "the engine reports check_failed");
    check(adapter.reason() == HTTPS::Reason::UrlNotHttps, "the adapter names the scheme rule");
    check(client.openCalls == 0, "a non-https URL never reaches the client");
  }

  {
    ScriptedClient client;
    HTTPS::Adapter adapter(client);
    OTA::Engine engine(adapter);

    check(engine.begin("https://"), "an empty authority begins");
    runToCompletion(engine);
    check(adapter.reason() == HTTPS::Reason::UrlNotHttps, "an empty authority is not https");
    check(client.openCalls == 0, "an empty authority never reaches the client");
  }

  {
    ScriptedClient client;
    client.imageSize = 0;
    HTTPS::Adapter adapter(client);
    OTA::Engine engine(adapter);

    check(engine.begin("https://updates.example/furble.bin"), "an unknown size begins");
    runToCompletion(engine);
    check(adapter.reason() == HTTPS::Reason::ImageSizeInvalid,
          "an unknown image length is refused");
    check(client.discardCalls == 1, "a refused image is discarded");
  }

  {
    ScriptedClient client;
    client.imageSize = 5000;
    client.slotCapacity = 4096;
    HTTPS::Adapter adapter(client);
    OTA::Engine engine(adapter);

    check(engine.begin("https://updates.example/furble.bin"), "an oversized image begins");
    runToCompletion(engine);
    check(adapter.reason() == HTTPS::Reason::SlotTooSmall, "an image past the slot is refused");
    check(client.discardCalls == 1, "an oversized image is discarded");
  }

  {
    ScriptedClient client;
    client.openSucceeds = false;
    HTTPS::Adapter adapter(client);
    OTA::Engine engine(adapter);

    check(engine.begin("https://updates.example/furble.bin"), "an unreachable source begins");
    runToCompletion(engine);
    check(adapter.reason() == HTTPS::Reason::SourceUnreachable, "a refused connection is named");
  }
}

void testWrongSize(void) {
  ScriptedClient client;
  client.plan = {
      {false, false, 400},
      {false, true,  900}
  };
  HTTPS::Adapter adapter(client);
  OTA::Engine engine(adapter);

  check(engine.begin("https://updates.example/furble.bin"), "a short image begins");
  runToCompletion(engine);
  check(engine.snapshot().state == OTA::State::Error, "a short image fails");
  check(engine.lastError() == OTA::Error::DownloadFailed, "the engine reports download_failed");
  check(adapter.reason() == HTTPS::Reason::IncompleteTransfer,
        "a completion before the announced length is named");
  check(adapter.reason() != HTTPS::Reason::None, "the status detail is not none");
  check(client.discardCalls == 1, "the short image is discarded");
}

void testVerifyFailures(void) {
  const HTTPS::Client::Verify outcomes[] = {
      HTTPS::Client::Verify::SizeMismatch, HTTPS::Client::Verify::DigestMismatch,
      HTTPS::Client::Verify::SignatureInvalid, HTTPS::Client::Verify::ImageInvalid};
  const HTTPS::Reason expected[] = {HTTPS::Reason::SizeMismatch, HTTPS::Reason::DigestMismatch,
                                    HTTPS::Reason::SignatureInvalid, HTTPS::Reason::ImageInvalid};
  const char *names[] = {"a size mismatch", "a digest mismatch", "an invalid signature",
                         "an invalid image"};

  for (size_t i = 0; i < 4; i++) {
    ScriptedClient client;
    client.plan = {
        {false, true, 1000}
    };
    client.verifyResult = outcomes[i];
    HTTPS::Adapter adapter(client);
    OTA::Engine engine(adapter);

    check(engine.begin("https://updates.example/furble.bin"), "a failing verify begins");
    runToCompletion(engine);
    check(engine.snapshot().state == OTA::State::Error, names[i]);
    check(engine.lastError() == OTA::Error::VerificationFailed, "the engine reports verify_failed");
    check(adapter.reason() == expected[i], "the adapter names the verify failure");
    check(client.applyCalls == 0, "a failed verify never applies");
    check(client.discardCalls == 1, "a failed verify discards the image");
  }
}

void testInterruptedTransferAndResume(void) {
  ScriptedClient client;
  client.plan = {
      {false, false, 400},
      {true,  false, 0  }
  };
  HTTPS::Adapter adapter(client);
  OTA::Engine engine(adapter);

  check(engine.begin("https://updates.example/furble.bin"), "the first attempt begins");
  runToCompletion(engine);
  check(engine.snapshot().state == OTA::State::Error, "an interrupted transfer fails");
  check(engine.lastError() == OTA::Error::DownloadFailed, "the engine reports download_failed");
  check(adapter.reason() == HTTPS::Reason::TransferInterrupted,
        "the adapter names the interruption");
  check(engine.snapshot().bytesDownloaded == 400, "the confirmed checkpoint is kept");
  check(client.discardCalls == 1, "the interrupted image is discarded");

  client.plan = {
      {false, false, 800 },
      {false, true,  1000}
  };
  check(engine.begin("https://updates.example/furble.bin", engine.snapshot().bytesDownloaded),
        "the retry begins from the checkpoint");
  runToCompletion(engine);
  check(engine.snapshot().state == OTA::State::Done, "the resumed transfer completes");
  check(client.offsets.size() == 2, "the client opened twice");
  check(client.offsets[1] == 400, "the retry passes the checkpoint as the range offset");
  check(adapter.reason() == HTTPS::Reason::None, "the resumed transfer reasons none");
}

void testProgressInvariants(void) {
  {
    ScriptedClient client;
    client.plan = {
        {false, false, 400},
        {false, false, 200}
    };
    HTTPS::Adapter adapter(client);
    OTA::Engine engine(adapter);

    check(engine.begin("https://updates.example/furble.bin"), "a regressing transfer begins");
    runToCompletion(engine);
    check(adapter.reason() == HTTPS::Reason::TransferInterrupted,
          "a regressing byte count is refused");
    check(engine.snapshot().state == OTA::State::Error, "a regressing transfer fails");
  }

  {
    ScriptedClient client;
    client.plan = {
        {false, true, 1200}
    };
    HTTPS::Adapter adapter(client);
    OTA::Engine engine(adapter);

    check(engine.begin("https://updates.example/furble.bin"), "an oversized transfer begins");
    runToCompletion(engine);
    check(adapter.reason() == HTTPS::Reason::SizeMismatch,
          "a byte count past the announced length is refused");
  }
}

void testApplyFailure(void) {
  ScriptedClient client;
  client.plan = {
      {false, true, 1000}
  };
  client.applySucceeds = false;
  HTTPS::Adapter adapter(client);
  OTA::Engine engine(adapter);

  check(engine.begin("https://updates.example/furble.bin"), "an apply failure begins");
  runToCompletion(engine);
  check(engine.snapshot().state == OTA::State::Error, "a failed apply fails");
  check(engine.lastError() == OTA::Error::ApplyFailed, "the engine reports apply_failed");
  check(adapter.reason() == HTTPS::Reason::ApplyFailed, "the adapter names the apply failure");
  check(client.discardCalls == 1, "a failed apply discards the staged image");
}

void testAbortIsIdempotent(void) {
  ScriptedClient client;
  client.plan = {
      {false, false, 400},
      {false, false, 800}
  };
  HTTPS::Adapter adapter(client);
  OTA::Engine engine(adapter);

  check(engine.begin("https://updates.example/furble.bin"), "the aborted update begins");
  engine.step();
  check(engine.snapshot().state == OTA::State::Downloading, "the update is downloading");
  engine.abort();
  check(engine.snapshot().state == OTA::State::Error, "an abort fails the engine");
  check(engine.lastError() == OTA::Error::Aborted, "the engine reports aborted");
  check(adapter.reason() == HTTPS::Reason::Aborted, "the adapter names the abort");
  check(client.discardCalls == 1, "the abort discards the transfer once");

  adapter.abort();
  check(client.discardCalls == 1, "a second abort is idempotent");

  const OTA::DownloadProgress progress = adapter.download();
  check(progress.result == OTA::DownloadResult::Failed, "a download after abort fails closed");
}

void testReasonNames(void) {
  const struct {
    HTTPS::Reason reason;
    const char *name;
  } cases[] = {
      {HTTPS::Reason::None,                "none"                },
      {HTTPS::Reason::UrlNotHttps,         "url_not_https"       },
      {HTTPS::Reason::UrlMalformed,        "url_malformed"       },
      {HTTPS::Reason::SourceUnreachable,   "source_unreachable"  },
      {HTTPS::Reason::ImageSizeInvalid,    "image_size_invalid"  },
      {HTTPS::Reason::SlotTooSmall,        "slot_too_small"      },
      {HTTPS::Reason::TransferInterrupted, "transfer_interrupted"},
      {HTTPS::Reason::IncompleteTransfer,  "incomplete_transfer" },
      {HTTPS::Reason::SizeMismatch,        "size_mismatch"       },
      {HTTPS::Reason::DigestMismatch,      "digest_mismatch"     },
      {HTTPS::Reason::SignatureInvalid,    "signature_invalid"   },
      {HTTPS::Reason::ImageInvalid,        "image_invalid"       },
      {HTTPS::Reason::ApplyFailed,         "apply_failed"        },
      {HTTPS::Reason::Aborted,             "aborted"             },
  };
  for (const auto &entry : cases) {
    check(std::string(HTTPS::reasonName(entry.reason)) == entry.name,
          "every HTTPS reason has a stable name");
  }
}

}  // namespace

int main() {
  testCompleteUpdate();
  testSchemeAndSizePolicy();
  testWrongSize();
  testVerifyFailures();
  testInterruptedTransferAndResume();
  testProgressInvariants();
  testApplyFailure();
  testAbortIsIdempotent();
  testReasonNames();

  if (g_Failures != 0) {
    std::cout << "ota https adapter harness: FAIL (" << g_Failures << " checks)\n";
    return 1;
  }
  std::cout << "ota https adapter harness: PASS\n";
  return 0;
}
