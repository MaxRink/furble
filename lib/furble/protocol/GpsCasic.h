#ifndef FURBLE_GPS_CASIC_H
#define FURBLE_GPS_CASIC_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/*
 * Pure CASIC and NMEA helper logic for the AT6668 / CASIC GPS/BDS Unit v1.1.
 *
 * Everything here is free of ESP-IDF, NVS, the board support library and the
 * UI toolkit, so it links into the host test suite unchanged. The firmware GPS
 * driver calls into it. The data sources for every constant and layout below
 * are cited in plans/32-gps-advanced.md under PR32a, PR32b, PR32d and PR32e.
 */

namespace Furble {
namespace Casic {

/*
 * CASIC binary checksum.
 *
 * The primary CASIC specification section 2.2 prints the operands class-first,
 * which is wrong. The Quectel L76K specification prints them id-first, and that
 * is the form every recorded frame balances under. Verified against the L76K
 * worked examples and 33 live frames from the Espruino CASIC mirror. See
 * plans/32-gps-advanced.md PR32b. The next reader of the CASIC PDF will try to
 * reintroduce the class-first bug, so do not "fix" this.
 */
uint32_t checksum(uint8_t class_id, uint8_t message_id, uint16_t length, const uint8_t *payload);

/** Frame a payload as a full CASIC message: BA CE len class id payload cksum. */
std::vector<uint8_t> frame(uint8_t class_id,
                           uint8_t message_id,
                           const std::vector<uint8_t> &payload);

/*
 * Autobaud ladder.
 *
 * The M5Stack Unit GPS v1.1 ships at 115200 8N1, the CASIC and L76K factory
 * default is 9600, and 38400, 57600, 19200 and 4800 complete the $PCAS01 set.
 * These six are the entire ladder; there is no seventh rate to try. Detection
 * criterion is two NMEA sentences with a passing checksum in one step, which
 * guards against a garbled stream passing checksum by chance (1 in 256 per
 * sentence). See plans/32-gps-advanced.md PR32a.
 */
class Autobaud {
 public:
  static constexpr std::array<uint32_t, 6> LADDER = {115200, 9600, 38400, 57600, 19200, 4800};

  /** Milliseconds spent on each ladder step. A 1 Hz burst arrives once a second. */
  static constexpr uint32_t STEP_MS = 1200;

  /** Passing sentences needed in one step to declare a lock. */
  static constexpr uint32_t REQUIRED_SENTENCES = 2;

  enum class State : uint8_t {
    IDLE,
    PROBING,
    LOCKED,
    NO_RECEIVER,
  };

  /** A baud change the caller should apply to the UART, or none. */
  struct Action {
    bool change;
    uint32_t baud;
  };

  /** Start the ladder at the fastest rate. Records the sentence baseline. */
  Action begin(uint32_t now, uint32_t passed_checksum);

  /** Advance the ladder. Feed the live TinyGPSPlus passedChecksum() counter. */
  Action service(uint32_t now, uint32_t passed_checksum);

  State state(void) const { return m_State; }
  uint32_t baud(void) const { return LADDER[m_Step < LADDER.size() ? m_Step : 0]; }
  size_t step(void) const { return m_Step; }

 private:
  State m_State = State::IDLE;
  size_t m_Step = 0;
  uint32_t m_Base = 0;
  uint32_t m_Deadline = 0;
};

/*
 * Per satellite detail parsed directly from GSV and GSA.
 *
 * The pinned TinyGPS++ fork parses GGA and RMC only, so GSV and GSA are dropped
 * today. A direct 192 byte parser beats the 3.3 kB TinyGPSCustom approach and,
 * unlike TinyGPSCustom, can reassemble a multi-sentence GSV set. See
 * plans/32-gps-advanced.md PR32e.
 */
struct Satellite {
  uint16_t prn;          /**< satellite id as reported in the sentence */
  uint8_t constellation; /**< 1 GPS, 2 GLONASS, 4 BeiDou, 5 QZSS, 0 unknown */
  uint8_t elevation;     /**< degrees, 0 to 90 */
  uint16_t azimuth;      /**< degrees true, 0 to 359 */
  uint8_t snr;           /**< C/N0 in dB-Hz, 0 when not tracking */
  bool snr_valid;        /**< false when the C/N0 field was empty */
  bool used;             /**< listed in a GSA solution */
};

struct DopInfo {
  float pdop;
  float hdop;
  float vdop;
  uint8_t fix_type; /**< 1 no fix, 2 is 2D, 3 is 3D */
  bool valid;
};

/** GNSS constellation ids, matching the L76K numbering appendix. */
enum : uint8_t {
  CONSTELLATION_UNKNOWN = 0,
  CONSTELLATION_GPS = 1,
  CONSTELLATION_GLONASS = 2,
  CONSTELLATION_GALILEO = 3,
  CONSTELLATION_BEIDOU = 4,
  CONSTELLATION_QZSS = 5,
};

class NmeaSatellites {
 public:
  static constexpr size_t MAX_SATELLITES = 32;
  static constexpr size_t MAX_USED_SATELLITES = 64;

  /** Feed one whole NMEA sentence. Returns true if it was a GSV or GSA. */
  bool feed(const std::string &sentence);

  /** Snapshot the satellite table, highest C/N0 first. */
  std::vector<Satellite> satellites(void) const;

  DopInfo dop(void) const { return m_Dop; }
  size_t inView(void) const;
  size_t used(void) const;
  void clear(void);

 private:
  struct TalkerSet {
    uint8_t talker_constellation = CONSTELLATION_UNKNOWN;
    uint8_t total_sentences = 0;
    uint8_t seen_mask = 0;
    bool malformed = false;
    std::vector<Satellite> building;
    std::vector<Satellite> published;
  };

  bool feedGsv(uint8_t constellation, const std::vector<std::string> &fields);
  bool feedGsa(uint8_t constellation, const std::vector<std::string> &fields);
  TalkerSet &setFor(uint8_t constellation);

  std::vector<TalkerSet> m_Sets;
  struct UsedSatellite {
    uint8_t constellation;
    uint16_t prn;
  };

  std::vector<UsedSatellite> m_Used;
  DopInfo m_Dop = {0, 0, 0, 0, false};
};

/** Map an NMEA talker prefix such as "GP" to a constellation id. */
uint8_t constellationForTalker(char a, char b);

/*
 * Ephemeris cache framing for GPS_ASSIST tier 2.
 *
 * The receiver publishes its own ephemeris as periodic binary messages:
 * MSG-GPSEPH 0x08 0x07, MSG-GPSION 0x08 0x06 and MSG-GPSUTC 0x08 0x05. A
 * CFG-MSG poll at rate 0xFFFF asks for one copy. furble stores the frames
 * verbatim so replay is a straight byte write with no re-framing, then plays
 * them back on the next enable. GPS ephemeris is valid for roughly four hours.
 * See plans/32-gps-advanced.md PR32d.
 */
namespace Eph {
constexpr uint8_t CLASS = 0x08;
constexpr uint8_t EPH_ID = 0x07;
constexpr uint8_t ION_ID = 0x06;
constexpr uint8_t UTC_ID = 0x05;

/** True for the three periodic assistance messages furble caches. */
bool isEphemerisMessage(uint8_t class_id, uint8_t message_id);
}  // namespace Eph

class EphemerisCollector {
 public:
  /** 32 satellites at 72 payload bytes plus 10 framing is about 2.6 kB. */
  static constexpr size_t MAX_BYTES = 4096;

  /**
   * Store one already checksum-validated frame verbatim if it is an assistance
   * message and it fits. Returns true when the frame was stored.
   */
  bool feed(const uint8_t *frame, size_t length);

  const std::vector<uint8_t> &data(void) const { return m_Data; }
  size_t frameCount(void) const { return m_Count; }
  void clear(void);

 private:
  std::vector<uint8_t> m_Data;
  size_t m_Count = 0;
};

/**
 * Split a verbatim frame stream into [offset, length] spans for paced replay.
 *
 * Walks 0xBA 0xCE frames, validating the length and checksum of each. Stops at
 * the first malformed frame so a corrupt cache cannot desynchronise the send.
 */
std::vector<std::pair<size_t, size_t>> splitFrames(const uint8_t *data, size_t length);

/*
 * MON-HW interference snapshot.
 *
 * MON-HW is class 0x0A id 0x09, 56 bytes, documented in CASIC section 2.14.2:
 * noise power, AGC counts, antenna status and jamming indicator. The exact
 * field offsets could not be verified against a live unit, so this decode is
 * best effort and the console marks it hardware-tuning-pending. See
 * plans/32-gps-advanced.md inventory row MON-HW.
 */
struct MonHw {
  uint32_t noise;         /**< noise level, receiver units */
  uint16_t agc;           /**< AGC count */
  uint8_t antenna_status; /**< 0 init, 1 unknown, 2 ok, 3 short, 4 open, per CASIC */
  uint8_t jam_indicator;  /**< 0 to 255 interference indicator */
  bool valid;
};

constexpr uint8_t MON_CLASS = 0x0A;
constexpr uint8_t MON_HW_ID = 0x09;

/** Best-effort decode of a MON-HW payload. valid is false when too short. */
MonHw parseMonHw(const uint8_t *payload, size_t length);

}  // namespace Casic
}  // namespace Furble

#endif
