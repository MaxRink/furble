// Host checks for the memory pressure governor policy.

#include <cstdio>

#include "FurbleMemoryPressure.h"

using namespace Furble::MemoryPressure;

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

level_t feed(Governor &governor,
             int ticks,
             size_t freeBytes,
             size_t largest,
             size_t mqttExtra = 0) {
  level_t level = governor.level();
  for (int i = 0; i < ticks; i++) {
    level = governor.update(freeBytes, largest, mqttExtra);
  }
  return level;
}

}  // namespace

int main() {
  check(classify(40000, 30000) == NORMAL, "a healthy reading is normal");
  check(classify(12287, 30000) == AMBER, "free below the amber floor is amber");
  check(classify(40000, 8191) == AMBER, "largest below the amber floor is amber");
  check(classify(8191, 30000) == RED, "free below the red floor is red");
  check(classify(40000, 6143) == RED, "largest below the red floor is red");
  check(pausesWebUI(AMBER) && !pausesMQTT(AMBER), "amber pauses only the WebUI");
  check(pausesWebUI(RED) && pausesMQTT(RED), "red pauses the WebUI and MQTT");
  check(!pausesWebUI(NORMAL) && !pausesMQTT(NORMAL), "normal pauses nothing");

  Governor governor;
  // One low reading is a spike, not pressure.
  check(feed(governor, ENTER_TICKS - 1, 10000, 30000) == NORMAL, "a short dip does not shed");
  check(feed(governor, 1, 40000, 30000) == NORMAL, "a recovery resets the enter count");
  check(feed(governor, ENTER_TICKS, 10000, 30000) == AMBER, "a sustained dip enters amber");
  check(feed(governor, ENTER_TICKS, 6000, 30000) == RED, "a deeper dip enters red");

  // Recovery steps one level at a time and needs room for the restored
  // service, so re-admitting it cannot push the heap straight back down.
  const size_t redExit = ENTER[RED].freeBytes + RESTORE_BYTES[RED];
  check(feed(governor, EXIT_TICKS * 2, redExit - 1, 30000) == RED,
        "red holds until MQTT fits again");
  check(feed(governor, EXIT_TICKS, redExit, 30000) == AMBER, "red steps up to amber");
  // A client whose queue and incoming copy are still missing and will be
  // internal (no PSRAM, or PSRAM too fragmented) reports that storage. RED
  // then holds at the plain exit and clears only where that first mqtts://
  // start passes its own gate, so a refused start cannot spend the retries.
  {
    constexpr size_t extra = Furble::MQTTMemory::EVENT_STORAGE_BYTES;
    Governor internalQueue;
    feed(internalQueue, ENTER_TICKS, 6000, 30000);
    check(feed(internalQueue, EXIT_TICKS * 2, redExit, 30000, extra) == RED,
          "red holds while the missing internal queue does not fit");
    check(feed(internalQueue, EXIT_TICKS, redExit + extra, 30000, extra) == AMBER,
          "red clears once the internal queue fits too");
    check(Furble::MQTTMemory::startGate(redExit + extra, 30000, true, extra,
                                        Furble::MQTTMemory::EVENT_QUEUE_BLOCK_BYTES)
              .open,
          "a first TLS start with an internal queue fits at the red exit");
  }
  const size_t amberExit = ENTER[AMBER].freeBytes + RESTORE_BYTES[AMBER];
  check(feed(governor, EXIT_TICKS - 1, amberExit, 30000) == AMBER,
        "amber waits for a sustained recovery");
  check(feed(governor, 1, amberExit, 30000) == NORMAL, "amber returns to normal");
  // The measured post-#394 StickS3 WebUI session is healthy, and a camera
  // connect after the WebUI is paused and released lets it return.
  check(classify(16087, 14324) == NORMAL, "a running StickS3 WebUI is not pressure");
  // Paused with the supervisor still running: 16,087 + the 8,192-byte server.
  check(amberExit <= 16087 + 8192, "the WebUI returns once a camera disconnects");

  // A largest-block collapse alone is pressure too.
  Governor fragmented;
  check(feed(fragmented, ENTER_TICKS, 60000, 5000) == RED, "fragmentation alone enters red");
  check(feed(fragmented, EXIT_TICKS, 60000, ENTER[RED].largestBlock + 4095) == RED,
        "red holds until the largest block clears its margin");

  if (failures != 0) {
    std::printf("%d memory pressure checks failed\n", failures);
    return 1;
  }
  std::printf("memory pressure checks passed\n");
  return 0;
}
