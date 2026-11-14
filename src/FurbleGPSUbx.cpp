/*
 * The UBX backend of the GPS driver: u-blox M10 detection, configuration,
 * NAV-PVT and the receiver status. The protocol itself is the pure codec in
 * lib/furble/protocol/GpsUbx; plans/321-ublox-m10-driver.md cites the u-blox
 * documents for every message and key. "IFD" is the u-blox M10 SPG 5.10
 * Interface description, UBX-21035062 R03.
 */
#include <algorithm>
#include <cmath>

#include <esp_log.h>

#include "FurbleGPS.h"
#include "FurblePlatform.h"
#include "FurbleTypes.h"

namespace Furble {

namespace {

/** The payload of a frame built by the codec, for the configuration queue. */
std::vector<uint8_t> payloadOf(const std::vector<uint8_t> &frame) {
  if (frame.size() < Ubx::FRAME_OVERHEAD) {
    return {};
  }
  return std::vector<uint8_t>(frame.begin() + Ubx::HEADER_SIZE, frame.end() - Ubx::CHECKSUM_SIZE);
}

}  // namespace

GPS::backend_t GPS::getBackend(void) const {
  return static_cast<backend_t>(m_Backend.load());
}

const char *GPS::backendName(backend_t backend) {
  switch (backend) {
    case backend_t::UNKNOWN:
      return "unknown";
    case backend_t::DETECTING:
      return "detecting";
    case backend_t::NMEA:
      return "nmea";
    case backend_t::UBX:
      return "ubx";
  }
  return "unknown";
}

void GPS::resetUbx(void) {
  m_Backend.store(static_cast<uint8_t>(backend_t::UNKNOWN));
  m_Detect = {};
  m_ConfigNotBefore = 0;
  m_UbxExpected.clear();
  {
    const std::lock_guard<std::mutex> lock(m_GPSMutex);
    m_UbxPvtValid = false;
  }
  const std::lock_guard<std::mutex> lock(m_UbxMutex);
  m_UbxHaveVersion = false;
  m_UbxVersion = {};
  m_UbxSosRestore = 0xff;
  m_UbxVerified = 0;
  m_UbxNaks = 0;
}

bool GPS::sendUbx(const std::vector<uint8_t> &frame) {
  if (frame.empty()) {
    return false;
  }
  const std::lock_guard<std::mutex> lock(m_TxMutex);
  const int written = uart_write_bytes(m_UART, frame.data(), frame.size());
  uart_wait_tx_done(m_UART, pdMS_TO_TICKS(TX_MS));
  if (written != static_cast<int>(frame.size())) {
    ESP_LOGE(LOG_TAG, "GPS UBX write failed: %d of %u", written,
             static_cast<unsigned>(frame.size()));
    return false;
  }
  return true;
}

/**
 * Poll MON-VER (IFD 3.14.9). Every u-blox receiver answers it whatever its
 * configuration, and the AT6668 and L76K ignore a UBX frame, so the poll is
 * the one byte sequence that tells the two apart without disturbing either.
 */
void GPS::startDetect(uint32_t now) {
  m_Backend.store(static_cast<uint8_t>(backend_t::DETECTING));
  m_Detect.begin(now);
  sendUbx(Ubx::pollMonVer());
}

bool GPS::serviceDetect(uint32_t now) {
  if (getBackend() != backend_t::DETECTING) {
    return true;
  }
  bool poll = false;
  const Ubx::Detect::Result result = m_Detect.service(now, poll);
  if (result == Ubx::Detect::Result::WAITING) {
    if (poll) {
      sendUbx(Ubx::pollMonVer());
    }
    return false;
  }
  if (result == Ubx::Detect::Result::UBX) {
    m_Backend.store(static_cast<uint8_t>(backend_t::UBX));
    return true;
  }
  // No UBX answer. Keep the path furble always had.
  ESP_LOGI(LOG_TAG, "GPS receiver did not answer MON-VER, using the NMEA backend");
  m_Backend.store(static_cast<uint8_t>(backend_t::NMEA));
  return true;
}

/**
 * Build the acknowledged UBX configuration queue.
 *
 * Every write goes to RAM and BBR. Software standby clears the RAM
 * configuration (MAX-M10S integration manual UBX-20053088 3.6.3.2), and BBR is
 * what the receiver reloads from when it wakes. A closing VALGET reads the RAM
 * values back, so a receiver that acknowledged and then did something else is
 * visible on the console and to the simulator.
 */
void GPS::configureUbx(void) {
  m_ConfigQueue.clear();
  m_ConfigInFlight.reset();
  m_ConfigNotBefore = 0;
  {
    const std::lock_guard<std::mutex> lock(m_ConfigMutex);
    m_ConfigStatusCount = 0;
  }
  {
    const std::lock_guard<std::mutex> lock(m_UbxMutex);
    m_UbxVerified = 0;
  }

  // NAV-PVT once per navigation epoch on UART1 (IFD 4.9.11). NMEA stays on,
  // so the parser remains the fallback when a NAV-PVT is lost.
  std::vector<Ubx::KeyValue> items = {
      {Ubx::Key::MSGOUT_NAV_PVT_UART1, 1},
  };

  m_UbxExpected = items;
  const auto set = Ubx::valSet(Ubx::LAYER_RAM | Ubx::LAYER_BBR, items);
  enqueueConfig(Ubx::CLASS_CFG, Ubx::ID_CFG_VALSET, payloadOf(set), "", false, false, true);

  std::vector<uint32_t> keys;
  for (const auto &item : items) {
    keys.push_back(item.key);
  }
  const auto get = Ubx::valGet(Ubx::GET_RAM, keys);
  enqueueConfig(Ubx::CLASS_CFG, Ubx::ID_CFG_VALGET, payloadOf(get), "", false, false, true);

  // Ask whether the receiver restored a save-on-shutdown backup at start-up
  // (IFD 3.19.1). Not a CFG message, so it is not acknowledged.
  sendUbx(Ubx::sosPoll());
}

void GPS::serviceUbx(const uint8_t *frame, size_t length) {
  const uint8_t cls = frame[2];
  const uint8_t id = frame[3];
  const uint8_t *payload = frame + Ubx::HEADER_SIZE;
  const size_t n = length - Ubx::FRAME_OVERHEAD;

  if ((cls == Ubx::CLASS_NAV) && (id == Ubx::ID_NAV_PVT)) {
    Ubx::NavPvt pvt;
    if (!Ubx::parseNavPvt(payload, n, pvt)) {
      return;
    }
    const std::lock_guard<std::mutex> lock(m_GPSMutex);
    m_UbxPvt = pvt;
    m_UbxPvtTick = Platform::getInstance().tick();
    m_UbxPvtValid = true;
    m_UbxPvtCount++;
    // the same one-fix-per-burst rule the NMEA path applies in processNmea()
    if (pvt.positionValid() && (m_FixSequence.load() != m_BurstSequence.load())) {
      m_FixSequence = m_BurstSequence.load();
#if defined(FURBLE_SIM)
      m_SimFreshFixesParsed.fetch_add(1);
      if (m_DutyWake) {
        m_SimDutyWakeFixesParsed.fetch_add(1);
      }
#endif
    }
    return;
  }

  if ((cls == Ubx::CLASS_ACK) && (n == 2)) {
    Ubx::Ack ack;
    if (!Ubx::parseAck(id, payload, n, ack)) {
      return;
    }
    if (!ack.ack) {
      const std::lock_guard<std::mutex> lock(m_UbxMutex);
      m_UbxNaks++;
    }
    if (!m_ConfigInFlight.has_value() || !m_ConfigInFlight->ubx
        || (ack.cls != m_ConfigInFlight->class_id) || (ack.id != m_ConfigInFlight->message_id)) {
      return;
    }
    if (ack.ack && (ack.id == Ubx::ID_CFG_VALGET)) {
      // the data answers a VALGET, the acknowledgement only follows it
      return;
    }
    const uint16_t settle = m_ConfigInFlight->settle_ms;
    finishConfigCommand(ack.ack ? CONFIG_ACKED : CONFIG_NACKED);
    if (ack.ack && (settle != 0)) {
      m_ConfigNotBefore = Platform::getInstance().tick() + settle;
    }
    return;
  }

  if ((cls == Ubx::CLASS_CFG) && (id == Ubx::ID_CFG_VALGET)) {
    Ubx::ValGet response;
    if (!Ubx::parseValGet(payload, n, response) || !m_ConfigInFlight.has_value()
        || !m_ConfigInFlight->ubx || (m_ConfigInFlight->message_id != Ubx::ID_CFG_VALGET)) {
      return;
    }
    bool match = response.items.size() == m_UbxExpected.size();
    for (const auto &expected : m_UbxExpected) {
      const auto found =
          std::find_if(response.items.begin(), response.items.end(),
                       [&expected](const Ubx::KeyValue &item) { return item.key == expected.key; });
      const uint64_t mask = (Ubx::valueSize(expected.key) >= 8)
                                ? ~0ULL
                                : ((1ULL << (8 * Ubx::valueSize(expected.key))) - 1);
      match = match && (found != response.items.end()) && (found->value == (expected.value & mask));
    }
    {
      const std::lock_guard<std::mutex> lock(m_UbxMutex);
      m_UbxVerified = match ? 1 : 2;
    }
    if (!match) {
      ESP_LOGW(LOG_TAG, "GPS UBX configuration read back differs from what was set");
    }
    finishConfigCommand(CONFIG_ACKED);
    return;
  }

  if ((cls == Ubx::CLASS_MON) && (id == Ubx::ID_MON_VER)) {
    Ubx::MonVer version;
    if (!Ubx::parseMonVer(payload, n, version)) {
      return;
    }
    {
      const std::lock_guard<std::mutex> lock(m_UbxMutex);
      m_UbxVersion = version;
      m_UbxHaveVersion = true;
    }
    const backend_t backend = getBackend();
    if ((backend == backend_t::UBX) || (backend == backend_t::UNKNOWN)) {
      // already identified, or detection has not started and will poll
      return;
    }
    ESP_LOGI(LOG_TAG, "GPS u-blox receiver: %s", version.sw.c_str());
    if (backend == backend_t::DETECTING) {
      // serviceDetect() picks the answer up and configures the receiver
      m_Detect.answered();
      return;
    }
    // A late answer after detection gave up. The receiver is u-blox after
    // all, so configure it as one; the CASIC frames it got were ignored.
    m_Backend.store(static_cast<uint8_t>(backend_t::UBX));
    m_ConfigChars = getStatusSnapshot().chars_processed;
    m_ConfigStart = Platform::getInstance().tick();
    m_ConfigPending = true;
    return;
  }

  if ((cls == Ubx::CLASS_UPD) && (id == Ubx::ID_UPD_SOS)) {
    Ubx::Sos sos;
    if (!Ubx::parseSos(payload, n, sos)) {
      return;
    }
    const std::lock_guard<std::mutex> lock(m_UbxMutex);
    if (sos.cmd == Ubx::SOS_RESTORED) {
      m_UbxSosRestore = sos.response;
    } else {
      m_UbxSosBackup = (sos.response == 1) ? 1 : 2;
    }
    return;
  }
}

void GPS::applyPvt(status_t &status) const {
  // Called from getStatusSnapshot() with m_GPSMutex held.
  if (!m_UbxPvtValid) {
    return;
  }
  const uint32_t age = Platform::getInstance().tick() - m_UbxPvtTick;
  if (age >= MAX_AGE_MS) {
    // Stale: the NMEA parser is the fallback.
    return;
  }
  const Ubx::NavPvt &pvt = m_UbxPvt;
  const bool position = pvt.positionValid();
  status.from_pvt = true;
  status.position_valid = position;
  status.fix = position;
  status.altitude_valid = position && (pvt.fixType >= 3);
  status.satellites = pvt.numSV;
  status.latitude = pvt.lat * 1e-7;
  status.longitude = pvt.lon * 1e-7;
  status.altitude = pvt.hMSL / 1000.0;
  status.speed_mps = pvt.gSpeed / 1000.0;
  status.speed_kmph = status.speed_mps * 3.6;
  status.course_deg = pvt.headMot * 1e-5;
  status.speed_valid = position;
  status.course_valid = position;
  status.speed_age = age;
  status.course_age = age;
  status.location_age = age;
  status.accuracy_valid = position;
  status.h_acc_m = pvt.hAcc / 1000.0F;
  status.v_acc_m = pvt.vAcc / 1000.0F;
  status.t_acc_ns = pvt.tAcc;
  if (pvt.timeValid()) {
    status.date_valid = true;
    status.time_valid = true;
    status.date_age = age;
    status.time_age = age;
    status.year = pvt.year;
    status.month = pvt.month;
    status.day = pvt.day;
    status.hour = pvt.hour;
    status.minute = pvt.min;
    status.second = std::min<uint8_t>(pvt.sec, 59);
    // nano is the signed fraction of the rounded second. A negative fraction
    // belongs to the previous second; report it as .00 rather than borrow.
    status.centisecond = static_cast<uint8_t>((pvt.nano > 0) ? (pvt.nano / 10000000) : 0);
  } else {
    status.date_valid = false;
    status.time_valid = false;
  }
}

GPS::ubx_status_t GPS::getUbxStatus(void) const {
  ubx_status_t status = {};
  {
    const std::lock_guard<std::mutex> lock(m_UbxMutex);
    status.have_version = m_UbxHaveVersion;
    status.software = m_UbxVersion.sw;
    status.hardware = m_UbxVersion.hw;
    status.firmware = m_UbxVersion.field("FWVER");
    status.protocol = m_UbxVersion.field("PROTVER");
    status.module = m_UbxVersion.field("MOD");
    status.sos_restore = m_UbxSosRestore;
    status.sos_backup = m_UbxSosBackup;
    status.config_verified = m_UbxVerified;
    status.naks = m_UbxNaks;
  }
  const std::lock_guard<std::mutex> lock(m_GPSMutex);
  status.have_pvt = m_UbxPvtValid;
  status.pvt = m_UbxPvt;
  status.pvt_age_ms = m_UbxPvtValid ? (Platform::getInstance().tick() - m_UbxPvtTick) : 0;
  return status;
}

}  // namespace Furble
