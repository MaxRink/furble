/*
 * The UBX backend of the GPS driver: u-blox M10 detection, configuration,
 * NAV-PVT and the receiver status. The protocol itself is the pure codec in
 * lib/furble/protocol/GpsUbx; plans/implemented/321-ublox-m10-driver.md cites the u-blox
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
#if !defined(FURBLE_GPS_TEXT) || FURBLE_GPS_TEXT
    case backend_t::QUECTEL:
      return "quectel";
    case backend_t::UNICORE:
      return "unicore";
#else
    // Neither enum value can be reached without the text backend; the default
    // keeps the switch exhaustive in a text-off build.
    default:
      break;
#endif
  }
  return "unknown";
}

void GPS::resetUbx(void) {
  m_Backend.store(static_cast<uint8_t>(backend_t::UNKNOWN));
  m_Detect = {};
#if FURBLE_GPS_L5
  m_CapsProbe = {};
  m_CapsAnswered = false;
  m_CapsAnswerL5 = false;
#endif
  m_ConfigSettle.clear();
  m_UbxExpected.clear();
  {
    const std::lock_guard<std::mutex> lock(m_GPSMutex);
    m_UbxPvtValid = false;
  }
  const std::lock_guard<std::mutex> lock(m_UbxMutex);
  m_UbxHaveVersion = false;
  m_UbxVersion = {};
  // Nothing is known about the bands of a receiver furble has not identified,
  // so a new session starts at L1 and asks again.
  m_UbxCaps = Ubx::CAPS_UNKNOWN;
  m_UbxCapsProbed = false;
  m_UbxSosRestore = 0xff;
  // The save-on-shutdown refusal latch lasts one GPS enable: a flashless
  // receiver costs at most one refused save per session.
  m_UbxSosBackup = 0;
  m_UbxVerified = 0;
  m_UbxNaks = 0;
  m_UbxConfigured = false;
  m_UbxPlan = {};
  m_UbxHaveSat = false;
  m_UbxSatView.fill(0);
  m_UbxSatUsed.fill(0);
  m_RailOffPending = false;
  // m_UbxStopped is not reset here: disable() calls this between the GNSS
  // stop and railOffOrRestart(), which must still see it if the cut fails.
  // railOffOrRestart() clears it.
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
 * Send the identification probes: UBX-MON-VER (IFD 3.14.9), $PQTMVERNO (Q76
 * 2.4.5 page 100) and $PDTINFO (UC 3.2 page 26).
 *
 * Every u-blox receiver answers MON-VER whatever its configuration. A Quectel
 * part answers the PQTM version query, a Unicore part answers the product
 * information query, and the AT6668 and L76K answer none of the three, so they
 * keep the NMEA backend. All three go out in one window, so the fallback costs
 * the same two seconds it always did instead of one window per vendor.
 */
void GPS::startDetect(uint32_t now) {
  m_Backend.store(static_cast<uint8_t>(backend_t::DETECTING));
  m_Detect.begin(now);
#if !defined(FURBLE_GPS_TEXT) || FURBLE_GPS_TEXT
  m_TextIdentity.store(static_cast<uint8_t>(GpsText::Dialect::NONE));
#endif
#if !defined(FURBLE_GPS_TEXT) || FURBLE_GPS_TEXT
  sendIdentifyProbes();
#else
  // The text probes belong to a backend this build does not have; a u-blox
  // receiver still identifies itself over UBX.
  sendUbx(Ubx::pollMonVer());
#endif
}

bool GPS::serviceDetect(uint32_t now) {
  if (getBackend() != backend_t::DETECTING) {
    return true;
  }

#if !defined(FURBLE_GPS_TEXT) || FURBLE_GPS_TEXT
  // A vendor text answer decides at once: the receiver that answered will never
  // answer another vendor's probe, so waiting out the window only delays its
  // configuration.
  switch (textIdentity()) {
    case GpsText::Dialect::QUECTEL:
      m_Backend.store(static_cast<uint8_t>(backend_t::QUECTEL));
      return true;
    case GpsText::Dialect::UNICORE:
      m_Backend.store(static_cast<uint8_t>(backend_t::UNICORE));
      return true;
    case GpsText::Dialect::NONE:
      break;
  }
#endif

  bool poll = false;
  const Ubx::Detect::Result result = m_Detect.service(now, poll);
  if (result == Ubx::Detect::Result::WAITING) {
    if (poll) {
#if !defined(FURBLE_GPS_TEXT) || FURBLE_GPS_TEXT
      sendIdentifyProbes();
#else
      sendUbx(Ubx::pollMonVer());
#endif
    }
    return false;
  }
  if (result == Ubx::Detect::Result::UBX) {
    m_Backend.store(static_cast<uint8_t>(backend_t::UBX));
    return true;
  }
  // No vendor answered. Keep the path furble always had.
  ESP_LOGI(LOG_TAG, "GPS receiver answered no identification probe, using the NMEA backend");
  m_Backend.store(static_cast<uint8_t>(backend_t::NMEA));
  return true;
}

/**
 * Ask the receiver which signal bands it implements.
 *
 * One CFG-VALGET of the four L5 keys at the default layer is the whole
 * question: a firmware that has no L5 signal answers UBX-ACK-NAK because a key
 * it does not know is refused (M10 SPG 5.10 IFD UBX-21035062 R03, 3.10.4 page
 * 53; F10 SPG 6.00 IFD UBX-23002975 R02, 3.10.4 pages 54 and 55), and a
 * firmware that has them answers with one item each (same sections). Nothing
 * is written, so the receiver keeps its configuration whatever the answer.
 *
 * MON-VER already names a family, so that answer is what furble believes until
 * the probe contradicts it. The probe only ever removes the L5 band: a signal
 * VALSET carrying one unknown key is refused in full, which would take the
 * rate, the dynamic model and the L1 signals down with it.
 */
bool GPS::serviceCaps(uint32_t now) {
#if !FURBLE_GPS_L5
  // The build has no L5 keys to ask for, so the receiver stays on L1 and the
  // MON-VER hint is all the capability the console and the UI report.
  (void)now;
  return true;
#else
  if (m_CapsProbe.result() == Ubx::CapsProbe::Result::IDLE) {
    m_CapsProbe.begin(now);
    std::vector<uint32_t> keys;
    for (const uint32_t key : Ubx::l5Keys()) {
      keys.push_back(key);
    }
    sendUbx(Ubx::valGet(Ubx::GET_DEFAULT, keys));
    return false;
  }
  if (m_CapsAnswered) {
    m_CapsAnswered = false;
    m_CapsProbe.answered(m_CapsAnswerL5);
  }
  bool poll = false;
  const Ubx::CapsProbe::Result result = m_CapsProbe.service(now, poll);
  if (poll) {
    std::vector<uint32_t> keys;
    for (const uint32_t key : Ubx::l5Keys()) {
      keys.push_back(key);
    }
    sendUbx(Ubx::valGet(Ubx::GET_DEFAULT, keys));
  }
  if (result == Ubx::CapsProbe::Result::WAITING) {
    return false;
  }
  const bool l5 = result == Ubx::CapsProbe::Result::L5;
  const bool answered = m_CapsProbe.answered();
  {
    const std::lock_guard<std::mutex> lock(m_UbxMutex);
    if (l5 && (m_UbxCaps.family != Ubx::FAMILY_F10)) {
      // The four L5 keys exist only in the dual-band firmware (M10 IFD 4.9.21
      // page 152 has none, F10-IFD 4.9.20 pages 158 and 159 has all four), so a
      // receiver that answers with them is the F10 family whatever MON-VER
      // said. Take the whole family, not just the band: the same firmware has
      // no CFG-PM group and no GLONASS key (F10-IFD 4.9, and the module manual's
      // appendix migration table page 64), and writing those would NAK.
      m_UbxCaps = Ubx::CAPS_F10;
    } else {
      m_UbxCaps.bands = l5 ? static_cast<uint8_t>(Ubx::BAND_L1 | Ubx::BAND_L5) : Ubx::BAND_L1;
    }
    // Only a real answer counts as evidence. A timeout also resolves to L1, for
    // safety, but the status says it is a guess.
    m_UbxCapsProbed = answered;
  }
  ESP_LOGI(LOG_TAG, "GPS receiver bands: L1%s (%s)", l5 ? " and L5" : "",
           !answered ? "the receiver did not answer, assuming L1"
           : l5      ? "the L5 signal keys are implemented"
                     : "no L5 signal key in the firmware");
  return true;
#endif
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
  m_ConfigSettle.clear();
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
  {
    const std::lock_guard<std::mutex> lock(m_UbxMutex);
    request.caps = m_UbxCaps;
  }
  const Ubx::Plan plan = Ubx::plan(request);
  if (plan.gnss_refused) {
    ESP_LOGW(LOG_TAG, "GPS constellation mask 0x%02x is not valid on this receiver, left alone",
             plan.gnss);
  }
  if (plan.psm_refused) {
    ESP_LOGW(LOG_TAG, "GPS power save is refused with BeiDou B1C, left at full power");
  }
  if (plan.psm_unsupported) {
    ESP_LOGW(LOG_TAG, "GPS power save is not implemented on this receiver, left at full power");
  }
#if FURBLE_GPS_L5
  if (plan.b1i_refused) {
    ESP_LOGI(LOG_TAG, "GPS BeiDou uses B1C and B2a on a dual-band receiver, B1I is not written");
  }
#endif
  if (request.ana && !plan.ana) {
    ESP_LOGW(LOG_TAG, "GPS AssistNow Autonomous is not supported with BeiDou B1C, left off");
  }
#if FURBLE_GPS_L5
  if (plan.bands != 0) {
    ESP_LOGI(
        LOG_TAG, "GPS signals: %s, %s", (plan.bands & Ubx::BAND_L5) != 0 ? "L1 and L5" : "L1",
        (plan.bands & Ubx::BAND_L5) != 0 ? "both bands of each enabled constellation" : "L1 only");
  }
#endif

  constexpr uint8_t LAYERS = Ubx::LAYER_RAM | Ubx::LAYER_BBR;
  constexpr uint16_t SIGNAL_SETTLE_MS = 500;
  // Every value written, for the closing read-back. A plain loop instead of a
  // range insert keeps one vector growth path in the image.
  m_UbxExpected.clear();
  for (const auto *items : {&plan.base, &plan.signals, &plan.power}) {
    for (const auto &item : *items) {
      m_UbxExpected.push_back(Ubx::KeyValue(item));
    }
  }
  enqueueUbxConfig(Ubx::ID_CFG_VALSET, Ubx::valSet(LAYERS, plan.base));
  // Entering power save keeps OPERATEMODE last, but leaving it for BeiDou B1C
  // has to come first: the receiver rejects a signal configuration that
  // includes B1C while a power save mode is active (RN 7.2 page 15).
  if (plan.power_before_signals && !plan.power.empty()) {
    enqueueUbxConfig(Ubx::ID_CFG_VALSET, Ubx::valSet(LAYERS, plan.power));
  }
  if (!plan.signals.empty()) {
    enqueueUbxConfig(Ubx::ID_CFG_VALSET, Ubx::valSet(LAYERS, plan.signals), SIGNAL_SETTLE_MS);
  }
  if (!plan.power_before_signals && !plan.power.empty()) {
    // A receiver without power save modes has an empty power list, and an
    // empty CFG-VALSET payload is a poll for a message with no poll form. It is
    // skipped rather than sent.
    enqueueUbxConfig(Ubx::ID_CFG_VALSET, Ubx::valSet(LAYERS, plan.power));
  }
  if (!plan.unset.empty()) {
    enqueueUbxConfig(Ubx::ID_CFG_VALDEL, Ubx::valDel(Ubx::LAYER_BBR, plan.unset));
  }
  std::vector<uint32_t> keys;
  for (const auto &item : m_UbxExpected) {
    keys.push_back(item.key);
  }
  enqueueUbxConfig(Ubx::ID_CFG_VALGET, Ubx::valGet(Ubx::GET_RAM, keys));

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
    m_UbxPlan = static_cast<const Ubx::PlanSummary &>(plan);
    m_UbxAna = plan.ana;
    m_UbxConfigured = true;
  }

  // Ask whether the receiver restored a save-on-shutdown backup at start-up
  // (IFD 3.19.1). Not a CFG message, so it is not acknowledged.
  sendUbx(Ubx::sosPoll());
}

void GPS::enqueueUbxConfig(uint8_t message_id,
                           const std::vector<uint8_t> &frame,
                           uint16_t settle_ms) {
  // The queue holds payloads; an empty frame (an encoder refusal) queues an
  // empty one.
  std::vector<uint8_t> payload;
  if (frame.size() >= Ubx::FRAME_OVERHEAD) {
    payload.assign(frame.begin() + Ubx::HEADER_SIZE, frame.end() - Ubx::CHECKSUM_SIZE);
  }
  enqueueConfig(Ubx::CLASS_CFG, message_id, payload, "", false, false, true, settle_ms);
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
  const backend_t backend = getBackend();
#if !defined(FURBLE_GPS_TEXT) || FURBLE_GPS_TEXT
  if ((backend == backend_t::QUECTEL) || (backend == backend_t::UNICORE)) {
    // A text part saves with its own command, which never stops the GNSS first.
    return textSave(now);
  }
#endif
  // A rail that is already off has nothing left to save. A late duty request
  // can arrive just after the cut, and must not mark the backup as refused.
  // A rail the board cannot cut must not lose a running receiver to a GNSS
  // stop that nothing would undo.
  if ((getBackend() != backend_t::UBX) || Platform::hasGpsBackupSupply()
      || !Platform::canCutGpsRail() || (m_CycleState == cycle_state_t::RAIL_OFF)) {
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
  m_UbxStopped = true;
  sendUbx(Ubx::sosCommand(Ubx::SOS_CREATE));
  m_RailOffPending = true;
  m_RailOffDeadline = now + UBX_ACK_TIMEOUT_MS;
  return true;
}

void GPS::saveOnShutdownBlocking(void) {
  const backend_t backend = getBackend();
#if !defined(FURBLE_GPS_TEXT) || FURBLE_GPS_TEXT
  if ((backend == backend_t::QUECTEL) || (backend == backend_t::UNICORE)) {
    // disable() parks the GPS task first, so the text save can read the UART.
    textSaveBlocking();
    return;
  }
#endif
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
    // Decode straight into the shared fix: a local NavPvt copy sits on the GPS
    // task's deepest stack path. parseNavPvt() writes nothing when it refuses
    // a frame, so a malformed one leaves the last fix intact.
    const std::lock_guard<std::mutex> lock(m_GPSMutex);
    if (!Ubx::parseNavPvt(payload, n, m_UbxPvt)) {
      return;
    }
    m_UbxPvtTick = Platform::getInstance().tick();
    m_UbxPvtValid = true;
    m_UbxPvtCount++;
    // the same one-fix-per-burst rule the NMEA path applies in processNmea()
    if (m_UbxPvt.positionValid() && (m_FixSequence.load() != m_BurstSequence.load())) {
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
      {
        const std::lock_guard<std::mutex> lock(m_UbxMutex);
        m_UbxNaks++;
        if ((ack.cls == Ubx::CLASS_UPD) && (ack.id == Ubx::ID_UPD_SOS)) {
          m_UbxSosBackup = 2;
        }
      }
      // A refused CFG-VALGET while the capability probe is out is the L1
      // answer: the firmware does not know one of the L5 keys (M10 IFD 3.10.4
      // page 53, F10-IFD 3.10.4 pages 54 and 55).
#if FURBLE_GPS_L5
      if ((m_CapsProbe.result() == Ubx::CapsProbe::Result::WAITING) && (ack.cls == Ubx::CLASS_CFG)
          && (ack.id == Ubx::ID_CFG_VALGET)) {
        m_CapsAnswerL5 = false;
        m_CapsAnswered = true;
        return;
      }
#endif
      ESP_LOGW(LOG_TAG, "GPS UBX %02X/%02X was refused by the receiver", ack.cls, ack.id);
    }
    if (!m_ConfigInFlight.has_value() || !m_ConfigInFlight->ubx
        || (ack.cls != m_ConfigInFlight->class_id) || (ack.id != m_ConfigInFlight->message_id)) {
      return;
    }
    if (ack.ack && (ack.id == Ubx::ID_CFG_VALGET)) {
      // the data answers a VALGET, the acknowledgement only follows it
      return;
    }
#if FURBLE_GPS_L5
    if (!ack.ack) {
      // A refused signal batch that carried an L5 key is the one case the probe
      // did not catch: the firmware answered for the keys but refused to apply
      // them. Keep the L1 signals for the rest of the session rather than
      // retrying the same batch.
      uint8_t layers = 0;
      std::vector<Ubx::KeyValue> items;
      bool l5 = false;
      if (Ubx::parseValSet(m_ConfigInFlight->payload.data(), m_ConfigInFlight->payload.size(),
                           layers, items)) {
        for (const auto &item : items) {
          l5 = l5 || Ubx::isL5Key(item.key);
        }
      }
      if (l5) {
        // The batch carried L5 keys, but the refusal may belong to another key
        // in the same batch. Say what was observed, not what is guessed, and
        // drop L5 for the rest of the session so the next attempt is L1 clean.
        const std::lock_guard<std::mutex> lock(m_UbxMutex);
        m_UbxCaps.bands = Ubx::BAND_L1;
        m_UbxPlan.l5_refused = true;
        ESP_LOGW(LOG_TAG,
                 "GPS receiver refused a signal batch that carried L5 keys; L5 is off from here");
      }
    }
#endif
    const uint16_t settle = m_ConfigInFlight->settle_ms;
    finishConfigCommand(ack.ack ? CONFIG_ACKED : CONFIG_NACKED);
    if (ack.ack && (settle != 0)) {
      m_ConfigSettle.arm(Platform::getInstance().tick(), settle);
    }
    return;
  }

  if ((cls == Ubx::CLASS_CFG) && (id == Ubx::ID_CFG_VALGET)) {
    Ubx::ValGet response;
    if (!Ubx::parseValGet(payload, n, response)) {
      return;
    }
#if FURBLE_GPS_L5
    if ((m_CapsProbe.result() == Ubx::CapsProbe::Result::WAITING)
        && (response.layer == Ubx::GET_DEFAULT)) {
      // The probe's answer: every L5 key present means the firmware implements
      // the band. A partial answer is not a capability furble will write.
      bool all = true;
      for (const uint32_t key : Ubx::l5Keys()) {
        all = all
              && std::any_of(response.items.begin(), response.items.end(),
                             [key](const Ubx::KeyValue &item) { return item.key == key; });
      }
      m_CapsAnswerL5 = all;
      m_CapsAnswered = true;
      return;
    }
#endif
    if (!m_ConfigInFlight.has_value() || !m_ConfigInFlight->ubx
        || (m_ConfigInFlight->message_id != Ubx::ID_CFG_VALGET)) {
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
    {
      // parseMonVer() writes nothing when it refuses a frame.
      const std::lock_guard<std::mutex> lock(m_UbxMutex);
      if (!Ubx::parseMonVer(payload, n, m_UbxVersion)) {
        return;
      }
      m_UbxHaveVersion = true;
      // MON-VER names the firmware family, which is what the capability line
      // reports until the probe answers. The bands it grants are only a hint:
      // serviceCaps() asks the receiver before any L5 key is written. Once the
      // probe or a refused batch has decided the bands, a late MON-VER answer
      // does not reopen them.
#if FURBLE_GPS_L5
      if (!m_UbxCapsProbed) {
        m_UbxCaps = Ubx::capsFromVersion(m_UbxVersion);
      }
#endif
    }
    const backend_t backend = getBackend();
    if ((backend == backend_t::UBX) || (backend == backend_t::UNKNOWN)) {
      // already identified, or detection has not started and will poll
      return;
    }
    {
      const std::lock_guard<std::mutex> lock(m_UbxMutex);
      ESP_LOGI(LOG_TAG, "GPS u-blox receiver: %s", m_UbxVersion.sw.c_str());
    }
    if (backend == backend_t::DETECTING) {
      // serviceDetect() picks the answer up and configures the receiver
      m_Detect.answered();
      return;
    }
    // A late answer after detection gave up. The receiver is u-blox after
    // all, so configure it as one; the CASIC frames it got were ignored.
    m_Backend.store(static_cast<uint8_t>(backend_t::UBX));
    {
      // Only the character count: a whole status snapshot here would sit on
      // the deepest GPS task path, under the read buffer in serviceSerial().
      const std::lock_guard<std::mutex> lock(m_GPSMutex);
      m_ConfigChars = m_GPS.charsProcessed();
    }
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
  // The NMEA parser ran on the same stream. When it holds a fix markedly
  // newer than this NAV-PVT (a lost or refused PVT output), the NMEA fix wins.
  // ponytail: a fixed two-epoch window at 1 Hz; scale with the rate if PVT
  // and NMEA ever run at different rates.
  constexpr uint32_t PVT_NMEA_SLACK_MS = 2000;
  if (status.fix && ((status.location_age + PVT_NMEA_SLACK_MS) < age)) {
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
  status.time_resolved = pvt.timeValid();
  if (pvt.dateTimeValid()) {
    // A valid but not yet fully resolved time keeps the fix: geotagging needs
    // the date and time, only the clock sync waits for full resolution.
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
#if FURBLE_GPS_L5
    status.bands = m_UbxCaps.bands;
    status.bands_probed = m_UbxCapsProbed;
    status.family = m_UbxCaps.family;
    status.psm_supported = m_UbxCaps.psm;
#endif
    status.gnss = m_UbxPlan.gnss;
    status.psm = m_UbxPlan.psm;
    status.rate_ms = m_UbxPlan.rate_ms;
    status.ana = m_UbxAna;
    status.gnss_refused = m_UbxPlan.gnss_refused;
    status.psm_refused = m_UbxPlan.psm_refused;
    status.psm_unsupported = m_UbxPlan.psm_unsupported;
    status.rate_clamped = m_UbxPlan.rate_clamped;
#if FURBLE_GPS_L5
    status.l5_written = (m_UbxPlan.bands & Ubx::BAND_L5) != 0;
    status.b1i_refused = m_UbxPlan.b1i_refused;
    status.l5_refused = m_UbxPlan.l5_refused;
#endif
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
