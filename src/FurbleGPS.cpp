#include <M5Unified.h>
#include <TinyGPS++.h>
#include <lvgl.h>

#include "icons.h"

#include "FurbleConsole.h"
#include "FurbleControl.h"
#include "FurbleGPS.h"
#include "FurblePlatform.h"
#include "FurblePower.h"
#include "FurbleSettings.h"

void gps_task(void *param) {
  Furble::GPS *gps = static_cast<Furble::GPS *>(param);
  gps->task();
}

namespace Furble {

GPS &GPS::getInstance() {
  static GPS instance;

  if (instance.m_Task == NULL) {
    //  RX=33, TX=32 for Module GPS v2.1, currently unsupported
    const int8_t tx = M5.getPin(m5::port_a_pin2);
    const int8_t rx = M5.getPin(m5::port_a_pin1);

    const int baud = Settings::load<Settings::GPS_BAUD>();
    const uart_config_t uart_config = {
        .baud_rate = (int)baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
#if defined(FURBLE_M5STICKS3)
        // XTAL keeps the baud stable while DFS scales the APB clock
        .source_clk = UART_SCLK_XTAL,
#else
        .source_clk = UART_SCLK_REF_TICK,
#endif
        .flags = {},
    };
    uart_driver_install(instance.m_UART, BUFFER_SIZE, 0, QUEUE_SIZE, &instance.m_Queue,
                        ESP_INTR_FLAG_IRAM);
    uart_param_config(instance.m_UART, &uart_config);
    uart_set_pin(instance.m_UART, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_enable_pattern_det_baud_intr(instance.m_UART, '\n', 1, 9, 0, 0);
    uart_pattern_queue_reset(instance.m_UART, QUEUE_SIZE);
    uart_flush(instance.m_UART);

    BaseType_t err = xTaskCreate(gps_task, LOG_TAG, 4096, &instance, 3, &instance.m_Task);
    if (err != pdTRUE) {
      ESP_LOGE(LOG_TAG, "Failed to create gps task.");
      abort();
    }
  }

  return instance;
}

void GPS::init(void) {
  getInstance().reloadSetting();
}

void GPS::setIcon(lv_obj_t *icon) {
  m_Icon = icon;
}

void GPS::reset(void) {
  uart_flush(m_UART);
  xQueueReset(m_Queue);
}

void GPS::task(void) {
  while (true) {
    uart_event_t event;
    if (!m_Enabled) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (xQueueReceive(m_Queue, &event, pdMS_TO_TICKS(100))) {
      switch (event.type) {
        case UART_DATA:
          break;
        case UART_FIFO_OVF:
          ESP_LOGW(LOG_TAG, "GPS HW FIFO overflow");
          reset();
          break;
        case UART_BUFFER_FULL:
          ESP_LOGW(LOG_TAG, "GPS ring buffer full");
          reset();
          break;
        case UART_BREAK:
          ESP_LOGW(LOG_TAG, "GPS rx break");
          break;
        case UART_PARITY_ERR:
          ESP_LOGE(LOG_TAG, "GPS parity error");
          break;
        case UART_FRAME_ERR:
          ESP_LOGE(LOG_TAG, "GPS frame error");
          break;
        case UART_PATTERN_DET:
          serviceSerial();
          break;
        default:
          ESP_LOGW(LOG_TAG, "unknown uart event type: %d", event.type);
          break;
      }
    }

    serviceConfig();
  }
}

void GPS::enable(void) {
  const uint32_t baud = Settings::load<Settings::GPS_BAUD>();

  uart_set_baudrate(m_UART, baud);
  reset();

  // power on
  M5.Power.setExtOutput(true, m5::ext_PA);

#if defined(FURBLE_M5STICKS3)
  // ESP32S3 UART does not function with light sleep
  if (!m_PowerLock.has_value()) {
    m_PowerLock.emplace(Power::LockType::NO_LIGHT_SLEEP, POWER_LOCK_OWNER);
  }
#endif

  // the receiver is not ready for commands yet, ask the GPS task to configure
  // it once sentences arrive
  m_ConfigChars = m_GPS.charsProcessed();
  m_ConfigStart = Platform::getInstance().tick();
  m_ConfigPending = true;
}

/** XOR of every character between '$' and '*', both excluded. */
uint8_t GPS::checksum(const std::string &payload) {
  uint8_t sum = 0;

  for (const char c : payload) {
    sum ^= static_cast<uint8_t>(c);
  }

  return sum;
}

/** Frame the payload as an NMEA sentence and send it to the receiver. */
void GPS::sendCommand(const std::string &payload) {
  std::array<char, 4> suffix;

  snprintf(suffix.data(), suffix.size(), "*%02X", checksum(payload));
  const std::string command = "$" + payload + suffix.data() + "\r\n";

  ESP_LOGI(LOG_TAG, "GPS command: %s", payload.c_str());

  uart_write_bytes(m_UART, command.data(), command.size());
  uart_wait_tx_done(m_UART, pdMS_TO_TICKS(TX_MS));
}

/** Restart the receiver, 0 hot, 1 warm, 2 cold. */
void GPS::restart(uint8_t mode) {
  if (!m_Enabled || (mode > 2)) {
    return;
  }

  sendCommand("PCAS10," + std::to_string(mode));
}

/**
 * Send the $PCAS configuration commands.
 *
 * Every setting defaults to 'do not send', so a device which has never been
 * configured leaves the receiver at its own defaults.
 */
void GPS::configure(void) {
  const uint8_t rate = Settings::load<Settings::GPS_RATE>();
  const bool prune = Settings::load<Settings::GPS_NMEA>();
  const uint8_t constellation = Settings::load<Settings::GPS_CONSTEL>();

  // prune first, the receiver cannot sustain the fast rates while it still
  // emits every sentence
  if (prune) {
    // GGA for altitude, satellites and fix quality, RMC for date, time and
    // position, nothing else is used
    sendCommand("PCAS03,1,0,0,0,1,0,0,0");
  }

  if ((rate > 0) && (rate < RATE_MS.size())) {
    sendCommand("PCAS02," + std::to_string(RATE_MS[rate]));
  }

  if ((constellation > 0) && (constellation <= CONSTELLATION_MAX)) {
    sendCommand("PCAS04," + std::to_string(constellation));
  }
}

/** Configure the receiver once it is awake, or give up and try anyway. */
void GPS::serviceConfig(void) {
  if (!m_Enabled || !m_ConfigPending) {
    return;
  }

  const bool awake = m_GPS.charsProcessed() > m_ConfigChars;
  const bool expired = (Platform::getInstance().tick() - m_ConfigStart) > SETTLE_MS;
  if (!awake && !expired) {
    return;
  }

  m_ConfigPending = false;
  configure();
}

void GPS::disable(void) {
  m_ConfigPending = false;

  // power off
  M5.Power.setExtOutput(false, m5::ext_PA);

#if defined(FURBLE_M5STICKS3)
  m_PowerLock.reset();
#endif
}

/** Refresh the setting from NVS. */
void GPS::reloadSetting(void) {
  m_Enabled = Settings::load<Settings::GPS>();
  if (m_Enabled) {
    enable();
  } else {
    disable();
  }
}

/** Is GPS enabled? */
bool GPS::isEnabled(void) const {
  return m_Enabled;
}

/** Start timer event to service/update GPS. */
void GPS::startService(void) {
  m_Timer = lv_timer_create(
      [](lv_timer_t *timer) {
        auto *gps = static_cast<GPS *>(lv_timer_get_user_data(timer));
        gps->update();
      },
      SERVICE_MS, this);
}

/** Send GPS data updates to the control task. */
void GPS::update(void) {
  if (!m_Enabled) {
    return;
  }

  if ((m_GPS.location.age() < MAX_AGE_MS) && m_GPS.location.isValid()
      && (m_GPS.date.age() < MAX_AGE_MS) && m_GPS.date.isValid() && (m_GPS.time.age() < MAX_AGE_MS)
      && m_GPS.time.isValid()) {
    m_HasFix = (m_GPS.location.FixQuality() != TinyGPSLocation::Quality::Invalid);
  } else {
    m_HasFix = false;
  }

  if (m_HasFix) {
    Camera::gps_t dgps = {
        m_GPS.location.lat(),
        m_GPS.location.lng(),
        m_GPS.altitude.meters(),
        m_GPS.satellites.value(),
    };
    Camera::timesync_t timesync = {
        m_GPS.date.year(),   m_GPS.date.month(),  m_GPS.date.day(),         m_GPS.time.hour(),
        m_GPS.time.minute(), m_GPS.time.second(), m_GPS.time.centisecond(),
    };

    Control::getInstance().updateGPS(dgps, timesync);
  }

  // setting the source invalidates the image and forces a decode, only do it
  // when the icon actually changes
  const lv_image_dsc_t *symbol = m_HasFix ? &icon_my_location : &icon_location_disabled;
  if ((m_Icon != NULL) && (m_IconSymbol != symbol)) {
    m_IconSymbol = symbol;
    lv_image_set_src(m_Icon, symbol);
  }
}

/** Read and decode the GPS data from serial port. */
void GPS::serviceSerial(void) {
  std::array<uint8_t, BUFFER_SIZE> buffer;

  if (!m_Enabled) {
    return;
  }

  int bytes = uart_read_bytes(m_UART, buffer.data(), buffer.size(), 1);
  if (bytes > 0) {
    Console::gpsRaw(reinterpret_cast<const char *>(buffer.data()), bytes);
    m_GPS.encode(reinterpret_cast<char *>(buffer.data()), bytes);
    captureSentences(reinterpret_cast<char *>(buffer.data()), bytes);
  }
}

/** Split the raw stream into sentences for the debug page. */
void GPS::captureSentences(const char *data, size_t length) {
  if (!m_Capture) {
    return;
  }

  std::lock_guard<std::mutex> lock(m_CaptureMutex);

  for (size_t i = 0; i < length; i++) {
    const char c = data[i];

    if ((c == '\r') || (c == '\n')) {
      if (!m_Partial.empty()) {
        m_Sentences[m_SentenceNext] = m_Partial;
        m_SentenceNext = (m_SentenceNext + 1) % m_Sentences.size();
        m_Partial.clear();
      }
    } else if (m_Partial.size() < SENTENCE_LEN) {
      m_Partial += c;
    }
  }
}

/** Start or stop raw NMEA sentence capture. */
void GPS::setCapture(bool capture) {
  std::lock_guard<std::mutex> lock(m_CaptureMutex);

  m_Capture = capture;
  if (!capture) {
    for (auto &sentence : m_Sentences) {
      sentence.clear();
    }
    m_SentenceNext = 0;
    m_Partial.clear();
  }
}

/** Get the captured raw NMEA sentences, oldest first. */
std::vector<std::string> GPS::getSentences(void) {
  std::vector<std::string> sentences;

  std::lock_guard<std::mutex> lock(m_CaptureMutex);

  for (size_t i = 0; i < m_Sentences.size(); i++) {
    const auto &sentence = m_Sentences[(m_SentenceNext + i) % m_Sentences.size()];
    if (!sentence.empty()) {
      sentences.push_back(sentence);
    }
  }

  return sentences;
}

TinyGPSPlus &GPS::get(void) {
  return m_GPS;
}

}  // namespace Furble
