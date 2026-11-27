/*
 * The vendor text backends of the GPS driver: Quectel $PAIR and $PQTM, Unicore
 * $CFG. The protocol itself is the pure code in lib/furble/protocol/GpsText,
 * GpsQuectel and GpsUnicore; plans/388-gnss-vendor-text.md cites the
 * Quectel and Unicore documents for every command, and
 * docs/protocols/gnss-quectel-pair.md and docs/protocols/gnss-unicore-cfg.md
 * hold the tables.
 *
 * The split with src/FurbleGPSUbx.cpp is the same one the UBX backend uses: the
 * codec decides what to send and how a reply reads, and this file owns the UART
 * writes, the one-command-in-flight queue, and the settings the plan is built
 * from.
 *
 * FURBLE_GPS_TEXT turns the backend off for a build that has no room for it:
 * the seam, both dialects and this file cost about 20 KB of flash, and the Core
 * debug profile's 1,740,800 byte OTA slot cannot take it. Off means the stubs
 * below, no allocation, and the NMEA path unchanged; every other build keeps
 * the backend. plans/388-gnss-vendor-text.md section 9 records the measurement.
 */
#include <algorithm>
#include <array>
#include <memory>
#include <new>
#include <string>

#include <esp_log.h>

#include "FurbleGPS.h"
#include "FurblePlatform.h"
#include "FurbleSettings.h"
#include "FurbleTypes.h"

#if !defined(FURBLE_GPS_TEXT) || FURBLE_GPS_TEXT

namespace Furble {

namespace {

/** The GpsText::Op a plugin reports, clamped into the status byte. */
uint8_t opByte(GpsText::Op op) {
  return static_cast<uint8_t>(op);
}

uint8_t verdictByte(GpsText::Verdict verdict) {
  return static_cast<uint8_t>(verdict);
}

}  // namespace

GpsText::Dialect GPS::textIdentity(void) const {
  return static_cast<GpsText::Dialect>(m_TextIdentity.load());
}

bool GPS::ensureTextState(void) {
  const std::lock_guard<std::mutex> lock(m_TextMutex);
  if (m_TextState == nullptr) {
    // Before any gate measures free bytes: enable() calls this first, and the
    // UART task cannot reach a text path until enable() has published it. The
    // allocation goes straight into an owner, so the only raw pointer is the
    // singleton member that freeTextState() hands back to one.
    std::unique_ptr<text_state_t> state(new (std::nothrow) text_state_t());
    if (!state) {
      ESP_LOGE(LOG_TAG, "GPS text backend state allocation failed, the text backends stay off");
      return false;
    }
    m_TextState = state.release();
  }
  return true;
}

void GPS::freeTextState(void) {
  std::unique_ptr<text_state_t> state;
  {
    const std::lock_guard<std::mutex> lock(m_TextMutex);
    state.reset(m_TextState);
    m_TextState = nullptr;
  }
  // Outside the lock: the destructor may run while a reader waits, and a reader
  // that already copied the pointer cannot exist here, because disable() holds
  // the service mutex and the only other reader takes this mutex.
}

void GPS::resetText(void) {
  m_TextIdentity.store(static_cast<uint8_t>(GpsText::Dialect::NONE));
  if (m_TextState == nullptr) {
    return;
  }
  text().session.clear();
  text().stopped = false;
  text().save_session.clear();
  text().save_queue.clear();
  text().save_next = 0;
  text().save_refused = false;
  const std::lock_guard<std::mutex> lock(m_TextMutex);
  text().status = {};
}

GPS::text_status_t GPS::getTextStatus(void) const {
  const std::lock_guard<std::mutex> lock(m_TextMutex);
  if (m_TextState == nullptr) {
    return text_status_t();
  }
  return m_TextState->status;
}

bool GPS::sendText(const GpsText::Command &command) {
  if (m_TextState == nullptr) {
    // no allocation, no text backend: the receiver keeps the NMEA path
    return false;
  }
  const std::string frame = GpsText::frame(command.body);
  if (frame.empty()) {
    ESP_LOGW(LOG_TAG, "GPS text command %s has no frame", GpsText::opName(command.op));
    return false;
  }
  const std::lock_guard<std::mutex> lock(m_TxMutex);
  const int written = uart_write_bytes(m_UART, frame.data(), frame.size());
  uart_wait_tx_done(m_UART, pdMS_TO_TICKS(TX_MS));
  if (written != static_cast<int>(frame.size())) {
    ESP_LOGE(LOG_TAG, "GPS text write failed: %d of %u", written,
             static_cast<unsigned>(frame.size()));
    return false;
  }
  return true;
}

void GPS::sendIdentifyProbes(void) {
  // Three probes in one window: a receiver answers the one it implements and
  // ignores the rest. Sent together so the NMEA fallback costs the same two
  // seconds it always did, rather than one window per vendor.
  sendUbx(Ubx::pollMonVer());
  sendText(Quectel::verNo());
  sendText(Unicore::productInfo());
}

void GPS::serviceText(const uint8_t *data, size_t length) {
  if (m_TextState == nullptr) {
    return;
  }
  GpsText::Line line;
  if (!GpsText::parse(reinterpret_cast<const char *>(data), length, line)) {
    return;
  }
  if (!line.checksum_ok) {
    // A corrupt line is dropped rather than half believed: the retry covers a
    // lost acknowledgement, and a broken line may never identify a receiver.
    ESP_LOGW(LOG_TAG, "GPS vendor reply %s failed its checksum", line.name.c_str());
    return;
  }
  if (!m_ConfigSettle.open(Platform::getInstance().tick()) && GpsText::windowed(line)) {
    // A Unicore $OK names no command (UC 3.2 page 30), so one that arrives
    // after its command failed would be credited to the next one. The window
    // is armed by that failure and this drops what falls inside it.
    ESP_LOGW(LOG_TAG, "GPS vendor reply %s arrived inside the settle window", line.name.c_str());
    return;
  }

  bool identified = false;
  GpsText::Dialect identity = GpsText::Dialect::NONE;
  std::string model;
  std::string firmware;
  if ((line.dialect == GpsText::Dialect::QUECTEL) && (line.name == "PQTMVERNO")) {
    // $PQTMVERNO,<VerStr>,<BuildDate>,<BuildTime> (Q76 2.4.5 page 100). An
    // ERROR reply is still a Quectel answer.
    if (!line.fields.empty() && (line.fields[0] != "ERROR")) {
      model = line.fields[0];
      if (line.fields.size() >= 3) {
        firmware = line.fields[1] + " " + line.fields[2];
      }
    } else if (!line.fields.empty()) {
      model = "error";
    }
    identified = !line.fields.empty();
    identity = GpsText::Dialect::QUECTEL;
  } else if ((line.dialect == GpsText::Dialect::UNICORE) && (line.name == "PDTINFO")) {
    // $PDTINFO,<pdtName>,<config>,<hwVer>,<fwVer>,<PN>,<SN> (UC 3.2 Table 3-2,
    // page 26).
    if (line.fields.size() >= 4) {
      model = line.fields[0] + " " + line.fields[1];
      firmware = line.fields[3];
    } else if (!line.fields.empty()) {
      model = line.fields[0];
    }
    identified = true;
    identity = GpsText::Dialect::UNICORE;
  } else if (line.dialect == GpsText::Dialect::NONE) {
    return;
  }

  if (identified) {
    {
      const std::lock_guard<std::mutex> lock(m_TextMutex);
      text().status.have_version = !model.empty();
      text().status.model = model;
      text().status.firmware = firmware;
    }
    m_TextIdentity.store(static_cast<uint8_t>(identity));
    if (!model.empty()) {
      ESP_LOGI(LOG_TAG, "GPS %s receiver: %s %s",
               (identity == GpsText::Dialect::QUECTEL) ? "Quectel" : "Unicore", model.c_str(),
               firmware.c_str());
    }
    if (getBackend() == backend_t::NMEA) {
      // A late answer after detection gave up. The receiver is what it is, so
      // configure it as that backend; the CASIC frames it got were ignored.
      m_Backend.store(static_cast<uint8_t>(
          (identity == GpsText::Dialect::QUECTEL) ? backend_t::QUECTEL : backend_t::UNICORE));
      {
        // Only the character count: a whole snapshot here would sit on the
        // deepest GPS task path.
        const std::lock_guard<std::mutex> lock(m_GPSMutex);
        m_ConfigChars = m_GPS.charsProcessed();
      }
      m_ConfigStart = Platform::getInstance().tick();
      m_ConfigPending = true;
      return;
    }
  }

  const GpsText::Verdict verdict = text().session.accept(line);
  if (verdict != GpsText::Verdict::NONE) {
    const std::lock_guard<std::mutex> lock(m_TextMutex);
    text().status.last_op = opByte(text().session.command().op);
    text().status.last_verdict = verdictByte(verdict);
    if (verdict == GpsText::Verdict::ACKED) {
      text().status.acks++;
    } else if (GpsText::refused(verdict)) {
      // a "being processed" answer is neither an acknowledgement nor a refusal
      text().status.naks++;
    }
    // The line belongs to the configuration session, so it does not also go to
    // the save session: one bare $OK must not be counted twice.
    return;
  }
  if (text().save_session.active()) {
    const GpsText::Verdict saved = text().save_session.accept(line);
    if (saved == GpsText::Verdict::ACKED) {
      const std::lock_guard<std::mutex> lock(m_TextMutex);
      text().status.acks++;
    }
  }
}

GpsText::Request GPS::textRequest(bool sentences) const {
  GpsText::Request request = {};
  const uint8_t rate = Settings::load<Settings::GPS_RATE>();
  request.rate_ms = (rate < RATE_MS.size()) ? RATE_MS[rate] : 0;

  const uint8_t gnss = Settings::load<Settings::GPS_GNSS>();
  const uint8_t constellation = Settings::load<Settings::GPS_CONSTEL>();
  if ((gnss != 0) && (gnss <= GpsText::GNSS_ALL)) {
    request.gnss = gnss;
    request.gnss_set = GpsText::GNSS_ALL;
  } else if ((constellation > 0) && (constellation <= CONSTELLATION_MAX)) {
    // One mask, one mapping: the CASIC constellation value is the same three
    // low bits the UBX planner maps, and the mask values are shared with it.
    request.gnss = Ubx::gnssFromConstellation(constellation);
    request.gnss_set = GpsText::GNSS_ALL;
  }
  request.psm = Settings::load<Settings::GPS_PSM>();
  request.duty_s = m_DutySeconds;
  request.platform = Settings::load<Settings::GPS_PLATFORM>();
  if (sentences && Settings::load<Settings::GPS_NMEA>()) {
    // The setting is "cut the receiver down", so off means leave the sentence
    // set alone, exactly as the CASIC path sends no $PCAS03 then. A receiver
    // that a previous session pruned stays pruned until it is power cycled,
    // which is the behaviour the CASIC path already has.
    request.sentences_set = true;
    request.sentences = GpsText::SentenceSet::PRUNE;
  }
  return request;
}

void GPS::publishTextPlan(const GpsText::Request &request,
                          uint16_t rate_ms,
                          uint8_t gnss,
                          bool gnss_refused,
                          bool sbas,
                          bool sbas_refused,
                          uint8_t platform,
                          bool platform_refused,
                          bool rate_refused,
                          bool psm_refused) {
  const std::lock_guard<std::mutex> lock(m_TextMutex);
  text().status.configured = true;
  text().status.rate_ms = rate_ms;
  text().status.gnss = gnss;
  text().status.gnss_refused = gnss_refused;
  text().status.sbas = sbas;
  text().status.sbas_refused = sbas_refused;
  text().status.platform = platform;
  text().status.platform_refused = platform_refused;
  text().status.rate_refused = rate_refused;
  text().status.psm_refused = psm_refused;
  text().status.sentences_set = request.sentences_set;
  text().status.sentences = static_cast<uint8_t>(request.sentences);
}

void GPS::enqueueTextConfig(const GpsText::Command &command) {
  size_t status_index;
  {
    const std::lock_guard<std::mutex> lock(m_ConfigMutex);
    if (m_ConfigStatusCount >= MAX_CONFIG_COMMANDS) {
      ESP_LOGE(LOG_TAG, "GPS configuration queue is full, %s dropped", GpsText::opName(command.op));
      return;
    }
    status_index = m_ConfigStatusCount++;
    // The status record keeps the class and message columns for a binary
    // command. A text command has neither, so the operation fills the message
    // column and the class stays zero.
    m_ConfigStatus[status_index] = {
        0,
        opByte(command.op),
        CONFIG_QUEUED,
        0,
    };
  }

  binary_command_t entry = {};
  entry.status_index = status_index;
  entry.text = true;
  entry.text_command = command;
  m_ConfigQueue.push_back(std::move(entry));
}

void GPS::configureQuectel(void) {
  if (m_TextState == nullptr) {
    return;
  }
  m_ConfigQueue.clear();
  m_ConfigInFlight.reset();
  text().session.clear();
  {
    const std::lock_guard<std::mutex> lock(m_ConfigMutex);
    m_ConfigStatusCount = 0;
  }

  const GpsText::Request request = textRequest(true);
  const Quectel::Plan plan = Quectel::plan(request);
  if (plan.gnss_refused) {
    ESP_LOGW(LOG_TAG,
             "GPS constellation mask 0x%02x is not a documented Quectel combination, left alone",
             plan.gnss);
  }
  if (plan.sbas_refused) {
    ESP_LOGW(LOG_TAG, "GPS SBAS is not supported in Quectel fitness mode, left alone");
  }
  if (plan.platform_refused) {
    ESP_LOGW(LOG_TAG, "GPS platform setting has no Quectel navigation mode, left alone");
  }
  if (plan.rate_refused) {
    ESP_LOGW(LOG_TAG, "GPS rate is outside PAIR050's 100 to 1000 ms, left alone");
  }

  bool psm_refused = false;
  if (request.psm != GpsText::PSM_OFF) {
    GpsText::Command standby;
    psm_refused = !Quectel::standby(request.duty_s, standby);
    if (psm_refused) {
      ESP_LOGW(LOG_TAG,
               "GPS standby needs %u s or more on Quectel, %u s is not supported; the receiver "
               "stays awake",
               static_cast<unsigned>(Quectel::BACKUP_MIN_S), static_cast<unsigned>(request.duty_s));
    }
  }

  for (const auto &command : plan.commands) {
    enqueueTextConfig(command);
  }
  // Only Portable and Pedestrian have a PAIR080 mode, so those are the settings
  // that reach the receiver.
  const uint8_t platform = plan.platform_refused ? 0 : request.platform;
  publishTextPlan(request, plan.rate_ms, plan.gnss, plan.gnss_refused, plan.sbas, plan.sbas_refused,
                  platform, plan.platform_refused, plan.rate_refused, psm_refused);

  // The receiver's own rate, so the burst predictor expects what it will do.
  const uint16_t interval = (plan.rate_ms != 0) ? plan.rate_ms : RATE_MS[1];
  const std::lock_guard<std::mutex> lock(m_CycleMutex);
  m_RateMs = interval;
  m_ExpectedInterval = interval;
}

void GPS::configureUnicore(void) {
  if (m_TextState == nullptr) {
    return;
  }
  m_ConfigQueue.clear();
  m_ConfigInFlight.reset();
  text().session.clear();
  {
    const std::lock_guard<std::mutex> lock(m_ConfigMutex);
    m_ConfigStatusCount = 0;
  }

  const GpsText::Request request = textRequest(true);
  const Unicore::Plan plan = Unicore::plan(request);
  if (plan.gnss_refused) {
    ESP_LOGW(LOG_TAG, "GPS constellation mask 0x%02x breaks a Unicore rule, left alone", plan.gnss);
  }
  if (plan.platform_refused) {
    ESP_LOGW(LOG_TAG,
             "GPS platform setting has no Unicore dynamic model, left alone (CFGDYN has portable "
             "and static only)");
  }
  if (plan.psm_refused) {
    ESP_LOGW(LOG_TAG, "GPS standby has no documented Unicore command, the receiver stays awake");
  }
  if (plan.rate_refused) {
    ESP_LOGW(LOG_TAG, "GPS rate is outside CFGNAV's 50 to 1000 ms, left alone");
  }

  for (const auto &command : plan.commands) {
    enqueueTextConfig(command);
  }
  publishTextPlan(request, plan.rate_ms, plan.gnss, plan.gnss_refused, false, false, plan.platform,
                  plan.platform_refused, plan.rate_refused, plan.psm_refused);

  const uint16_t interval = (plan.rate_ms != 0) ? plan.rate_ms : RATE_MS[1];
  const std::lock_guard<std::mutex> lock(m_CycleMutex);
  m_RateMs = interval;
  m_ExpectedInterval = interval;
}

bool GPS::textStandby(uint32_t now) {
  (void)now;
  if (m_TextState == nullptr) {
    return false;
  }
  const backend_t backend = getBackend();
  GpsText::Command command;
  bool supported = false;

  if (backend == backend_t::QUECTEL) {
    supported = Quectel::standby(m_DutySeconds, command);
    if (!supported) {
      ESP_LOGW(LOG_TAG,
               "GPS Quectel backup mode needs %u s or more, %u s is not supported; the receiver "
               "stays awake",
               static_cast<unsigned>(Quectel::BACKUP_MIN_S), static_cast<unsigned>(m_DutySeconds));
    }
  } else if (backend == backend_t::UNICORE) {
    ESP_LOGW(LOG_TAG,
             "GPS Unicore documents no standby command, the receiver stays awake for the whole "
             "duty cycle");
  } else {
    return false;
  }

  {
    const std::lock_guard<std::mutex> lock(m_TextMutex);
    text().status.standby = supported ? 1 : 2;
  }
  if (!supported) {
    return false;
  }
  // One frame, no retry: the receiver stops accepting commands once it is in
  // backup mode (Q76 2.3.46 page 69), so a resend would only wake it.
  return sendText(command);
}

void GPS::textSaveQueueBuild(void) {
  text().save_queue.clear();
  text().save_next = 0;
  text().save_refused = false;
  if (getBackend() == backend_t::QUECTEL) {
    // A fix rate above 1 Hz needs the GNSS engine off before it saves, and the
    // lock is what keeps the port answering a command after PAIR003 (Q76
    // 2.3.30 page 56, 2.3.45 note 2 page 69). Sent in that order whatever the
    // rate, because the cost is three short frames and the rail is going away.
    text().save_queue.push_back(Quectel::lockSleep());
    text().save_queue.push_back(Quectel::powerOff());
    // PAIR003 powers the GNSS engine off (Q76 2.3.3 page 34). If the rail cut
    // that follows is refused, railOffOrRestart() is the only path that can
    // bring it back, so the state is carried until it does.
    text().stopped = true;
    text().save_queue.push_back(Quectel::saveSettings());
    text().save_queue.push_back(Quectel::saveParameters());
    if (!Platform::hasGpsBackupSupply()) {
      // "If the backup domain cannot be powered ... this command needs to be
      // sent every time the parameters are modified" (Q76 2.3.44 note 1 page
      // 68).
      text().save_queue.push_back(Quectel::saveNavigation());
    }
    return;
  }
  text().save_queue.push_back(Unicore::save());
}

bool GPS::textSave(uint32_t now) {
  if (m_TextState == nullptr) {
    return false;
  }
  const backend_t backend = getBackend();
  // A rail that is already off has nothing left to save, and a rail the board
  // cannot cut must not lose a running receiver to a GNSS stop nothing undoes.
  if (((backend != backend_t::QUECTEL) && (backend != backend_t::UNICORE))
      || Platform::hasGpsBackupSupply() || !Platform::canCutGpsRail()
      || (m_CycleState == cycle_state_t::RAIL_OFF)) {
    return false;
  }
  {
    const std::lock_guard<std::mutex> lock(m_TextMutex);
    if (text().status.save == 2) {
      // refused once this session: do not retry on every duty cycle
      return false;
    }
    text().status.save = 0;
  }

  textSaveQueueBuild();
  text().save_session.clear();
  m_RailOffPending = true;
  m_RailOffDeadline = now + TEXT_SAVE_BUDGET_MS;
  // The first frame goes out now; the rest follow on the cycle passes.
  textSaveService(now);
  return true;
}

bool GPS::textSaveService(uint32_t now) {
  if ((m_TextState == nullptr) || text().save_queue.empty()) {
    return true;
  }

  if (!text().save_session.active()) {
    if (text().save_next >= text().save_queue.size()) {
      return true;
    }
    text().save_session.begin(text().save_queue[text().save_next], now);
    text().save_next++;
    if (!sendText(text().save_session.command())) {
      text().save_refused = true;
      return true;
    }
    return false;
  }

  switch (text().save_session.service(now)) {
    case GpsText::Session::Step::SEND:
      if (!sendText(text().save_session.command())) {
        text().save_refused = true;
        return true;
      }
      return false;
    case GpsText::Session::Step::WAIT:
      return false;
    case GpsText::Session::Step::ACKED:
      text().save_session.clear();
      // the next command starts on the next pass, which gives the receiver a
      // moment between a save and the frame that follows it
      return text().save_next >= text().save_queue.size();
    case GpsText::Session::Step::FAILED:
    case GpsText::Session::Step::TIMEOUT:
      text().save_refused = true;
      return true;
    case GpsText::Session::Step::IDLE:
      return true;
  }
  return true;
}

void GPS::textSaveBlocking(void) {
  if (!textSave(Platform::getInstance().tick())) {
    // textSave() refuses when there is nothing to do, and it has recorded why
    // in the status the console prints.
    return;
  }
  const uint32_t start = Platform::getInstance().tick();
  std::array<uint8_t, 64> buffer;
  std::string pending;
  bool done = false;
  while (!done && ((Platform::getInstance().tick() - start) < TEXT_SAVE_BUDGET_MS)) {
    // disable() parks the GPS task first, so this is the only UART reader.
    const int read = uart_read_bytes(m_UART, buffer.data(), buffer.size(), pdMS_TO_TICKS(20));
    for (int i = 0; i < read; i++) {
      const char c = static_cast<char>(buffer[i]);
      if ((c != '\r') && (c != '\n')) {
        pending.push_back(c);
        if (pending.size() > GpsText::MAX_LINE) {
          pending.clear();
        }
        continue;
      }
      if (pending.empty()) {
        continue;
      }
      GpsText::Line line;
      if (GpsText::parse(pending.data(), pending.size(), line) && line.checksum_ok) {
        (void)text().save_session.accept(line);
      }
      pending.clear();
    }

    switch (text().save_session.service(Platform::getInstance().tick())) {
      case GpsText::Session::Step::SEND:
        (void)sendText(text().save_session.command());
        break;
      case GpsText::Session::Step::ACKED:
        text().save_session.clear();
        if (text().save_next >= text().save_queue.size()) {
          done = true;
        } else {
          text().save_session.begin(text().save_queue[text().save_next],
                                    Platform::getInstance().tick());
          text().save_next++;
          (void)sendText(text().save_session.command());
        }
        break;
      case GpsText::Session::Step::FAILED:
      case GpsText::Session::Step::TIMEOUT:
        text().save_refused = true;
        done = true;
        break;
      case GpsText::Session::Step::IDLE:
      case GpsText::Session::Step::WAIT:
        break;
    }

    if (read <= 0) {
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
  if (!done) {
    text().save_refused = true;
  }
  // the verdict before the session is cleared, so a refusal the receiver stated
  // is reported as that refusal rather than as an unanswered frame
  const GpsText::Verdict result = text().save_session.verdict();
  text().save_session.clear();
  text().save_queue.clear();
  m_RailOffPending = false;
  {
    const std::lock_guard<std::mutex> lock(m_TextMutex);
    text().status.save = text().save_refused ? 2 : 1;
    text().status.last_op = opByte(GpsText::Op::SAVE);
    text().status.last_verdict =
        verdictByte(text().save_refused
                        ? ((result != GpsText::Verdict::NONE) ? result : GpsText::Verdict::TIMEOUT)
                        : GpsText::Verdict::ACKED);
  }
}

void GPS::textPowerBackOn(void) {
  if ((m_TextState == nullptr) || !text().stopped) {
    return;
  }
  // The save powered the GNSS engine off and the cut that was supposed to
  // follow did not happen, so the receiver is powered with no GNSS. On a
  // Quectel part that is PAIR002 (Q76 2.3.2 page 34) or, where that command
  // does not exist, a hot start (Q76 2.3.4 page 34).
  text().stopped = false;
  if (getBackend() != backend_t::QUECTEL) {
    return;
  }
  const GpsText::Command command = Quectel::powerBackOn(getTextStatus().model);
  ESP_LOGW(LOG_TAG, "GPS rail stayed on after PAIR003, sending %s to restart the GNSS engine",
           command.body.c_str());
  if (!sendText(command)) {
    ESP_LOGE(LOG_TAG, "GPS %s write failed, the GNSS engine stays off", command.body.c_str());
  }
}

bool GPS::textRestart(uint8_t mode) {
  const backend_t backend = getBackend();
  if (backend == backend_t::QUECTEL) {
    // thermal: PAIR004 hot, PAIR005 warm, PAIR006 cold (Q76 2.3.4 to 2.3.6)
    return sendText(Quectel::restart(mode));
  }
  if (backend == backend_t::UNICORE) {
    // $RESET,0,h00 hot, h01 warm, h85 cold (UC 3.2 Table 3-5 page 29)
    return sendText(Unicore::restart(mode));
  }
  return false;
}

}  // namespace Furble

#else  // FURBLE_GPS_TEXT

/*
 * The capability is off: keep every entry point the rest of the driver calls,
 * answer nothing, and allocate nothing. No text reply can arrive because
 * serviceText() drops what the splitter hands it, so no text backend is ever
 * detected and the receiver keeps the NMEA or UBX path it would have had.
 */
namespace Furble {

GpsText::Dialect GPS::textIdentity(void) const {
  return GpsText::Dialect::NONE;
}

GPS::text_status_t GPS::getTextStatus(void) const {
  return text_status_t();
}

bool GPS::ensureTextState(void) {
  return false;
}

void GPS::freeTextState(void) {}

void GPS::resetText(void) {
  m_TextIdentity.store(static_cast<uint8_t>(GpsText::Dialect::NONE));
}

bool GPS::sendText(const GpsText::Command &command) {
  (void)command;
  return false;
}

void GPS::sendIdentifyProbes(void) {
  // The text probes belong to the backend this build does not have; a u-blox
  // receiver still identifies itself, and every other receiver keeps NMEA.
  sendUbx(Ubx::pollMonVer());
}

void GPS::serviceText(const uint8_t *data, size_t length) {
  (void)data;
  (void)length;
}

void GPS::configureQuectel(void) {}

void GPS::configureUnicore(void) {}

bool GPS::textStandby(uint32_t now) {
  (void)now;
  return false;
}

bool GPS::textSave(uint32_t now) {
  (void)now;
  return false;
}

void GPS::textSaveBlocking(void) {}

bool GPS::textSaveService(uint32_t now) {
  (void)now;
  return true;
}

void GPS::textSaveQueueBuild(void) {}

bool GPS::textRestart(uint8_t mode) {
  (void)mode;
  return false;
}

void GPS::textPowerBackOn(void) {}

}  // namespace Furble

#endif  // FURBLE_GPS_TEXT
