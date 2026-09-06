#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <driver/uart.h>

#include "FurbleSettings.h"
#include "clock.h"

namespace {

QueueHandle_t gpsQueue = nullptr;
size_t gpsOffset = 0;
std::vector<std::string> uartWrites;
std::deque<uint8_t> rxBytes;
bool gpsEventQueued = false;
bool rxEventQueued = false;
uint32_t gpsNextEventMillis = 0;
std::mutex gpsMutex;
std::string uartMode = "ack";

constexpr uint8_t SYNC_0 = 0xBA;
constexpr uint8_t SYNC_1 = 0xCE;

constexpr char gpsData[] =
    "$GPRMC,123519.00,A,4807.038,N,01131.000,E,22.678,0.0,230394,,,A*67\r\n"
    "$GPGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*69\r\n";

// The same fix standing still. Fix hold extrapolation only projects a track
// above 2 m/s, so a scenario needs a receiver that reports motion below it to
// prove a parked user is never moved around by dead reckoning. Padded to the
// same length as the moving track so a burst already in flight stays coherent.
constexpr char gpsDataStationary[] =
    "$GPRMC,123519.00,A,4807.038,N,01131.000,E,00.412,0.0,230394,,,A*69\r\n"
    "$GPGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*69\r\n";

static_assert(sizeof(gpsDataStationary) == sizeof(gpsData),
              "both canned tracks must be the same length");

bool gpsStationary = false;

const char *gpsSentences(void) {
  return gpsStationary ? gpsDataStationary : gpsData;
// The 1 Hz fix burst. The default is the historic fixture above, byte for byte,
// so every existing scenario and documentation capture is unchanged. The
// "modern" variant only moves the RMC date inside the window
// TimeKeeperPolicy::valid() accepts, which the persisted clock and therefore
// the ephemeris cache freshness check both need.
std::string gpsStream(gpsData, sizeof(gpsData) - 1);

// The "modern" fix fixture advances its clock one second per burst, the way a
// receiver with a clock of its own does. The historic default fixture stays
// frozen so every scenario and capture written against it is unchanged.
bool fixDateAdvances = false;

// Seconds the advancing fixture clock has run for. It is seeded from the
// environment and handed back before a `restart` re-execs, because a real
// receiver's clock does not rewind when the ESP32 reboots. Letting it reset
// here made the positive ephemeris leg pass on a one second margin.
uint32_t fixSecond = [] {
  const char *carried = std::getenv("FURBLE_SIM_FIX_SECOND");
  return carried != nullptr ? static_cast<uint32_t>(std::strtoul(carried, nullptr, 10)) : 0u;
}();

// Bytes served per read from the fix burst, 0 for the whole burst at once. A
// real burst carrying GSA and GSV is larger than the driver's 256 byte read, so
// a sentence routinely spans two reads; scenarios set this to prove the parser
// path survives that.
size_t fixChunk = 0;

// When the burst is chunked, the chunks arrive over time the way bytes off a
// real UART do, rather than all being drained inside one serviceSerial() call.
// That is the whole point of chunking: GPS::task runs serviceSerial() and then
// servicePoll() in the same iteration, so a sentence half delivered leaves the
// ephemeris arm reading a parser that still holds the previous date.
uint32_t fixChunkNext = 0;
constexpr uint32_t FIX_CHUNK_MS = 20;

// Bursts of coarse time a cold-start receiver reports before it decodes TOW and
// corrects itself. Deliberately not carried across a restart: every boot is a
// fresh cold start. A date behind the cache is what furble sees during it.
uint32_t fixColdBursts = 0;
constexpr uint32_t FIX_COLD_BURSTS = 5;

// A receiver on a noisy line whose RMC always arrives corrupt. The sentence is
// there, so anything counting sentence names in the raw bytes counts it, but
// the parser rejects it and never commits a date from it.
bool fixRmcCorrupt = false;
// day-of-month of the advancing fixture. "modern" is the 6th and "stale" is
// the 13th, a week later, which is well past the four hour ephemeris window.
const char *fixDay = "06";

// Modelled receiver rate. 0 means the receiver answers whatever the driver
// programmed, which is the default and keeps every pre-existing scenario
// unchanged. A specific rate makes the receiver mute until the autobaud ladder
// reaches it, and receiverPresent false models no unit on the Grove port.
uint32_t uartBaud = 9600;
uint32_t receiverBaud = 0;
bool receiverPresent = true;

// Which GSV/GSA fixture the satellite page sees. The default set is the eight
// satellite fixture the documentation capture is pinned to; do not change its
// values without regenerating docs/img/gps-satellites.png.
std::string satelliteFixture = "default";

// Counters the scenarios assert on. Ephemeris replay and MON-HW polls are
// binary traffic, so a write count alone cannot see them.
uint32_t ephReplayFrames = 0;
uint32_t monHwPolls = 0;
bool monHwShort = false;

/** Rebuild the advancing fix burst at base 12:35:19 plus fixSecond. */
std::string modernFixStream(void);

/** Wrap an NMEA body in its '$' and its computed checksum. */
std::string nmea(const std::string &body) {
  uint8_t sum = 0;
  for (const char c : body) {
    sum ^= static_cast<uint8_t>(c);
  }
  char tail[8];
  std::snprintf(tail, sizeof(tail), "*%02X\r\n", sum);
  return "$" + body + tail;
}

/**
 * GSV and GSA fixtures for the satellite detail page.
 *
 * Each entry is a list of sentence bodies without the '$' or the checksum, so
 * a fixture cannot carry a stale hand computed checksum. The names are the
 * values of the `gps_sats` scenario seed.
 */
std::vector<std::string> satelliteBodies(const std::string &name) {
  if (name == "none") {
    // nothing in view, and a GSA with no used satellites and no DOP
    return {"GPGSV,1,1,00,1", "GPGSA,A,1,,,,,,,,,,,,,,,,1"};
  }
  if (name == "one") {
    return {"GPGSV,1,1,01,01,40,100,45,1", "GPGSA,A,2,01,,,,,,,,,,,,5.0,4.0,3.0,1"};
  }
  if (name == "twelve") {
    return {"GPGSV,3,1,12,01,40,100,45,02,30,200,44,03,20,300,43,04,10,050,42,1",
            "GPGSV,3,2,12,05,45,010,41,06,35,020,40,07,25,030,39,08,15,040,38,1",
            "GPGSV,3,3,12,09,50,060,37,10,55,070,36,11,60,080,35,12,65,090,34,1",
            "GPGSA,A,3,01,02,03,04,05,06,07,08,09,10,11,12,1.2,0.8,0.9,1"};
  }
  if (name == "partial") {
    // sentence 1 of 2 only, so the set can never complete
    return {"GPGSV,2,1,08,01,40,100,45,02,30,200,40,03,20,300,35,04,10,050,30,1",
            "GPGSA,A,3,01,02,03,04,,,,,,,,,2.5,2.0,1.5,1"};
  }
  if (name == "duplicate") {
    // the same PRN reported twice in one set, and twice in the GSA
    return {"GPGSV,2,1,04,01,40,100,45,01,40,100,45,02,30,200,40,02,30,200,40,1",
            "GPGSV,2,2,04,03,20,300,35,03,20,300,35,04,10,050,30,04,10,050,30,1",
            "GPGSA,A,3,01,01,02,02,,,,,,,,,2.5,2.0,1.5,1"};
  }
  if (name == "range") {
    // elevation, azimuth, C/N0 and PRN all past their documented maxima, plus
    // one satellite that is in range, which must be the only one published
    return {"GPGSV,1,1,04,01,91,100,45,02,30,400,40,03,20,300,120,70000,10,050,30,1",
            "GPGSA,A,3,01,,,,,,,,,,,,2.5,2.0,1.5,1"};
  }
  if (name == "multi") {
    return {"GPGSV,1,1,02,01,40,100,45,02,30,200,40,1", "GLGSV,1,1,02,65,50,110,44,66,20,210,33,1",
            "BDGSV,1,1,02,201,35,120,41,202,15,220,29,1", "GNGSA,A,3,01,02,,,,,,,,,,,2.5,2.0,1.5,1",
            "GNGSA,A,3,65,66,,,,,,,,,,,2.5,2.0,1.5,2"};
  }
  // default: the eight satellite documentation fixture
  return {"GPGSV,2,1,08,01,40,100,45,02,30,200,40,03,20,300,35,04,10,050,30,1",
          "GPGSV,2,2,08,05,45,010,50,06,35,020,42,07,25,030,38,08,15,040,33,1",
          "GPGSA,A,3,01,02,03,04,,,,,,,,,2.5,2.0,1.5,1"};
}

/** The bytes the fake receiver answers a GSV/GSA request with. */
std::vector<uint8_t> satelliteBytes(void) {
  std::string text;
  if (satelliteFixture == "malformed") {
    // a bad checksum, a sentence with no terminator field, a truncated line and
    // a run of raw noise. None of these may reach the satellite table.
    text =
        "$GPGSV,2,1,08,01,40,100,45,02,30,200,40,03,20,300,35,04,10,050,30,1*00\r\n"
        "$GPGSV,2,2,08,05,45\r\n"
        "$GPGSA,A,3,01,02\r\n"
        "$GPGS\r\n"
        "\x01\x02\x03\xff\xfe\r\n";
  } else {
    for (const auto &body : satelliteBodies(satelliteFixture)) {
      text += nmea(body);
    }
  }
  return std::vector<uint8_t>(text.begin(), text.end());
}

uint32_t gpsRatePeriodMillis(void) {
  constexpr uint32_t periods[] = {1000, 1000, 500, 200, 100};
  const uint8_t rate = Furble::Settings::load<Furble::Settings::GPS_RATE>();
  return rate < (sizeof(periods) / sizeof(periods[0])) ? periods[rate] : periods[0];
}

uint16_t readU16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t casicChecksum(uint8_t classId,
                       uint8_t messageId,
                       uint16_t length,
                       const uint8_t *payload) {
  uint32_t sum =
      (static_cast<uint32_t>(messageId) << 24) | (static_cast<uint32_t>(classId) << 16) | length;
  for (uint16_t offset = 0; offset < length; offset += 4) {
    uint32_t word = 0;
    for (uint16_t byte = 0; byte < 4 && offset + byte < length; byte++) {
      word |= static_cast<uint32_t>(payload[offset + byte]) << (byte * 8);
    }
    sum += word;
  }
  return sum;
}

std::vector<uint8_t> binaryFrame(uint8_t classId,
                                 uint8_t messageId,
                                 const std::vector<uint8_t> &payload) {
  const uint16_t length = static_cast<uint16_t>(payload.size());
  std::vector<uint8_t> frame = {
      SYNC_0,  SYNC_1,   static_cast<uint8_t>(length & 0xff), static_cast<uint8_t>(length >> 8),
      classId, messageId};
  frame.insert(frame.end(), payload.begin(), payload.end());
  const uint32_t sum = casicChecksum(classId, messageId, length, payload.data());
  frame.push_back(static_cast<uint8_t>(sum & 0xff));
  frame.push_back(static_cast<uint8_t>((sum >> 8) & 0xff));
  frame.push_back(static_cast<uint8_t>((sum >> 16) & 0xff));
  frame.push_back(static_cast<uint8_t>(sum >> 24));
  return frame;
}

/** Does the modelled receiver answer at the baud the driver has programmed? */
bool receiverAnswersLocked(void) {
  return receiverPresent && ((receiverBaud == 0) || (receiverBaud == uartBaud));
}

void queueRxLocked(const std::vector<uint8_t> &bytes) {
  rxBytes.insert(rxBytes.end(), bytes.begin(), bytes.end());
  if (!rxEventQueued && gpsQueue != nullptr) {
    const uart_event_t event = {.type = UART_DATA, .size = rxBytes.size()};
    rxEventQueued = xQueueSend(gpsQueue, &event, 0) == pdTRUE;
  }
}

void queueAckLocked(const uint8_t *request, bool ack) {
  const uint16_t length = readU16(request + 2);
  if (length > 2048 || (length % 4) != 0) {
    return;
  }
  queueRxLocked(binaryFrame(0x05, ack ? 0x01 : 0x00, {request[4], request[5], 0, 0}));
  if (ack && request[4] == 0x06 && request[5] == 0x07 && length == 0) {
    queueRxLocked(binaryFrame(0x06, 0x07, std::vector<uint8_t>(44, 0)));
  }
}

std::string modernFixStream(void) {
  const uint32_t total = (12 * 3600) + (35 * 60) + 19 + fixSecond;
  char stamp[16];
  std::snprintf(stamp, sizeof(stamp), "%02u%02u%02u.00", (total / 3600) % 24, (total / 60) % 60,
                total % 60);
  const std::string time(stamp);
  const std::string gga =
      nmea("GPGGA," + time + ",4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  // A null day is the RMC-pruned receiver: a ticking time and no date at all.
  if (fixDay == nullptr) {
    return gga;
  }
  // While the modelled receiver is still waking, its date reads a day behind.
  const std::string day = (fixColdBursts > 0) ? std::string("05") : std::string(fixDay);
  const std::string date = day + "0926";
  std::string rmc = nmea("GPRMC," + time + ",A,4807.038,N,01131.000,E,22.678,0.0," + date + ",,,A");
  if (fixRmcCorrupt) {
    // Break the checksum, not the shape. The token is still in the stream.
    const size_t star = rmc.rfind('*');
    if (star != std::string::npos) {
      rmc[star + 1] = (rmc[star + 1] == '0') ? '1' : '0';
    }
  }
  return rmc + gga;
}

/**
 * Answer a CFG-MSG poll at rate 0xFFFF with the message it asked for.
 *
 * The firmware polls MON-HW for the interference snapshot and MSG-GPSEPH,
 * MSG-GPSION and MSG-GPSUTC for the tier 2 ephemeris cache. Payload lengths are
 * multiples of four and the content is fixed, so a scenario can assert exact
 * decoded values.
 */
void queuePollResponseLocked(const uint8_t *request, uint16_t length) {
  if ((length != 4) || (request[4] != 0x06) || (request[5] != 0x01)) {
    return;
  }
  const uint8_t wantClass = request[6];
  const uint8_t wantId = request[7];
  const uint16_t rate = readU16(request + 8);
  if (rate != 0xFFFF) {
    return;
  }

  if ((wantClass == 0x0A) && (wantId == 0x09)) {
    monHwPolls++;
    // A receiver that answers MON-HW with a truncated payload. The decode has
    // to refuse it rather than read past the end of the frame.
    std::vector<uint8_t> payload(monHwShort ? 20 : 56, 0);
    payload[0] = 0x11;  // noise, little endian 0x00000311
    payload[1] = 0x03;
    payload[4] = 0x2a;  // agc 42
    payload[8] = 2;     // antenna ok
    payload[9] = 7;     // jamming indicator
    queueRxLocked(binaryFrame(wantClass, wantId, payload));
    return;
  }

  if (wantClass != 0x08) {
    return;
  }
  size_t payloadLength = 0;
  if (wantId == 0x07) {
    payloadLength = 72;
  } else if ((wantId == 0x06) || (wantId == 0x05)) {
    payloadLength = 40;
  } else {
    return;
  }
  std::vector<uint8_t> payload(payloadLength, 0);
  for (size_t i = 0; i < payload.size(); i++) {
    payload[i] = static_cast<uint8_t>(wantId + i);
  }
  queueRxLocked(binaryFrame(wantClass, wantId, payload));
}

void queueGpsEvent(QueueHandle_t queue) {
  {
    std::lock_guard<std::mutex> lock(gpsMutex);
    if (uartMode == "pause") {
      gpsQueue = queue;
      gpsOffset = gpsStream.size();
      rxBytes.clear();
      rxEventQueued = false;
      gpsEventQueued = false;
      gpsNextEventMillis = UINT32_MAX;
      return;
    }
    if (fixDateAdvances) {
      fixSecond++;
      if (fixColdBursts > 0) {
        fixColdBursts--;
      }
      gpsStream = modernFixStream();
    }
    if (!receiverAnswersLocked()) {
      gpsQueue = queue;
      gpsOffset = gpsStream.size();
      rxBytes.clear();
      rxEventQueued = false;
      gpsEventQueued = false;
      gpsNextEventMillis = Furble::Sim::clockMillis();
      return;
    }
    gpsQueue = queue;
    gpsOffset = 0;
    rxBytes.clear();
    rxEventQueued = false;
    gpsEventQueued = true;
    gpsNextEventMillis = Furble::Sim::clockMillis();
  }
  const uart_event_t event = {.type = UART_PATTERN_DET, .size = gpsStream.size()};
  xQueueSend(queue, &event, 0);
}

}  // namespace

esp_err_t uart_driver_install(uart_port_t, int, int, int, QueueHandle_t *uart_queue, int) {
  gpsQueue = xQueueCreate(32, sizeof(uart_event_t));
  if (uart_queue != nullptr) {
    *uart_queue = gpsQueue;
  }
  furble_sim_queue_set_reset_callback(gpsQueue, queueGpsEvent);
  queueGpsEvent(gpsQueue);
  return ESP_OK;
}

esp_err_t uart_param_config(uart_port_t, const uart_config_t *) {
  return ESP_OK;
}

esp_err_t uart_set_pin(uart_port_t, int, int, int, int) {
  return ESP_OK;
}

esp_err_t uart_enable_pattern_det_baud_intr(uart_port_t, char, int, int, int, int) {
  return ESP_OK;
}

esp_err_t uart_pattern_queue_reset(uart_port_t, int) {
  return ESP_OK;
}

esp_err_t uart_flush(uart_port_t) {
  return ESP_OK;
}

esp_err_t uart_set_baudrate(uart_port_t, uint32_t baud) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  uartBaud = baud;
  return ESP_OK;
}

int uart_read_bytes(uart_port_t, uint8_t *buffer, uint32_t length, TickType_t) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  if (gpsQueue == nullptr || buffer == nullptr || length == 0) {
    return 0;
  }
  if (!rxBytes.empty()) {
    const size_t count = uartMode == "partial" ? 1 : std::min<size_t>(length, rxBytes.size());
    for (size_t i = 0; i < count; i++) {
      buffer[i] = rxBytes.front();
      rxBytes.pop_front();
    }
    rxEventQueued = false;
    if (!rxBytes.empty()) {
      const uart_event_t event = {.type = UART_DATA, .size = rxBytes.size()};
      rxEventQueued = xQueueSend(gpsQueue, &event, 0) == pdTRUE;
    }
    return static_cast<int>(count);
  }
  if (gpsOffset >= gpsStream.size()) {
    return 0;
  }
  const size_t remaining = (gpsStream.size()) - gpsOffset;
  size_t count = std::min<size_t>(length, remaining);
  if (fixChunk != 0) {
    const uint32_t now = Furble::Sim::clockMillis();
    if (static_cast<int32_t>(now - fixChunkNext) < 0) {
      return 0;  // these bytes have not arrived off the wire yet
    }
    fixChunkNext = now + FIX_CHUNK_MS;
    if (count > fixChunk) {
      count = fixChunk;
    }
  }
  std::memcpy(buffer, gpsStream.data() + gpsOffset, count);
  gpsOffset += count;
  if (gpsOffset >= gpsStream.size()) {
    gpsEventQueued = false;
    gpsNextEventMillis = Furble::Sim::clockMillis() + gpsRatePeriodMillis();
  }
  return static_cast<int>(count);
}

void furble_sim_uart_update(void) {
  QueueHandle_t queue = nullptr;
  {
    std::lock_guard<std::mutex> lock(gpsMutex);
    // A chunked burst is delivered a chunk at a time as the bytes arrive, so
    // each one needs its own event. Without this the burst stalls half read.
    if ((fixChunk != 0) && (gpsQueue != nullptr) && (gpsOffset < gpsStream.size())
        && (static_cast<int32_t>(Furble::Sim::clockMillis() - fixChunkNext) >= 0)) {
      const uart_event_t event = {.type = UART_DATA, .size = gpsStream.size() - gpsOffset};
      xQueueSend(gpsQueue, &event, 0);
    }
    const uint32_t now = Furble::Sim::clockMillis();
    const bool due = (uartMode != "pause") && receiverAnswersLocked()
                     && (static_cast<int32_t>(now - gpsNextEventMillis) >= 0);
    if (gpsQueue != nullptr && !gpsEventQueued && rxBytes.empty() && gpsOffset >= gpsStream.size()
        && due) {
      queue = gpsQueue;
    }
  }
  if (queue != nullptr) {
    queueGpsEvent(queue);
  }
}

int uart_write_bytes(uart_port_t, const void *data, size_t length) {
  if (data == nullptr || length == 0) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(gpsMutex);
  const std::string command(static_cast<const char *>(data), length);
  uartWrites.push_back(command);
  if (uartMode == "write-error") {
    return -1;
  }
  const auto *bytes = static_cast<const uint8_t *>(data);
  if (!receiverAnswersLocked()) {
    // nothing on the port, or the driver is still probing another rate
    return static_cast<int>(length);
  }
  if (command.find("PCAS03,1,0,1,1,1,0,0,0") != std::string::npos) {
    queueRxLocked(satelliteBytes());
  }
  if (length >= 10 && bytes[0] == SYNC_0 && bytes[1] == SYNC_1) {
    // a replayed assistance frame is class 0x08 arriving from the host
    if (bytes[4] == 0x08) {
      ephReplayFrames++;
    }
    if (uartMode == "timeout") {
      return static_cast<int>(length);
    }
    if (uartMode == "malformed") {
      auto malformed = binaryFrame(0x05, 0x01, {bytes[4], bytes[5], 0, 0});
      malformed.back() ^= 0xff;
      queueRxLocked(malformed);
    } else {
      queueAckLocked(bytes, uartMode != "nack");
      queuePollResponseLocked(bytes, readU16(bytes + 2));
    }
  }
  const std::string prefix = "PCAS12,";
  const size_t offset = command.find(prefix);
  if (offset != std::string::npos) {
    const size_t start = offset + prefix.size();
    const size_t end = command.find_first_not_of("0123456789", start);
    const uint32_t seconds = static_cast<uint32_t>(
        std::strtoul(command.substr(start, end - start).c_str(), nullptr, 10));
    if (seconds > 0 && gpsQueue != nullptr && !gpsEventQueued && gpsOffset >= gpsStream.size()) {
      gpsNextEventMillis = Furble::Sim::clockMillis() + seconds * 1000;
    }
  }
  return static_cast<int>(length);
}

std::vector<std::string> furble_sim_uart_writes_snapshot(void) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  return uartWrites;
}

std::vector<std::string> furble_sim_uart_take_writes(void) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  std::vector<std::string> writes;
  writes.swap(uartWrites);
  return writes;
}

void furble_sim_uart_clear_writes(void) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  uartWrites.clear();
}

void furble_sim_uart_set_fix_chunk(size_t bytes) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  fixChunk = bytes;
}

uint32_t furble_sim_uart_fix_second(void) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  return fixSecond;
}

void furble_sim_uart_set_stationary(bool stationary) {
  const std::lock_guard<std::mutex> lock(gpsMutex);
  gpsStationary = stationary;
  // The fix burst is a single buffer now, so choosing the stationary track
  // swaps the buffer rather than switching a pointer at read time. Both canned
  // tracks are the same length, which the static_assert above holds.
  fixDateAdvances = false;
  gpsStream.assign(gpsSentences(), sizeof(gpsData) - 1);
  gpsOffset = gpsStream.size();
void furble_sim_uart_set_fix_date(const char *name) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  fixDateAdvances = false;
  fixDay = "06";
  fixColdBursts = 0;
  fixRmcCorrupt = false;
  if ((name != nullptr) && (std::string(name) == "badrmc")) {
    // Every RMC arrives with a broken checksum. The parser commits no date from
    // it, so the ephemeris arm must never commit either.
    fixRmcCorrupt = true;
    fixDateAdvances = true;
    gpsStream = modernFixStream();
  } else if ((name != nullptr) && (std::string(name) == "coldstart")) {
    // The modern burst, but the receiver spends its first few seconds reporting
    // a date a day behind, as a cold unit does before it decodes TOW.
    fixColdBursts = FIX_COLD_BURSTS;
    fixDateAdvances = true;
    gpsStream = modernFixStream();
  } else if ((name != nullptr) && (std::string(name) == "stale")) {
    fixDay = "13";
    fixDateAdvances = true;
    gpsStream = modernFixStream();
  } else if ((name != nullptr) && (std::string(name) == "nodate")) {
    // GGA only, with a ticking clock. It carries a position and a time but no
    // date, so the parser keeps whatever date the previous session left it
    // with. That is a receiver with RMC pruned, and it is the case where the
    // cache age can never be established. The clock has to tick: a frozen one
    // would let a rule that keys off the time pass for the wrong reason.
    fixDay = nullptr;
    fixDateAdvances = true;
    gpsStream = modernFixStream();
  } else if ((name != nullptr) && (std::string(name) == "modern")) {
    fixDateAdvances = true;
    gpsStream = modernFixStream();
  } else {
    gpsStream.assign(gpsData, sizeof(gpsData) - 1);
  }
  gpsOffset = gpsStream.size();
}

void furble_sim_uart_set_receiver(uint32_t baud, bool present) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  receiverBaud = baud;
  receiverPresent = present;
}

void furble_sim_uart_set_monhw_short(bool shortFrame) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  monHwShort = shortFrame;
}

void furble_sim_uart_set_satellite_fixture(const char *name) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  satelliteFixture = (name == nullptr) ? "default" : name;
}

uint32_t furble_sim_uart_baud(void) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  return uartBaud;
}

uint32_t furble_sim_uart_eph_replay_frames(void) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  return ephReplayFrames;
}

uint32_t furble_sim_uart_monhw_polls(void) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  return monHwPolls;
}

void furble_sim_uart_set_mode(const char *mode) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  uartMode = mode == nullptr ? "ack" : mode;
  if (uartMode == "pause") {
    gpsNextEventMillis = UINT32_MAX;
  } else if (gpsQueue != nullptr && rxBytes.empty() && gpsOffset >= gpsStream.size()) {
    gpsNextEventMillis = Furble::Sim::clockMillis();
  }
}

void furble_sim_uart_inject_event(const char *eventName) {
  std::lock_guard<std::mutex> lock(gpsMutex);
  if (gpsQueue == nullptr || eventName == nullptr) {
    return;
  }
  static const std::array<std::pair<const char *, uart_event_type_t>, 7> events = {
      {
       {"data", UART_DATA},
       {"fifo", UART_FIFO_OVF},
       {"buffer", UART_BUFFER_FULL},
       {"break", UART_BREAK},
       {"parity", UART_PARITY_ERR},
       {"frame", UART_FRAME_ERR},
       {"pattern", UART_PATTERN_DET},
       }
  };
  for (const auto &entry : events) {
    if (entry.first == std::string(eventName)) {
      const uart_event_t event = {.type = entry.second, .size = 0};
      xQueueSend(gpsQueue, &event, 0);
      return;
    }
  }
}

esp_err_t uart_wait_tx_done(uart_port_t, TickType_t) {
  return ESP_OK;
}
