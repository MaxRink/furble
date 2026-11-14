#ifndef FURBLE_MEMORY_PRESSURE_H
#define FURBLE_MEMORY_PRESSURE_H

#include <cstddef>
#include <cstdint>

#include "FurbleMQTTMemory.h"
#include "FurbleTaskMemory.h"
#include "FurbleWebUIMemory.h"

namespace Furble {
namespace MemoryPressure {

/**
 * Runtime load shedding for internal RAM.
 *
 * Every board aborts on a failed allocation, so the device survives memory
 * pressure only by never reaching exhaustion. Start gates cover a service's
 * first allocation; this governor covers the RAM a running service keeps
 * taking. It pauses optional services in a fixed order and lets them return
 * through their own start gates once the heap recovers. A pause is runtime
 * state only and never writes NVS.
 *
 * The NimBLE host, Control, the UI, Settings, CrashLog, GPS, WiFi and the
 * companion are never shed. WiFi's driver pools are allocated once at start
 * and do not grow, and the companion keeps its task and GATT server across a
 * disable, so pausing either would free little.
 *
 * The thresholds are derived, not measured. Hardware readings of a healthy
 * WebUI and MQTT session are pending with the rest of hardware validation
 * (plans/313-sim-only-status.md).
 */
enum level_t : uint8_t {
  NORMAL = 0,
  /** WebUI server paused. */
  AMBER = 1,
  /** WebUI and the MQTT client paused. */
  RED = 2,
};

/** Internal 8-bit free and largest-block thresholds for each level. */
struct threshold_t {
  size_t freeBytes;
  size_t largestBlock;
};

/**
 * A level is entered when free or largest drops below its threshold. The
 * thresholds keep room for the core path, not for comfort: a camera connect
 * creates a 4,096-byte `Control::Target` task (stack, TCB and allocator
 * overhead need a block of about 4.6 KiB) and a NimBLE client. RED keeps that
 * block and one more connect's worth of free RAM; AMBER keeps a second.
 * A healthy post-#394 StickS3 runs the WebUI at 16,087 free / 14,324 largest
 * (`hw-memory-webui-post394`), above AMBER.
 */
inline constexpr threshold_t ENTER[] = {
    {0,     0   }, // NORMAL
    {12288, 8192}, // AMBER
    {8192,  6144}, // RED
};

/**
 * Internal RAM the service a level paused takes back when it is re-admitted.
 * A level is left only when free clears its threshold by this much, so the
 * re-admitted service cannot push the heap straight back below it:
 * - AMBER, WebUI server: `WebUIMemory::HTTPD_STACK_BYTES`, the 8 KiB HTTPS
 *   task. The 3 KiB supervisor keeps running while the server is paused.
 * - RED, MQTT client: `MQTTMemory::START_TLS_INTERNAL_FREE_FLOOR`. The
 *   `mqtts://` start floor covers the 6 KiB TLS worker, so a returning secure
 *   client cannot push free back under RED. A plain client needs less and only
 *   recovers later. When the client's event queue or incoming copy is still
 *   missing and will be internal (no PSRAM, or PSRAM too fragmented), its next
 *   start needs those bytes too. MQTT reports them through
 *   `setMQTTStartExtra()` from the same calculation its start gate uses, and
 *   RED adds them. Otherwise RED could clear inside the start gate's floor and
 *   each refused start would spend the retry budget until MQTT blocks.
 * Each service still passes its own start gate when it returns.
 */
inline constexpr size_t RESTORE_BYTES[] = {
    0,
    WebUIMemory::HTTPD_STACK_BYTES,
    MQTTMemory::START_TLS_INTERNAL_FREE_FLOOR,
};

/** Largest-block margin above a level's threshold before it is left. */
inline constexpr size_t LARGEST_HYSTERESIS_BYTES = 4096;

/** Consecutive one-second readings below a threshold before shedding. */
inline constexpr uint8_t ENTER_TICKS = 3;

/** Consecutive readings above the exit threshold before re-admitting. */
inline constexpr uint8_t EXIT_TICKS = 5;

/** The deepest level a reading qualifies for, without hysteresis. */
inline constexpr level_t classify(size_t freeBytes, size_t largestBlock) {
  for (uint8_t level = RED; level > NORMAL; level--) {
    if ((freeBytes < ENTER[level].freeBytes) || (largestBlock < ENTER[level].largestBlock)) {
      return static_cast<level_t>(level);
    }
  }
  return NORMAL;
}

/**
 * Internal RAM one camera connect takes at once: the `Control::Target` stack
 * that `Control::addActive()` creates, plus the TCB and block overhead.
 */
inline constexpr size_t CONNECT_TARGET_BYTES =
    TaskMemory::CAMERA_TARGET_STACK_BYTES + TaskMemory::TASK_OVERHEAD_BYTES;

/**
 * The level a reading reaches after a core-path allocation of bytes. The
 * largest block shrinks by the same amount, the worst case when the
 * allocation lands in it.
 */
inline constexpr level_t classifyAfter(size_t freeBytes, size_t largestBlock, size_t bytes) {
  return classify((freeBytes > bytes) ? (freeBytes - bytes) : 0,
                  (largestBlock > bytes) ? (largestBlock - bytes) : 0);
}

/**
 * True when a reading leaves room to re-admit the service level paused.
 * mqttExtra is the internal storage the MQTT client's next start still has to
 * allocate; it only applies to RED.
 */
inline constexpr bool clears(level_t level,
                             size_t freeBytes,
                             size_t largestBlock,
                             size_t mqttExtra = 0) {
  if (level > RED) {
    return false;
  }
  const size_t extra = (level == RED) ? mqttExtra : 0;
  return (freeBytes >= ENTER[level].freeBytes + RESTORE_BYTES[level] + extra)
         && (largestBlock >= ENTER[level].largestBlock + LARGEST_HYSTERESIS_BYTES);
}

/**
 * The debounced level. One step per transition: a deeper reading must hold
 * for ENTER_TICKS, and a recovery steps up one level after EXIT_TICKS clear
 * readings, so services come back one at a time.
 */
class Governor {
 public:
  level_t update(size_t freeBytes, size_t largestBlock, size_t mqttExtra = 0) {
    const level_t raw = classify(freeBytes, largestBlock);
    if (raw > m_Level) {
      m_Exit = 0;
      if (++m_Enter >= ENTER_TICKS) {
        m_Enter = 0;
        m_Level = raw;
      }
    } else if ((m_Level > NORMAL) && clears(m_Level, freeBytes, largestBlock, mqttExtra)) {
      m_Enter = 0;
      if (++m_Exit >= EXIT_TICKS) {
        m_Exit = 0;
        m_Level = static_cast<level_t>(m_Level - 1);
      }
    } else {
      m_Enter = 0;
      m_Exit = 0;
    }
    return m_Level;
  }

  /**
   * Enter level at once, without the debounce. Never lowers the level; the
   * exit still needs EXIT_TICKS clear readings.
   */
  level_t escalate(level_t level) {
    if (level > m_Level) {
      m_Level = level;
      m_Enter = 0;
      m_Exit = 0;
    }
    return m_Level;
  }

  level_t level(void) const { return m_Level; }

 private:
  level_t m_Level = NORMAL;
  uint8_t m_Enter = 0;
  uint8_t m_Exit = 0;
};

/** Run one governor step from the UI loop; reads the heap at most once a second. */
void tick(void);

/**
 * Enter a deeper level before a core-path allocation of bytes. When the
 * allocation would take the heap into AMBER or RED, that level is entered now
 * instead of three readings later, and the escalate hook wakes the services
 * that act on it. A connect calls this with CONNECT_TARGET_BYTES before each
 * `Control::addActive()`. The target task is created right after it returns,
 * so a woken WebUI server stops shortly after the target task exists. Nothing
 * orders that stop against the NimBLE client or the connected page's first
 * draw. Safe from any task.
 */
void admit(size_t bytes);

typedef void (*escalate_hook_t)(void);

/**
 * Called by admit() after it raised the level, outside the governor lock.
 * The WebUI registers a wake for its supervisor, which otherwise sees the new
 * level only on its next 500 ms poll. MQTT reads the level on its own loop.
 * One hook; a later call replaces it.
 */
void setEscalateHook(escalate_hook_t hook);

/**
 * Internal bytes beyond its start floor that the MQTT client's next start
 * must allocate. The MQTT owner sets it from its start gate inputs; zero once
 * the queue and incoming copy exist or will be in PSRAM. Safe from any task.
 */
void setMQTTStartExtra(size_t bytes);

/** The current level, safe from any task. */
level_t current(void);

/** True when level pauses the service, for each service's own run decision. */
inline constexpr bool pausesWebUI(level_t level) {
  return level >= AMBER;
}
inline constexpr bool pausesMQTT(level_t level) {
  return level >= RED;
}

const char *levelName(level_t level);

}  // namespace MemoryPressure
}  // namespace Furble

#endif
