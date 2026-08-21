#include <algorithm>
#include <array>
#include <utility>

#include "Device.h"

#include "FurbleControl.h"
#include "FurblePlatform.h"
#include "FurblePower.h"
#include "FurbleSettings.h"

namespace Furble {

namespace {

constexpr uint32_t RSSI_SAMPLE_INTERVAL_MS = 5000;
constexpr float RSSI_FILTER_ALPHA = 0.25f;
constexpr int8_t RSSI_STEP_DOWN_DBM = -60;
constexpr int8_t RSSI_STEP_UP_DBM = -80;
constexpr uint8_t RSSI_STEP_DOWN_SAMPLES = 3;
constexpr uint8_t RSSI_STEP_UP_SAMPLES = 2;

constexpr std::array<esp_power_level_t, 3> POWER_LEVELS = {
    ESP_PWR_LVL_P3,
    ESP_PWR_LVL_P6,
    ESP_PWR_LVL_P9,
};

int powerIndex(esp_power_level_t power) {
  for (size_t i = 0; i < POWER_LEVELS.size(); i++) {
    if (POWER_LEVELS[i] == power) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

const char *stateName(Control::state_t state) {
  switch (state) {
    case Control::STATE_IDLE:
      return "idle";
    case Control::STATE_CONNECT:
      return "connect";
    case Control::STATE_CONNECTING:
      return "connecting";
    case Control::STATE_CONNECT_FAILED:
      return "connect_failed";
    case Control::STATE_ACTIVE:
      return "active";
    case Control::STATE_DISCONNECTING:
      return "disconnecting";
  }
  return "unknown";
}

}  // namespace

Control::Target::Target(std::shared_ptr<Camera> camera) : m_Camera(std::move(camera)) {
  m_Queue = xQueueCreate(m_QueueLength, sizeof(cmd_t));
}

Control::Target::~Target() {
  vQueueDelete(m_Queue);
  m_Queue = NULL;
  if (!m_Stopped) {
    m_Camera->disconnect();
  }
  // Drop this target's strong reference. The Camera is freed here only if no
  // other reference (the list, an in-flight connect) still holds it.
  m_Camera = nullptr;
}

std::shared_ptr<Camera> Control::Target::getCamera(void) const {
  return m_Camera;
}

void Control::Target::sendCommand(cmd_t cmd) {
  if (cmd == CMD_DISCONNECT) {
    // CMD_DISCONNECT must never be dropped. Losing it strands the target task in
    // its command loop and hangs disconnect(). The transient commands still in
    // the queue (shutter, focus, GPS) are moot once we are tearing down, so
    // clear the queue and place the disconnect at the front for immediate
    // delivery. Reset guarantees space, so this never blocks the caller and
    // never fails on a full queue.
    xQueueReset(m_Queue);
    xQueueSendToFront(m_Queue, &cmd, 0);
    return;
  }

  BaseType_t ret = xQueueSend(m_Queue, &cmd, 0);
  if (ret != pdTRUE) {
    ESP_LOGE(LOG_TAG, "Failed to send command to target.");
  }
}

Control::cmd_t Control::Target::getCommand(void) {
  cmd_t cmd = CMD_ERROR;
  BaseType_t ret = xQueueReceive(m_Queue, &cmd, pdMS_TO_TICKS(50));
  if (ret != pdTRUE) {
    return CMD_ERROR;
  }
  return cmd;
}

void Control::Target::updateGPS(const Camera::gps_t &gps, const Camera::timesync_t &timesync) {
  m_GPS = gps;
  m_Timesync = timesync;
}

void Control::Target::task(void) {
  const char *name = m_Camera->getName().c_str();

  while (true) {
    cmd_t cmd = this->getCommand();
    switch (cmd) {
      case CMD_SHUTTER_PRESS:
        m_Camera->noteConnActivity(true);
        // Request the fast profile but do not wait for it. A central-initiated
        // update applies a few connection events after the request, so this
        // press still goes out at the current (possibly idle) interval. The
        // fast profile serves the follow-up commands.
        m_Camera->setConnProfile(Camera::ConnProfile::FAST);
        ESP_LOGI(LOG_TAG, "shutterPress(%s)", name);
        m_Camera->shutterPress();
        break;
      case CMD_SHUTTER_RELEASE:
        m_Camera->noteConnActivity(false);
        ESP_LOGI(LOG_TAG, "shutterRelease(%s)", name);
        m_Camera->shutterRelease();
        break;
      case CMD_FOCUS_PRESS:
        m_Camera->noteConnActivity(true);
        // Same fire-and-forget fast request as CMD_SHUTTER_PRESS.
        m_Camera->setConnProfile(Camera::ConnProfile::FAST);
        ESP_LOGI(LOG_TAG, "focusPress(%s)", name);
        m_Camera->focusPress();
        break;
      case CMD_FOCUS_RELEASE:
        m_Camera->noteConnActivity(false);
        ESP_LOGI(LOG_TAG, "focusRelease(%s)", name);
        m_Camera->focusRelease();
        break;
      case CMD_GPS_UPDATE:
        ESP_LOGI(LOG_TAG, "updateGeoData(%s)", name);
        m_Camera->updateGeoData(m_GPS, m_Timesync);
        break;
      case CMD_DISCONNECT:
        m_Camera->setActive(false);
        m_Camera->disconnect();
        goto task_exit;
      case CMD_ERROR:
        // Not an error: getCommand() returns CMD_ERROR when the 50 ms queue
        // wait times out. Reuse that tick as the idle profile timer and the
        // connection statistics sampler (both are internally rate limited).
        m_Camera->maybeSetIdle();
        m_Camera->updateConnStats();
        break;
      default:
        ESP_LOGE(LOG_TAG, "Invalid control command %d.", cmd);
    }
  }
task_exit:
  m_Stopped = true;
  vTaskDelete(NULL);
}

Control &Control::getInstance(void) {
  static Control instance;
  if (instance.m_Queue == NULL) {
    instance.m_Queue = xQueueCreate(m_QueueLength, sizeof(cmd_t));
    if (instance.m_Queue == NULL) {
      ESP_LOGE(LOG_TAG, "Failed to create control queue.");
      abort();
    }

    // First call runs after Settings::init() in app_main. Load the user
    // transmit power cap so boot matches Device::init() instead of pinning
    // the compile-time default.
    instance.m_Power = Settings::load<esp_power_level_t>(Settings::TX_POWER);
    instance.m_AdaptivePower = instance.m_Power;
  }

  return instance;
}

Control::state_t Control::connectAll(void) {
  static uint32_t failcount = 0;
  uint32_t timeout = m_InfiniteReconnect ? TIMEOUT_INFINITE_MS : TIMEOUT_DEFAULT_MS;
  std::vector<std::shared_ptr<Camera>> cameras;
  std::vector<std::shared_ptr<Camera>> all;

  const bool connSaver = Settings::load<Settings::CONN_SAVER>();

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);

    // Snapshot cameras so the mutex is not held during connection attempts. The
    // snapshot holds strong references, so a camera stays alive through the
    // unlocked connect even if disconnect() clears its target meanwhile.
    for (const auto &target : m_Targets) {
      auto camera = target->getCamera();
      all.push_back(camera);
      if (!camera->isConnected()) {
        cameras.push_back(camera);
      }
    }
    m_ConnectInProgress = true;
  }

  ESP_LOGD(LOG_TAG,
           "connectAll: %u target(s), %u to connect, reconnect attempt %lu, timeout %lu ms",
           static_cast<unsigned>(all.size()), static_cast<unsigned>(cameras.size()),
           static_cast<unsigned long>(m_ReconnectAttempt), static_cast<unsigned long>(timeout));

  // Apply the setting outside the mutex. On an already connected camera this
  // enters NimBLE and can block on the HCI transport.
  for (const auto &camera : all) {
    camera->setConnSaverEnabled(connSaver);
  }

  for (const auto &camera : cameras) {
    if (m_ConnectAbort) {
      break;
    }

    m_ConnectCamera = camera;
    if (!camera->connect(m_Power, timeout)) {
      failcount++;
      break;
    } else {
      m_ConnectCamera = nullptr;
    }
  }

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_ConnectInProgress = false;

    if (m_ConnectAbort || m_State == STATE_DISCONNECTING) {
      m_ConnectCamera = nullptr;
      return m_State;
    }

    if (allConnected()) {
      failcount = 0;
      m_ReconnectAttempt = 0;
      m_ReconnectHintLogged = false;
      return STATE_ACTIVE;
    }
  }

  if (m_InfiniteReconnect || (failcount < 2)) {
    if (m_InfiniteReconnect) {
      const uint32_t delay = ReconnectBackoff::delayMs(m_ReconnectAttempt, m_ReconnectBackoff);

      if (m_ReconnectAttempt == 0 && !m_ReconnectHintLogged) {
        ESP_LOGW(LOG_TAG,
                 "Reconnect failed; camera may still hold the previous session. Retrying in "
                 "%lu ms.",
                 delay);
        m_ReconnectHintLogged = true;
      }

      ESP_LOGI(LOG_TAG, "Reconnect retry %lu, waiting %lu ms.", m_ReconnectAttempt + 1, delay);
      m_ReconnectAttempt++;

      // Sleep in short slices so disconnect can interrupt the retry wait.
      uint32_t remaining = delay;
      while (remaining > 0 && !m_ConnectAbort && m_State != STATE_DISCONNECTING) {
        const uint32_t slice = remaining < BACKOFF_SLICE_MS ? remaining : BACKOFF_SLICE_MS;
        vTaskDelay(pdMS_TO_TICKS(slice));
        remaining -= slice;
      }
    }
    return m_ConnectAbort ? m_State
                          : (m_State == STATE_DISCONNECTING ? STATE_DISCONNECTING : STATE_CONNECT);
  }

  return STATE_CONNECT_FAILED;
}

void Control::task(void) {
  while (true) {
    // Free any target that was force-completed on a disconnect timeout and whose
    // teardown task has since finished. Runs every 50 ms tick regardless of
    // state, so quarantined objects never linger and are never freed while their
    // task can still touch them.
    reapZombieTargets();

    cmd_t cmd;
    BaseType_t ret = xQueueReceive(m_Queue, &cmd, pdMS_TO_TICKS(50));

    switch (m_State) {
      case STATE_IDLE:
        if (ret == pdTRUE) {
          if (cmd == CMD_CONNECT) {
            setState(STATE_CONNECT);
            continue;
          }
        }
        break;

      case STATE_CONNECT:
        // Do not start a new connection while a prior teardown is still
        // draining. A fresh NimBLE client allocated in connectAll() would race
        // the client a quarantined target's teardown task is still releasing,
        // the connect-side use-after-free. Stay in STATE_CONNECT and retry on
        // the next tick; reapZombieTargets() above clears the drain once each
        // teardown task has stopped.
        if (teardownDraining()) {
          break;
        }
        setState(STATE_CONNECTING);
        setState(connectAll());
        break;

      case STATE_CONNECTING:
      case STATE_CONNECT_FAILED:
        break;

      case STATE_ACTIVE:
        if (!allConnected()) {
          {
            const std::lock_guard<std::mutex> lock(m_Mutex);
            resetAdaptiveState();
          }
          setState(STATE_CONNECT);
          continue;
        }

        sampleAdaptivePower();

        if (ret == pdTRUE) {
          for (const auto &target : m_Targets) {
            switch (cmd) {
              case CMD_SHUTTER_PRESS:
              case CMD_SHUTTER_RELEASE:
              case CMD_FOCUS_PRESS:
              case CMD_FOCUS_RELEASE:
              case CMD_GPS_UPDATE:
                target->sendCommand(cmd);
                break;
              default:
                ESP_LOGE(LOG_TAG, "Invalid control command %d.", cmd);
                break;
            }
          }
        }
        break;

      case STATE_DISCONNECTING:
        break;
    }
  }
}

BaseType_t Control::sendCommand(cmd_t cmd) {
  return xQueueSend(m_Queue, &cmd, 0);
}

BaseType_t Control::updateGPS(const Camera::gps_t &gps, const Camera::timesync_t &timesync) {
  for (const auto &target : m_Targets) {
    target->updateGPS(gps, timesync);
  }

  cmd_t cmd = CMD_GPS_UPDATE;
  return xQueueSend(m_Queue, &cmd, 0);
}

bool Control::allConnected(void) {
  for (const auto &target : m_Targets) {
    if (!target->getCamera()->isConnected()) {
      return false;
    }
  }

  return true;
}

std::vector<Control::Target *> Control::getTargets(void) {
  const std::lock_guard<std::mutex> lock(m_Mutex);

  std::vector<Target *> targets;
  targets.reserve(m_Targets.size());
  for (const auto &target : m_Targets) {
    targets.push_back(target.get());
  }
  return targets;
}

void Control::connectAll(bool infiniteReconnect) {
  m_InfiniteReconnect = infiniteReconnect;
  m_ReconnectBackoff = Settings::load<Settings::RECON_BACKOFF>();
  m_ReconnectAttempt = 0;
  m_ReconnectHintLogged = false;
  m_ConnectAbort = false;

  this->sendCommand(CMD_CONNECT);
}

bool Control::disconnectComplete(void) {
  std::vector<std::shared_ptr<Camera>> cameras;

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    for (const auto &target : m_Targets) {
      if (!target->m_Stopped) {
        return false;
      }
      cameras.push_back(target->getCamera());
    }
  }

  if (m_ConnectInProgress) {
    return false;
  }

  for (const auto &camera : cameras) {
    if (camera->isConnected()) {
      return false;
    }
  }

  return true;
}

bool Control::disconnect(uint32_t timeout_ms, bool forRestart) {
  m_ConnectAbort = true;
  setState(STATE_DISCONNECTING);
  m_ReconnectAttempt = 0;

  // Force cancel any active connection attempts
  ble_gap_conn_cancel();

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);

    // State only, no radio calls under m_Mutex.
    resetAdaptiveState();

    // send disconnect
    for (const auto &target : m_Targets) {
      target->sendCommand(CMD_DISCONNECT);
    }
  }

  // Interactive callers wait for the teardown to actually complete. The old
  // force-complete-on-timeout design returned while NimBLE was still tearing
  // down, so a later connect raced the still-freeing client and crashed with a
  // use-after-free. Waiting here closes that race by completing the teardown,
  // not by force-freeing. The wait bound is large (DISCONNECT_WAIT_MAX_MS) and
  // acts only as a backstop against a genuinely stuck teardown; a normal
  // teardown finishes in well under a second once the aborting connect unwinds.
  //
  // Fast path: disconnectComplete() returns true as soon as the link is really
  // down (every target task stopped, no connect in progress, isConnected()
  // false for all), so a healthy disconnect or an already-dead link exits the
  // first check with zero delay slices. We cannot declare completion any earlier
  // than isConnected() clearing: that flag drops in onDisconnect, right before
  // NimBLE frees the client, so a shorter wait would reopen the connect-side
  // use-after-free above. When the camera is simply powered off, the host issued
  // ble_gap_terminate (Fujifilm::_disconnect) completes only at the link
  // supervision timeout, so this wait, and the interactive screen, is bounded by
  // Camera::m_IdleTimeout (now ~5 s, was ~16 s). Removing the residual freeze
  // entirely needs the wait moved off the LVGL task, which is a separate change
  // so it does not disturb this teardown lifecycle.
  //
  // The restart caller passes forRestart == true and a short timeout. It may
  // force-complete because esp_restart() runs immediately after and kills the
  // in-flight teardown, so no later connect can race it.
  const TickType_t start = xTaskGetTickCount();
  const uint32_t waitMs = forRestart ? timeout_ms : DISCONNECT_WAIT_MAX_MS;
  const TickType_t timeout = pdMS_TO_TICKS(waitMs);
  bool completed = true;
  while (!disconnectComplete()) {
    if ((timeout == 0) || (xTaskGetTickCount() - start >= timeout)) {
      ESP_LOGW(LOG_TAG, "Camera disconnect timed out after %lu ms, forcing completion.", waitMs);
      completed = false;
      break;
    }
    // The interactive wait runs on the LVGL task, which also feeds the M5PM1
    // watchdog. A slow teardown can outlast the feed period, so feed it here.
    // No mutex is held across this slice.
    Platform::getInstance().watchdogFeed();
    vTaskDelay(pdMS_TO_TICKS(DISCONNECT_WAIT_SLICE_MS));
  }

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);

    // Reaching here without completion means the restart force-complete fired,
    // or the interactive backstop expired on a stuck teardown. Control must not
    // stay wedged in STATE_DISCONNECTING, so end in a recoverable state, but the
    // underlying BLE teardown may still be in flight. Two independent lifetimes
    // must be respected here:
    //
    // Object lifetime: a target with m_Stopped == false has not finished. Its
    // task dequeued the undroppable CMD_DISCONNECT and is still inside
    // Camera::disconnect(), and it will write m_Stopped = true through its own
    // `this` when it returns, so freeing the object now is a use-after-free.
    // Move those targets to m_ZombieTargets (no destructor runs) and reap them
    // from the control task once m_Stopped flips, at which point the task has
    // called vTaskDelete(NULL) and can never touch the object again.
    //
    // Radio-call lifetime: ~Target() calls m_Camera->disconnect() only when
    // m_Stopped is false. We never destroy an m_Stopped == false target here
    // (it is quarantined instead), so no destructor run here or by the reaper
    // makes a radio call, which also keeps radio calls off m_Mutex.
    if (!completed) {
      for (auto &target : m_Targets) {
        if (!target->m_Stopped) {
          m_ZombieTargets.push_back(std::move(target));
        }
      }
    }

    // Destroys the stopped targets and drops the moved-from slots. On the clean
    // path every target is stopped, so this frees them all and quarantines none.
    m_Targets.clear();
    m_ConnectCamera = nullptr;
  }
  setState(STATE_IDLE);
  return completed;
}

bool Control::teardownDraining(void) {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return !m_ZombieTargets.empty();
}

void Control::reapZombieTargets(void) {
  const std::lock_guard<std::mutex> lock(m_Mutex);

  // A zombie's task was blocked inside Camera::disconnect() when we force
  // completed. task_exit writes m_Stopped = true and then only calls
  // vTaskDelete(NULL), which touches the task handle, not the Target object.
  // Once m_Stopped reads true the task can no longer touch `this`, so the object
  // is safe to free, and ~Target() skips its radio call because m_Stopped is
  // set. Leave any zombie still tearing down for a later sweep.
  const size_t before = m_ZombieTargets.size();
  m_ZombieTargets.erase(
      std::remove_if(m_ZombieTargets.begin(), m_ZombieTargets.end(),
                     [](const std::unique_ptr<Target> &target) { return target->m_Stopped; }),
      m_ZombieTargets.end());
  const size_t reaped = before - m_ZombieTargets.size();
  if (reaped > 0) {
    ESP_LOGD(LOG_TAG, "Reaped %u zombie target(s), %u still draining.",
             static_cast<unsigned>(reaped), static_cast<unsigned>(m_ZombieTargets.size()));
  }
}

void Control::addActive(std::shared_ptr<Camera> camera) {
  const std::lock_guard<std::mutex> lock(m_Mutex);

  // Deduplicate: never stack a second target on a camera that is already active.
  // Repeated connect requests (the console `connect <index>` path, a double tap)
  // otherwise add a duplicate Target for the same physical camera. That stacks
  // another per-target task and queue until the device runs out of memory and
  // reboots, and it would run resetConnectionState() below on a live link,
  // clearing the connected guard so connectAll() re-runs connect() and orphans
  // the still-live NimBLE client. Skip both by returning here.
  //
  // Key on the BLE address, not the Camera pointer. Every connect runs
  // CameraList::load(), which rebuilds the saved cameras as fresh shared_ptrs, so
  // the same physical camera has a different pointer on each connect and a
  // pointer compare never matches across reloads (the duplicate would slip
  // through). The address comes from the saved index entry and is stable across
  // the reload. getAddress() returns the cached m_Address member and never
  // touches m_Client, so there is no deref of a freed client. A legitimate
  // reconnect still works: disconnect() clears m_Targets, so the address is no
  // longer present and the connect is allowed. Distinct cameras in a multi-select
  // have distinct addresses and are all added.
  for (const auto &target : m_Targets) {
    if (target->getCamera()->getAddress() == camera->getAddress()) {
      ESP_LOGW(LOG_TAG, "Camera '%s' already active, ignoring duplicate connect.",
               camera->getName().c_str());
      return;
    }
  }

  // Clear any stale connected flag before this camera joins a fresh connect.
  // The camera comes from the persistent CameraList and may carry a stale-true
  // flag from a prior session whose onDisconnect never fired (camera powered
  // off, or a link that came up but failed registration). Without this,
  // connectAll() would see isConnected() true, skip the real connect, and jump
  // straight to active with no BLE work done. The dedup check above guarantees
  // the camera is not an active target here, so clearing the flag cannot race a
  // live session.
  camera->resetConnectionState();

  auto target = std::make_unique<Control::Target>(camera);

  // Create per-target task that will self-delete on disconnect
  BaseType_t ret = xTaskCreate(
      [](void *param) {
        auto *target = static_cast<Furble::Control::Target *>(param);
        target->task();
      },
      camera->getName().c_str(), 4096, target.get(), 3, NULL);
  if (ret != pdPASS) {
    ESP_LOGE(LOG_TAG, "Failed to create task for '%s'.", camera->getName().c_str());
  } else {
    m_Targets.push_back(std::move(target));
  }
}

std::shared_ptr<Camera> Control::getConnectingCamera(void) const {
  return m_ConnectCamera;
}

Control::state_t Control::getState(void) const {
  return m_State;
}

#if defined(FURBLE_CONSOLE)
Control::debug_state_t Control::getDebugState(void) const {
  debug_state_t snapshot = {};

  // m_State and the volatile abort/progress flags are read without m_StateMutex,
  // mirroring getState(): a debug snapshot tolerates a benign torn read and
  // taking m_StateMutex here would risk a lock ordering hazard against setState().
  snapshot.state = m_State;
  snapshot.connectInProgress = m_ConnectInProgress;
  snapshot.connectAbort = m_ConnectAbort;
  snapshot.sleepLockHeld = m_SleepLockHeld;
  snapshot.infiniteReconnect = m_InfiniteReconnect;
  snapshot.reconnectBackoff = m_ReconnectBackoff;
  snapshot.reconnectAttempt = m_ReconnectAttempt;

  const std::lock_guard<std::mutex> lock(m_Mutex);
  snapshot.targetCount = m_Targets.size();
  snapshot.zombieCount = m_ZombieTargets.size();
  snapshot.adaptiveActive = m_AdaptiveActive;
  snapshot.userPowerLevel = static_cast<int>(m_Power);
  snapshot.adaptivePowerLevel = static_cast<int>(m_AdaptivePower);
  snapshot.rssiStrongSamples = m_RssiStrongSamples;
  snapshot.rssiWeakSamples = m_RssiWeakSamples;

  size_t connected = 0;
  for (const auto &target : m_Targets) {
    if (target->getCamera()->isConnected()) {
      connected++;
    }
  }
  snapshot.connectedCount = connected;

  if (m_ConnectCamera != nullptr) {
    snapshot.connectingCamera = m_ConnectCamera->getName();
  }

  return snapshot;
}
#endif

size_t Control::getTargetCount(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  return m_Targets.size();
}

size_t Control::getConnectedTargetCount(void) const {
  const std::lock_guard<std::mutex> lock(m_Mutex);
  size_t connected = 0;
  for (const auto &target : m_Targets) {
    if (target->getCamera()->isConnected()) {
      connected++;
    }
  }
  return connected;
}

void Control::setState(state_t state) {
  const std::lock_guard<std::mutex> lock(m_StateMutex);

  if (state == m_State) {
    return;
  }

  // State transitions are the backbone of a connect/disconnect trace. Logged at
  // DEBUG so a release build compiles the line out and never spams the monitor.
  ESP_LOGD(LOG_TAG, "state %s -> %s", stateName(m_State), stateName(state));

  m_State = state;

  // The setting is read once per connect, on the transition into STATE_ACTIVE.
  // Holding the lock keeps the device awake for the whole connection, which is
  // what furble did before the controller could modem sleep.
  bool hold = (state == STATE_ACTIVE) && !Settings::load<Settings::SLEEP_CONN>();
  if (hold == m_SleepLockHeld) {
    return;
  }

  auto &power = Power::getInstance();
  if (hold) {
    power.acquire(Power::LockType::NO_LIGHT_SLEEP, POWER_LOCK_OWNER);
  } else {
    power.release(Power::LockType::NO_LIGHT_SLEEP, POWER_LOCK_OWNER);
  }

  m_SleepLockHeld = hold;
}

void Control::sampleAdaptivePower(void) {
  struct sample_t {
    Camera *camera;
    int8_t rssi;
  };

  std::vector<std::shared_ptr<Camera>> cameras;
  esp_power_level_t cap;
  bool restoreCap = false;

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    const uint32_t now = xTaskGetTickCount();
    if ((now - m_LastRssiSample) < pdMS_TO_TICKS(RSSI_SAMPLE_INTERVAL_MS)) {
      return;
    }
    m_LastRssiSample = now;
  }

  // NVS read, keep it off m_Mutex.
  const bool enabled = Settings::load<Settings::TX_ADAPTIVE>();

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    cap = m_Power;
    if (!enabled) {
      if (m_AdaptiveActive) {
        restoreCap = (m_AdaptivePower != m_Power);
        resetAdaptiveState();
      }
    } else {
      m_AdaptiveActive = true;
      // Snapshot cameras so the RSSI reads below run without the mutex.
      cameras.reserve(m_Targets.size());
      for (const auto &target : m_Targets) {
        cameras.push_back(target->getCamera());
      }
    }
  }

  if (!enabled) {
    if (restoreCap) {
      applyPower(cap);
    }
    return;
  }

  // Each getRssi() is a synchronous HCI round trip, keep m_Mutex released.
  std::vector<sample_t> samples;
  samples.reserve(cameras.size());
  for (const auto &camera : cameras) {
    const int8_t rssi = camera->getRssi();
    if (rssi != 0) {
      samples.push_back({camera.get(), rssi});
    }
  }

  bool step = false;
  int nextIndex = 0;
  esp_power_level_t next = cap;

  {
    const std::lock_guard<std::mutex> lock(m_Mutex);

    bool haveRssi = false;
    float weakestRssi = 0.0f;
    for (const auto &target : m_Targets) {
      auto *camera = target->getCamera().get();
      for (const auto &sample : samples) {
        if (sample.camera != camera) {
          continue;
        }

        if (target->m_HasRssi) {
          target->m_RssiAverage += RSSI_FILTER_ALPHA * (sample.rssi - target->m_RssiAverage);
        } else {
          target->m_RssiAverage = sample.rssi;
          target->m_HasRssi = true;
        }

        if (!haveRssi || (target->m_RssiAverage < weakestRssi)) {
          weakestRssi = target->m_RssiAverage;
          haveRssi = true;
        }
        break;
      }
    }

    if (!haveRssi) {
      ESP_LOGD(LOG_TAG, "Adaptive transmit power skipped, no RSSI sample");
      return;
    }

    ESP_LOGD(LOG_TAG, "Adaptive RSSI %.1f dBm", weakestRssi);

    int capIndex = powerIndex(m_Power);
    if (capIndex < 0) {
      capIndex = 0;
    }
    int current = powerIndex(m_AdaptivePower);
    if ((current < 0) || (current > capIndex)) {
      current = capIndex;
    }

    if (weakestRssi > RSSI_STEP_DOWN_DBM) {
      m_RssiWeakSamples = 0;
      if (m_RssiStrongSamples < UINT8_MAX) {
        m_RssiStrongSamples++;
      }
      if (m_RssiStrongSamples < RSSI_STEP_DOWN_SAMPLES) {
        return;
      }

      m_RssiStrongSamples = 0;
      if (current > 0) {
        nextIndex = current - 1;
        next = POWER_LEVELS[nextIndex];
        m_AdaptivePower = next;
        step = true;
      }
    } else if (weakestRssi < RSSI_STEP_UP_DBM) {
      m_RssiStrongSamples = 0;
      if (m_RssiWeakSamples < UINT8_MAX) {
        m_RssiWeakSamples++;
      }
      if (m_RssiWeakSamples < RSSI_STEP_UP_SAMPLES) {
        return;
      }

      m_RssiWeakSamples = 0;
      if (current < capIndex) {
        nextIndex = current + 1;
        next = POWER_LEVELS[nextIndex];
        m_AdaptivePower = next;
        step = true;
      }
    } else {
      m_RssiStrongSamples = 0;
      m_RssiWeakSamples = 0;
    }
  }

  if (step) {
    // Radio call, outside m_Mutex.
    applyPower(next);
    ESP_LOGI(LOG_TAG, "Adaptive transmit power stepped to level %d", nextIndex);
  }
}

void Control::resetAdaptiveState(void) {
  m_LastRssiSample = xTaskGetTickCount();
  m_RssiStrongSamples = 0;
  m_RssiWeakSamples = 0;
  m_AdaptiveActive = false;
  m_AdaptivePower = m_Power;

  for (const auto &target : m_Targets) {
    target->m_RssiAverage = 0.0f;
    target->m_HasRssi = false;
  }
}

void Control::applyPower(esp_power_level_t power) {
  // NimBLETxPowerType::Connection maps to the shared default connection power,
  // so one call covers every link. The controller offers no clean
  // per-connection readback, log the request only.
  const int8_t dbm = Device::powerLevelToDbm(power);
  const bool set = NimBLEDevice::setPower(dbm, NimBLETxPowerType::Connection);
  ESP_LOGI(LOG_TAG, "Transmit power requested %d dBm (level %d), set %s", dbm,
           static_cast<int>(power), set ? "ok" : "failed");
}

void Control::setPower(esp_power_level_t power) {
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    m_Power = power;
    resetAdaptiveState();
  }

  // Radio call, outside m_Mutex.
  applyPower(power);
}

void Control::setConnSaver(bool enabled) {
  std::vector<std::shared_ptr<Camera>> cameras;
  {
    const std::lock_guard<std::mutex> lock(m_Mutex);
    for (const auto &target : m_Targets) {
      cameras.push_back(target->getCamera());
    }
  }

  // Apply outside the mutex. setConnSaverEnabled() on a connected camera
  // enters NimBLE and can block on the HCI transport for up to two seconds,
  // and this runs on the caller's task (the LVGL task for the UI toggle).
  for (const auto &camera : cameras) {
    camera->setConnSaverEnabled(enabled);
  }
}

};  // namespace Furble

void control_task(void *param) {
  Furble::Control *control = static_cast<Furble::Control *>(param);

  control->task();
}
