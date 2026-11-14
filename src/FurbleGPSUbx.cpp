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
#include "FurbleSettings.h"
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
  m_UbxConfigured = false;
  m_UbxPlan = {};
  m_UbxHaveSat = false;
  m_UbxSatView.fill(0);
  m_UbxSatUsed.fill(0);
  m_RailOffPending = false;
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
 * The settings become a Ubx::Plan: NAV-PVT output, rate, dynamic model and
 * AssistNow Autonomous; then the constellations, which restart the GNSS and
 * need 0.5 s before the next command (IFD 4.9.21); then power save with
 * OPERATEMODE last (MAX-M10S integration manual UBX-20053088 3.6.2.6). Every
 * write goes to RAM and BBR, because software standby clears the RAM
 * configuration (3.6.3.2) and BBR is what the receiver reloads when it wakes.
 * Keys furble manages but the settings leave alone are deleted from BBR, so a
 * value an earlier session stored does not outlive its setting. A closing
 * VALGET reads the RAM values back.
 */
void GPS::configureUbx(void) {
  m_ConfigQueue.clear();
  m_ConfigInFlight.reset();
  m_ConfigNotBefore = 0;
  {
    const std::lock_guard<std::mutex> lock(m_ConfigMutex);
    m_ConfigStatusCount = 0;
  }

  Ubx::Request request = {};
  const uint8_t rate = Settings::load<Settings::GPS_RATE>();
  request.rate_ms = (rate < RATE_MS.size()) ? RATE_MS[rate] : 0;
  request.dyn_model = Ubx::dynModelForPlatform(Settings::load<Settings::GPS_PLATFORM>());
  const uint8_t gnss = Settings::load<Settings::GPS_GNSS>();
  const uint8_t constellation = Settings::load<Settings::GPS_CONSTEL>();
  if ((gnss != 0) && (gnss <= Ubx::GNSS_MASK_ALL)) {
    request.gnss = gnss;
    request.gnss_set = Ubx::GNSS_MASK_ALL;
  } else if ((constellation > 0) && (constellation <= CONSTELLATION_MAX)) {
    // CASIC has no Galileo bit, so Galileo keeps the receiver's own setting.
    request.gnss = Ubx::gnssFromConstellation(constellation);
    request.gnss_set = Ubx::GNSS_MASK_ALL & ~Ubx::GNSS_MASK_GAL;
  }
  request.psm = Settings::load<Settings::GPS_PSM>();
  request.period_s = (m_DutySeconds > 0) ? m_DutySeconds : 10;
  request.ana = Settings::load<Settings::GPS_ANA>();
  const Ubx::Plan plan = Ubx::plan(request);
  if (plan.gnss_refused) {
    ESP_LOGW(LOG_TAG, "GPS constellation mask 0x%02x is not valid on M10, left alone", plan.gnss);
  }
  if (plan.psm_refused) {
    ESP_LOGW(LOG_TAG, "GPS power save is refused with BeiDou B1C, left at full power");
  }

  constexpr uint8_t LAYERS = Ubx::LAYER_RAM | Ubx::LAYER_BBR;
  constexpr uint16_t SIGNAL_SETTLE_MS = 500;
  m_UbxExpected = plan.base;
  enqueueConfig(Ubx::CLASS_CFG, Ubx::ID_CFG_VALSET, payloadOf(Ubx::valSet(LAYERS, plan.base)), "",
                false, false, true);
  if (!plan.signals.empty()) {
    m_UbxExpected.insert(m_UbxExpected.end(), plan.signals.begin(), plan.signals.end());
    enqueueConfig(Ubx::CLASS_CFG, Ubx::ID_CFG_VALSET, payloadOf(Ubx::valSet(LAYERS, plan.signals)),
                  "", false, false, true, SIGNAL_SETTLE_MS);
  }
  m_UbxExpected.insert(m_UbxExpected.end(), plan.power.begin(), plan.power.end());
  enqueueConfig(Ubx::CLASS_CFG, Ubx::ID_CFG_VALSET, payloadOf(Ubx::valSet(LAYERS, plan.power)), "",
                false, false, true);
  if (!plan.unset.empty()) {
    enqueueConfig(Ubx::CLASS_CFG, Ubx::ID_CFG_VALDEL,
                  payloadOf(Ubx::valDel(Ubx::LAYER_BBR, plan.unset)), "", false, false, true);
  }
  std::vector<uint32_t> keys;
  for (const auto &item : m_UbxExpected) {
    keys.push_back(item.key);
  }
  enqueueConfig(Ubx::CLASS_CFG, Ubx::ID_CFG_VALGET, payloadOf(Ubx::valGet(Ubx::GET_RAM, keys)), "",
                false, false, true);

  {
    // The burst predictor has to expect what the receiver will now do: the
    // clamped rate, or one epoch per on/off period.
    const uint32_t interval = (plan.psm == Ubx::PSM_ON_OFF)
                                  ? (std::max<uint32_t>(5, request.period_s) * 1000)
                                  : plan.rate_ms;
    const std::lock_guard<std::mutex> lock(m_CycleMutex);
    m_RateMs = static_cast<uint16_t>(std::min<uint32_t>(interval, UINT16_MAX));
    m_ExpectedInterval = m_RateMs;
  }
  {
    const std::lock_guard<std::mutex> lock(m_UbxMutex);
    m_UbxVerified = 0;
    m_UbxPlan = plan;
    m_UbxAna = request.ana;
    m_UbxConfigured = true;
  }

  // Ask whether the receiver restored a save-on-shutdown backup at start-up
  // (IFD 3.19.1). Not a CFG message, so it is not acknowledged.
  sendUbx(Ubx::sosPoll());
}

void GPS::pollUbxSatellites(void) {
  if (!m_Enabled || (getBackend() != backend_t::UBX)) {
    return;
  }
  {
    const std::lock_guard<std::mutex> lock(m_UbxMutex);
    m_UbxHaveSat = false;
  }
  // An empty NAV-SAT is a poll for one copy (IFD 3.5.2).
  sendUbx(Ubx::frame(Ubx::CLASS_NAV, Ubx::ID_NAV_SAT, {}));
}

bool GPS::beginSaveOnShutdown(uint32_t now) {
  // A rail that is already off has nothing left to save. A late duty request
  // can arrive just after the cut, and must not mark the backup as refused.
  if ((getBackend() != backend_t::UBX) || Platform::hasGpsBackupSupply()
      || (m_CycleState == cycle_state_t::RAIL_OFF)) {
    // With V_BCKP the receiver keeps BBR itself (MAX-M10S integration manual
    // 3.6.3.1), so there is nothing to save.
    return false;
  }
  {
    const std::lock_guard<std::mutex> lock(m_UbxMutex);
    if (m_UbxSosBackup == 2) {
      // refused once this session: a module without flash, do not retry
      return false;
    }
    m_UbxSosBackup = 0;
  }
  // A GNSS stop first keeps BBR consistent (IFD 3.19.1), then the backup.
  sendUbx(Ubx::cfgRst(Ubx::RST_HOT, Ubx::RST_MODE_GNSS_STOP));
  sendUbx(Ubx::sosCommand(Ubx::SOS_CREATE));
  m_RailOffPending = true;
  m_RailOffDeadline = now + UBX_ACK_TIMEOUT_MS;
  return true;
}

void GPS::saveOnShutdownBlocking(void) {
  const uint32_t start = Platform::getInstance().tick();
  if (!beginSaveOnShutdown(start)) {
    return;
  }
  m_RailOffPending = false;
  // disable() parks the GPS task first, so this is the only UART reader.
  Ubx::Stream stream;
  std::array<uint8_t, 64> buffer;
  while ((Platform::getInstance().tick() - start) < UBX_ACK_TIMEOUT_MS) {
    const int read = uart_read_bytes(m_UART, buffer.data(), buffer.size(), pdMS_TO_TICKS(20));
    for (int i = 0; i < read; i++) {
      if (!stream.push(buffer[i])) {
        continue;
      }
      serviceUbx(stream.frame().data(), stream.frame().size());
      const std::lock_guard<std::mutex> lock(m_UbxMutex);
      if (m_UbxSosBackup != 0) {
        return;
      }
    }
    if (read <= 0) {
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
  const std::lock_guard<std::mutex> lock(m_UbxMutex);
  m_UbxSosBackup = 2;
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

  if ((cls == Ubx::CLASS_NAV) && (id == Ubx::ID_NAV_SAT)) {
    std::vector<Ubx::NavSatSv> svs;
    if (!Ubx::parseNavSat(payload, n, svs)) {
      return;
    }
    std::array<uint8_t, 7> view = {};
    std::array<uint8_t, 7> used = {};
    for (const auto &sv : svs) {
      if (sv.gnssId < view.size()) {
        view[sv.gnssId] = static_cast<uint8_t>(std::min(255, view[sv.gnssId] + 1));
        if (sv.used()) {
          used[sv.gnssId] = static_cast<uint8_t>(std::min(255, used[sv.gnssId] + 1));
        }
      }
    }
    const std::lock_guard<std::mutex> lock(m_UbxMutex);
    m_UbxSatView = view;
    m_UbxSatUsed = used;
    m_UbxHaveSat = true;
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
      if ((ack.cls == Ubx::CLASS_UPD) && (ack.id == Ubx::ID_UPD_SOS)) {
        m_UbxSosBackup = 2;
      }
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
    status.configured = m_UbxConfigured;
    status.gnss = m_UbxPlan.gnss;
    status.psm = m_UbxPlan.psm;
    status.rate_ms = m_UbxPlan.rate_ms;
    status.ana = m_UbxAna;
    status.gnss_refused = m_UbxPlan.gnss_refused;
    status.psm_refused = m_UbxPlan.psm_refused;
    status.rate_clamped = m_UbxPlan.rate_clamped;
    status.have_sat = m_UbxHaveSat;
    status.sat_view = m_UbxSatView;
    status.sat_used = m_UbxSatUsed;
  }
  status.backup_supply = Platform::hasGpsBackupSupply();
  const std::lock_guard<std::mutex> lock(m_GPSMutex);
  status.have_pvt = m_UbxPvtValid;
  status.pvt = m_UbxPvt;
  status.pvt_age_ms = m_UbxPvtValid ? (Platform::getInstance().tick() - m_UbxPvtTick) : 0;
  return status;
}

}  // namespace Furble
