#if defined(FURBLE_RIG)

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <esp_random.h>

#include "../include/FurbleCompanionService.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"
#include "driver.h"
#include "esp_log.h"
#include "rig_frame.h"

namespace Furble::Sim {
namespace {

constexpr std::array<uint8_t, 16> SERVICE_UUID = {
    0x5e, 0x4f, 0x7f, 0xb5, 0x7b, 0x08, 0x40, 0x47, 0xb7, 0x1d, 0x82, 0x62, 0xcf, 0x26, 0xeb, 0xbc,
};

uint16_t readLittle16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

void writeLittle32(uint8_t *data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
  data[2] = static_cast<uint8_t>(value >> 16);
  data[3] = static_cast<uint8_t>(value >> 24);
}

void writeLittle16(uint8_t *data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
}

class CompanionRigTransport final: public Furble::CompanionTransport {
 public:
  CompanionRigTransport(uint16_t port, bool ignoreUuidMismatch, bool dropNotify, uint32_t delayMs)
      : m_Port {port},
        m_IgnoreUuidMismatch {ignoreUuidMismatch},
        m_DropNotify {dropNotify},
        m_DelayMs {delayMs},
        m_Service {*this} {
    m_Service.setSettingReloadCallback(
        [this](bool pairingWindow) { reloadSetting(pairingWindow); });
  }

  ~CompanionRigTransport() override { stop(); }

  bool start(void) {
    if (m_Running.exchange(true)) {
      return true;
    }

    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
      ESP_LOGE(LOG_TAG, "Rig socket creation failed: %s", std::strerror(errno));
      m_Running = false;
      return false;
    }

    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(m_Port);
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1
        || bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) < 0
        || listen(listener, 1) < 0) {
      ESP_LOGE(LOG_TAG, "Rig bind failed on 127.0.0.1:%u: %s", m_Port, std::strerror(errno));
      close(listener);
      m_Running = false;
      return false;
    }

    m_ListenFd = listener;
    m_Service.init();
    ESP_LOGI(LOG_TAG, "RIG BUILD, NO BLE, NO ENCRYPTION");
    ESP_LOGI(LOG_TAG, "Rig listening on 127.0.0.1:%u", m_Port);
    m_ListenThread = std::thread {[this]() { listenLoop(); }};
    m_NotifyThread = std::thread {[this]() { notifyLoop(); }};
    return true;
  }

  void stop(void) {
    const bool wasRunning = m_Running.exchange(false);
    if (!wasRunning && !m_ListenThread.joinable() && !m_NotifyThread.joinable()) {
      return;
    }

    const int listener = m_ListenFd.exchange(-1);
    if (listener >= 0) {
      shutdown(listener, SHUT_RDWR);
      close(listener);
    }
    const int connection = m_Connection.load();
    if (connection >= 0) {
      shutdown(connection, SHUT_RDWR);
    }

    if (m_ListenThread.joinable()) {
      m_ListenThread.join();
    }
    if (m_NotifyThread.joinable()) {
      m_NotifyThread.join();
    }
    m_Service.onDisconnected();
    m_Service.deinit();
  }

  bool isEnabled(void) const { return m_Running.load(); }

  bool hasPendingPairing(void) const { return m_Service.hasPendingPairing(); }

  uint32_t pendingPairingPin(void) const { return m_Service.getPendingPairingPin(); }

  void confirmPairing(bool accept) {
    m_Service.confirmPairing(accept);
    if (!accept) {
      closeConnection();
    }
  }

  void reloadSetting(bool pairingWindow) {
    if (!m_Running.load()) {
      return;
    }
    if (!Settings::load<Settings::COMPANION>()) {
      closeConnection();
      return;
    }
    if (pairingWindow && isConnected()) {
      const uint32_t pin = 100000 + (esp_random() % 900000);
      m_Service.beginPairing(pin);
      uint8_t value[sizeof(pin)];
      writeLittle32(value, pin);
      sendFrame(Rig::PAIR_REQUEST, Rig::CHAR_NONE, value, sizeof(value));
    }
  }

  bool isConnected(void) const override { return m_Connection.load() >= 0; }

  // The rig deliberately reports the link as secure to exercise the same
  // settings and trigger service logic. The socket itself is plaintext.
  bool isEncrypted(void) const override { return isConnected(); }

  bool isAuthenticated(void) const override { return isConnected(); }

  uint16_t getMaxPayload(void) const override { return m_MaxPayload.load(); }

  void notify(uint8_t charId, const uint8_t *data, size_t len) override {
    if (charId != Furble::COMPANION_CHAR_STATUS || !m_StatusSubscribed.load() || m_DropNotify) {
      return;
    }
    if (len > getMaxPayload()) {
      error(charId, 0x0d);
      return;
    }
    sendFrame(Rig::NOTIFY, charId, data, len);
  }

  void indicate(uint8_t charId, const uint8_t *data, size_t len) override {
    if (charId != Furble::COMPANION_CHAR_SETTINGS || !m_SettingsSubscribed.load()) {
      return;
    }
    if (len > getMaxPayload()) {
      error(charId, 0x0d);
      return;
    }
    sendFrame(Rig::INDICATE, charId, data, len);
  }

  void error(uint8_t charId, uint8_t attError) override {
    sendFrame(Rig::ERROR, charId, &attError, sizeof(attError));
  }

 private:
  static bool readExact(int fd, uint8_t *data, size_t len) {
    size_t offset = 0;
    while (offset < len) {
      const ssize_t count = recv(fd, data + offset, len - offset, 0);
      if (count == 0) {
        return false;
      }
      if (count < 0) {
        if (errno == EINTR) {
          continue;
        }
        return false;
      }
      offset += static_cast<size_t>(count);
    }
    return true;
  }

  static bool writeExact(int fd, const uint8_t *data, size_t len) {
    size_t offset = 0;
    while (offset < len) {
#ifdef MSG_NOSIGNAL
      constexpr int sendFlags = MSG_NOSIGNAL;
#else
      constexpr int sendFlags = 0;
#endif
      const ssize_t count = send(fd, data + offset, len - offset, sendFlags);
      if (count < 0) {
        if (errno == EINTR) {
          continue;
        }
        return false;
      }
      if (count == 0) {
        return false;
      }
      offset += static_cast<size_t>(count);
    }
    return true;
  }

  bool sendFrame(uint8_t op, uint8_t charId, const uint8_t *data, size_t len) {
    if (len > Rig::MAX_FRAME_PAYLOAD || len > UINT16_MAX || (data == nullptr && len != 0)) {
      return false;
    }

    const int fd = m_Connection.load();
    if (fd < 0) {
      return false;
    }

    std::array<uint8_t, sizeof(Rig::header_t)> header = {
        Rig::MAGIC0, Rig::MAGIC1, op, charId, 0, 0,
    };
    writeLittle16(header.data() + 4, static_cast<uint16_t>(len));
    std::vector<uint8_t> frame(header.size() + len);
    std::memcpy(frame.data(), header.data(), header.size());
    if (len != 0) {
      std::memcpy(frame.data() + header.size(), data, len);
    }

    const std::lock_guard<std::mutex> lock(m_SendMutex);
    if (m_DelayMs != 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(m_DelayMs));
    }
    if (m_Connection.load() != fd) {
      return false;
    }
    return writeExact(fd, frame.data(), frame.size());
  }

  void sendHelloAck(void) {
    Rig::hello_t hello = {};
    hello.rig_version = Rig::RIG_VERSION;
    hello.wire_version = Furble::CompanionService::WIRE_VERSION;
    hello.role = 0;
    std::memcpy(hello.service_uuid, SERVICE_UUID.data(), SERVICE_UUID.size());
    writeLittle16(reinterpret_cast<uint8_t *>(&hello) + 20, m_MaxPayload.load());
    sendFrame(Rig::HELLO_ACK, Rig::CHAR_NONE, reinterpret_cast<const uint8_t *>(&hello),
              sizeof(hello));
  }

  bool handleHello(const std::vector<uint8_t> &payload) {
    if (payload.size() != sizeof(Rig::hello_t)) {
      ESP_LOGW(LOG_TAG, "Rig HELLO has invalid length");
      return false;
    }

    Rig::hello_t hello = {};
    std::memcpy(&hello, payload.data(), sizeof(hello));
    const bool uuidMatches =
        std::memcmp(hello.service_uuid, SERVICE_UUID.data(), SERVICE_UUID.size()) == 0;
    if (hello.rig_version != Rig::RIG_VERSION || hello.role != 1
        || hello.wire_version != Furble::CompanionService::WIRE_VERSION
        || (!uuidMatches && !m_IgnoreUuidMismatch)) {
      ESP_LOGE(LOG_TAG, "Rig HELLO mismatch, closing connection");
      return false;
    }
    if (!uuidMatches) {
      ESP_LOGW(LOG_TAG, "Ignoring rig service UUID mismatch");
    }

    const uint16_t requestedMax = readLittle16(payload.data() + 20);
    const uint16_t peerMax = requestedMax == 0 ? Rig::DEFAULT_MAX_PAYLOAD : requestedMax;
    m_MaxPayload = std::min<uint16_t>(Rig::DEFAULT_MAX_PAYLOAD, peerMax);
    sendHelloAck();

    const uint32_t pin = 100000 + (esp_random() % 900000);
    m_Service.beginPairing(pin);
    uint8_t pairValue[sizeof(pin)];
    writeLittle32(pairValue, pin);
    sendFrame(Rig::PAIR_REQUEST, Rig::CHAR_NONE, pairValue, sizeof(pairValue));
    return true;
  }

  bool canWrite(uint8_t charId, bool withoutResponse) const {
    if (charId == Rig::CHAR_LOCATION) {
      return true;
    }
    if (withoutResponse) {
      return false;
    }
    return charId == Rig::CHAR_SETTINGS || charId == Rig::CHAR_TRIGGER;
  }

  bool dispatchFrame(const Rig::header_t &header, const std::vector<uint8_t> &payload) {
    if ((header.op != Rig::HELLO) && !m_HelloComplete) {
      ESP_LOGW(LOG_TAG, "Rig frame arrived before HELLO");
      return false;
    }

    switch (header.op) {
      case Rig::HELLO:
        if (m_HelloComplete) {
          return false;
        }
        m_HelloComplete = handleHello(payload);
        return m_HelloComplete;
      case Rig::PAIR_CONFIRM:
        if (payload.size() != 1) {
          return false;
        }
        confirmPairing(payload[0] != 0);
        return payload[0] != 0;
      case Rig::SUBSCRIBE:
        if (payload.size() != sizeof(uint16_t)) {
          return false;
        }
        if (header.char_id == Rig::CHAR_STATUS) {
          m_StatusSubscribed = readLittle16(payload.data()) != 0;
          m_Service.notifyStatus(true);
        } else if (header.char_id == Rig::CHAR_SETTINGS) {
          m_SettingsSubscribed = readLittle16(payload.data()) != 0;
        } else {
          error(header.char_id, 0x06);
        }
        return true;
      case Rig::INDICATE_CONFIRM:
        return true;
      case Rig::READ_REQUEST:
        if (header.char_id != Rig::CHAR_STATUS || !payload.empty()) {
          error(header.char_id, 0x06);
          return true;
        }
        m_Service.notifyStatus(true);
        {
          const auto status = m_Service.getStatus();
          sendFrame(Rig::READ_RESPONSE, header.char_id, reinterpret_cast<const uint8_t *>(&status),
                    sizeof(status));
        }
        return true;
      case Rig::WRITE:
      case Rig::WRITE_NO_RSP:
      {
        const bool withoutResponse = header.op == Rig::WRITE_NO_RSP;
        if (!canWrite(header.char_id, withoutResponse)) {
          error(header.char_id, 0x03);
          return true;
        }
        if (payload.size() > getMaxPayload()) {
          error(header.char_id, 0x0d);
          return true;
        }
        switch (header.char_id) {
          case Rig::CHAR_LOCATION:
            m_Service.handleLocation(payload.data(), payload.size());
            break;
          case Rig::CHAR_SETTINGS:
            m_Service.handleSettings(payload.data(), payload.size());
            break;
          case Rig::CHAR_TRIGGER:
            m_Service.handleTrigger(payload.data(), payload.size());
            break;
          default:
            error(header.char_id, 0x0a);
            return true;
        }
        if (!withoutResponse) {
          sendFrame(Rig::WRITE_RESPONSE, header.char_id, nullptr, 0);
        }
        return true;
      }
      default:
        error(header.char_id, 0x06);
        return true;
    }
  }

  void session(int fd) {
    uint64_t offset = 0;

    while (m_Running.load() && m_Connection.load() == fd) {
      std::array<uint8_t, sizeof(Rig::header_t)> rawHeader = {};
      if (!readExact(fd, rawHeader.data(), rawHeader.size())) {
        break;
      }
      Rig::header_t header = {
          rawHeader[0],
          rawHeader[1],
          rawHeader[2],
          rawHeader[3],
          readLittle16(rawHeader.data() + 4),
      };
      if (header.magic0 != Rig::MAGIC0 || header.magic1 != Rig::MAGIC1) {
        ESP_LOGE(LOG_TAG, "Rig framing error at offset %llu", offset);
        break;
      }
      offset += sizeof(header);
      if (header.length > Rig::MAX_FRAME_PAYLOAD) {
        ESP_LOGE(LOG_TAG, "Rig payload too large at offset %llu", offset);
        break;
      }
      std::vector<uint8_t> payload(header.length);
      if (!readExact(fd, payload.data(), payload.size())) {
        break;
      }
      offset += payload.size();
      if (!dispatchFrame(header, payload)) {
        break;
      }
    }

    m_Service.onDisconnected();
    int expectedConnection = fd;
    m_Connection.compare_exchange_strong(expectedConnection, -1);
    shutdown(fd, SHUT_RDWR);
    close(fd);
  }

  void listenLoop(void) {
    while (m_Running.load()) {
      sockaddr_in peer = {};
      socklen_t peerLength = sizeof(peer);
      const int fd = accept(m_ListenFd.load(), reinterpret_cast<sockaddr *>(&peer), &peerLength);
      if (fd < 0) {
        if (m_Running.load()) {
          ESP_LOGW(LOG_TAG, "Rig accept failed: %s", std::strerror(errno));
        }
        continue;
      }

      if (!m_Running.load()) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
        break;
      }

#ifdef SO_NOSIGPIPE
      int noSigpipe = 1;
      setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof(noSigpipe));
#endif

      int expected = -1;
      if (!m_Connection.compare_exchange_strong(expected, fd)) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
        continue;
      }

      m_MaxPayload = Rig::DEFAULT_MAX_PAYLOAD;
      m_StatusSubscribed = false;
      m_SettingsSubscribed = false;
      m_HelloComplete = false;
      m_Service.onConnected();
      if (m_SessionThread.joinable()) {
        m_SessionThread.join();
      }
      m_SessionThread = std::thread {[this, fd]() { session(fd); }};
    }

    if (m_SessionThread.joinable()) {
      m_SessionThread.join();
    }
  }

  void notifyLoop(void) {
    while (m_Running.load()) {
      m_Service.notifyStatus();
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  void closeConnection(void) {
    const int fd = m_Connection.load();
    if (fd >= 0) {
      shutdown(fd, SHUT_RDWR);
    }
  }

  const uint16_t m_Port;
  const bool m_IgnoreUuidMismatch;
  const bool m_DropNotify;
  const uint32_t m_DelayMs;
  Furble::CompanionService m_Service;
  std::atomic<bool> m_Running {false};
  std::atomic<int> m_ListenFd {-1};
  std::atomic<int> m_Connection {-1};
  std::atomic<uint16_t> m_MaxPayload {Rig::DEFAULT_MAX_PAYLOAD};
  std::atomic<bool> m_StatusSubscribed {false};
  std::atomic<bool> m_SettingsSubscribed {false};
  bool m_HelloComplete = false;
  std::mutex m_SendMutex;
  std::thread m_ListenThread;
  std::thread m_SessionThread;
  std::thread m_NotifyThread;
};

std::unique_ptr<CompanionRigTransport> rig;
bool requested = false;
uint16_t configuredPort = 6737;
bool configuredIgnoreUuidMismatch = false;
bool configuredDropNotify = false;
uint32_t configuredDelayMs = 0;

}  // namespace

void rigConfigure(bool requestedValue,
                  uint16_t port,
                  bool ignoreUuidMismatch,
                  bool dropNotify,
                  uint32_t delayMs) {
  requested = requestedValue;
  configuredPort = port;
  configuredIgnoreUuidMismatch = ignoreUuidMismatch;
  configuredDropNotify = dropNotify;
  configuredDelayMs = delayMs;
}

void startRig(void) {
  if (requested && rig == nullptr) {
    rig = std::make_unique<CompanionRigTransport>(configuredPort, configuredIgnoreUuidMismatch,
                                                  configuredDropNotify, configuredDelayMs);
    if (!rig->start()) {
      rig.reset();
    }
  }
}

bool rigRequested(void) {
  return requested;
}

bool rigIsEnabled(void) {
  return rig != nullptr && rig->isEnabled();
}

bool rigHasPendingPairing(void) {
  return rig != nullptr && rig->hasPendingPairing();
}

uint32_t rigPendingPairingPin(void) {
  return rig == nullptr ? 0 : rig->pendingPairingPin();
}

void rigConfirmPairing(bool accept) {
  if (rig != nullptr) {
    rig->confirmPairing(accept);
  }
}

void rigReloadSetting(bool pairingWindow) {
  if (rig != nullptr) {
    rig->reloadSetting(pairingWindow);
  }
}

}  // namespace Furble::Sim

#else

#include "driver.h"

namespace Furble::Sim {

void rigConfigure(bool, uint16_t, bool, bool, uint32_t) {}
void startRig(void) {}
bool rigRequested(void) {
  return false;
}
bool rigIsEnabled(void) {
  return false;
}
bool rigHasPendingPairing(void) {
  return false;
}
uint32_t rigPendingPairingPin(void) {
  return 0;
}
void rigConfirmPairing(bool) {}
void rigReloadSetting(bool) {}

}  // namespace Furble::Sim

#endif
