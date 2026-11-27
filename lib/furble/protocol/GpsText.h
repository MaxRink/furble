#ifndef FURBLE_GPS_TEXT_H
#define FURBLE_GPS_TEXT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/*
 * Vendor text command protocol for GNSS receivers that configure themselves with
 * NMEA-shaped ASCII sentences: Quectel $PAIR and $PQTM, Unicore $CFG.
 *
 * One seam owns the parts every such receiver shares: the '$' body '*' checksum
 * CR LF frame, the XOR checksum, splitting a reply into a name and its fields,
 * deciding whether a reply answers the command in flight, and the timeout and
 * retry policy. The per-vendor half is the name set and the acknowledgement
 * shape, which is why the seam carries a Dialect rather than an interface: a
 * virtual driver object would cost flash the 4 MB Core debug image does not
 * have, and the two dialects differ in a table, not in behaviour.
 *
 * Sources, cited at every constant:
 *
 * Q76  Quectel "LC26G (AB)&LC76G&LC86G Series GNSS Protocol Specification",
 *      V1.1, 2022-12-20, status Released.
 * Q29  Quectel "LC29H Series&LC79H (AL) GNSS Protocol Specification", V1.2,
 *      2023-06-20, status Released.
 * UC   Unicore "UFirebird II Series Protocol Specification", UC-04-FB2 EN R1.5,
 *      November 2025, which lists UC6580, UM670A, UM680A and UM681A as
 *      applicable (UC 3.2, page 26).
 *
 * The full command tables and the document identities are in
 * plans/367-gnss-vendor-text-drivers.md and docs/protocols/gnss-quectel-pair.md
 * and docs/protocols/gnss-unicore-cfg.md.
 */

namespace Furble {
namespace GpsText {

/**
 * Longest vendor line furble will buffer before it gives up and resynchronises.
 *
 * Unicore caps every message at 128 bytes (UC 3.1, page 24). Quectel states no
 * cap, and its longest sentence furble sends is a $PQTMCFGMSGRATE set at about
 * 40 bytes, so the Unicore bound covers both with room for the reply.
 */
constexpr size_t MAX_LINE = 128;

/** Longest body frame() accepts. The cap keeps a runaway string off the UART. */
constexpr size_t MAX_BODY = 100;

/** Which vendor dialect a line belongs to. */
enum class Dialect : uint8_t {
  NONE,    /**< not a vendor text message, or not identified */
  QUECTEL, /**< $PAIR and $PQTM */
  UNICORE, /**< $CFG, $OK, $FAIL and $PDTINFO */
};

/**
 * Constellation mask bits, the stored GPS_GNSS wire values.
 *
 * These are the same numbers as Ubx::GNSS_MASK_*, because they are one setting.
 * The gps-text host test pins both, so a change in either fails here.
 */
constexpr uint8_t GNSS_GPS = 0x01;
constexpr uint8_t GNSS_BDS = 0x02;
constexpr uint8_t GNSS_GLO = 0x04;
constexpr uint8_t GNSS_GAL = 0x08;
constexpr uint8_t GNSS_QZSS = 0x10;
constexpr uint8_t GNSS_SBAS = 0x20;
constexpr uint8_t GNSS_ALL = 0x3f;

/** Power save setting values, as stored in GPS_PSM. */
constexpr uint8_t PSM_OFF = 0;
constexpr uint8_t PSM_CYCLIC = 1;
constexpr uint8_t PSM_ON_OFF = 2;

/** NMEA output set the GPS_NMEA setting asks for, mirroring Casic::SentenceSet. */
enum class SentenceSet : uint8_t {
  PRUNE,     /**< RMC and GGA only */
  SATELLITE, /**< RMC, GGA, GSA and GSV while the satellite page is open */
  STANDARD,  /**< the six sentences both Quectel documents ship by default */
};

/**
 * The operation a queued command performs.
 *
 * The value is what the configuration status list records in its message id
 * column, so the existing UI line and the console keep working for a text
 * backend without a new status record layout.
 */
enum class Op : uint8_t {
  UNKNOWN = 0,
  VERSION = 1,       /**< identification */
  FIX_RATE = 2,      /**< position fix interval */
  SENTENCES = 3,     /**< NMEA sentence enable or rate */
  GNSS = 4,          /**< constellation enable */
  SBAS = 5,          /**< SBAS enable */
  NAV_MODE = 6,      /**< dynamic platform model */
  BAUD = 7,          /**< UART baud rate */
  SAVE = 8,          /**< save configuration to flash */
  STANDBY = 9,       /**< timed standby or backup mode */
  POWER = 10,        /**< GNSS subsystem power on or off */
  RESET = 11,        /**< receiver restart */
  MSG_RATE = 12,     /**< proprietary message rate */
  PRODUCT_INFO = 13, /**< Unicore product information */
};

/** Printable name of an operation, for the console. Never null. */
const char *opName(Op op);

/** What the GPS settings ask a receiver for, in furble's setting terms. */
struct Request {
  uint16_t rate_ms = 0; /**< 0 leaves the receiver fix rate alone */
  uint8_t gnss = 0;     /**< GNSS_* bits */
  uint8_t gnss_set = 0; /**< which GNSS_* bits gnss decides; 0 leaves all alone */
  uint8_t psm = 0;      /**< PSM_OFF, PSM_CYCLIC or PSM_ON_OFF */
  uint8_t duty_s = 0;   /**< standby interval in seconds, 0 when there is none */
  SentenceSet sentences = SentenceSet::STANDARD;
  bool sentences_set = false; /**< false leaves the receiver's sentence set alone */
  uint8_t platform = 0;       /**< GPS_PLATFORM 1 to 4, 0 leaves it alone */
  uint32_t baud = 0;          /**< 0 leaves the port rate alone */
};

/** XOR of every character between the '$' and the '*'. */
uint8_t checksum(const char *body, size_t length);

/** Build the wire form: '$' body '*' XX CR LF. Empty when the body is too long. */
std::string frame(const std::string &body);

/**
 * How far a line that starts with '$' matches a vendor message name.
 *
 * Runs on the bytes after the '$'. The demux uses it to tell a vendor line from
 * standard NMEA before the line has fully arrived, so MAYBE means "wait for
 * more bytes" and never "drop".
 */
enum class Prefix : uint8_t {
  NO,    /**< cannot become a vendor message name */
  MAYBE, /**< still a proper prefix of one */
  YES,   /**< matched one */
};

/** Classify the name of a line. data points at the '$'. */
Prefix prefix(const uint8_t *data, size_t length);

/** The dialect a message name belongs to, or NONE. */
Dialect classify(const char *name, size_t length);

/** One parsed reply line. */
struct Line {
  Dialect dialect = Dialect::NONE;
  std::string name;                /**< "PAIR001", "PQTMVERNO", "OK", "CFGSYS" */
  std::vector<std::string> fields; /**< data fields after the name, empty kept */
  bool checksum_ok = false;
  bool terminated = false; /**< the line ended with CR or LF */
};

/**
 * Parse one complete line, '$' through its terminator.
 *
 * Returns false when the line has no '$', no '*', or a malformed checksum
 * field. A wrong checksum value is not a parse failure: it is reported in
 * checksum_ok so a corrupted reply is a malformed reply, not silence.
 */
bool parse(const char *data, size_t length, Line &out);

/** What a reply means for the command in flight. */
enum class Verdict : uint8_t {
  NONE,        /**< not a reply to this command */
  ACKED,       /**< the receiver accepted it */
  PENDING,     /**< the receiver says it is still processing (PAIR result 1) */
  NACKED,      /**< the receiver refused it (PAIR result 2, or no reason given) */
  UNSUPPORTED, /**< PAIR result 3: this receiver does not have the command */
  PARAM,       /**< PAIR result 4 or FAIL 0: the parameters were rejected */
  BUSY,        /**< PAIR result 5: the receiver asked for a retry */
  BAD_REPLY,   /**< answered this command, but malformed or a bad checksum */
  TIMEOUT,     /**< no reply after every attempt */
};

/** Short token for a verdict, for the console and the logs. Never null. */
const char *verdictName(Verdict verdict);

/** True when the verdict is a refusal the receiver stated. */
bool refused(Verdict verdict);

/** One queued command. */
struct Command {
  Dialect dialect = Dialect::NONE;
  Op op = Op::UNKNOWN;
  /** Body without '$' or checksum, for example "PAIR050,1000". */
  std::string body;
  /** Name of the sentence that answers it: "PAIR001", "OK", "PQTMVERNO". */
  std::string ack;
  /**
   * Quectel packet id the acknowledgement names, or 0 when the reply carries
   * the command name itself. PAIR acks repeat the acknowledged id (Q76 2.3.1
   * page 32), which is what makes them safe to match; a Unicore $OK does not
   * (UC 3.2 page 30), so the session relies on one command in flight.
   */
  uint16_t ack_id = 0;
  /** Pause the receiver is owed after the acknowledgement, in ms. */
  uint16_t settle_ms = 0;
};

/**
 * Decide whether a reply answers a command.
 *
 * A PAIR001 ack carries the acknowledged packet id and a result code (Q76
 * 2.3.1 page 32, Q29 2.4.1 page 51). A PQTM reply repeats the command name and
 * starts its fields with OK or ERROR (Q76 2.4 page 95 onward). Unicore answers
 * with a bare $OK or $FAIL,<code> and repeats the command name only when the
 * answer carries data (UC 3.2 pages 30 and 31).
 */
Verdict match(const Line &line, const Command &command);

/**
 * The timeout and retry state machine for one command in flight.
 *
 * The driver holds one of these and drives it from its existing configuration
 * pass, so a text command gets the same one-in-flight discipline as a CASIC or
 * UBX one. The policy lives here rather than in the driver so the host test can
 * pin it without a UART.
 */
class Session {
 public:
  /**
   * Time to wait for a reply. Unicore states no bound. Quectel answers a PAIR
   * command as soon as the MNL accepts it and returns 1 while it is still
   * processing (Q76 2.3.1 page 32), and the retry exists for a lost line, so
   * one second matches the UBX acknowledgement bound the driver already uses.
   */
  static constexpr uint32_t ACK_TIMEOUT_MS = 1000;

  /** Send attempts, the first included. Same count as the CASIC path. */
  static constexpr uint8_t ATTEMPTS = 3;

  enum class Step : uint8_t {
    IDLE,    /**< nothing in flight */
    WAIT,    /**< a send is outstanding */
    SEND,    /**< write the frame again */
    ACKED,   /**< accepted */
    FAILED,  /**< refused, with the reason in verdict() */
    TIMEOUT, /**< every attempt went unanswered */
  };

  /** Start a command. The caller sends the first frame. */
  void begin(const Command &command, uint32_t now);
  /** Drop the command without a result. */
  void clear(void);

  bool active(void) const { return m_Active; }
  /** Advance the deadline. Call once per configuration pass. */
  Step service(uint32_t now);
  /** Offer one parsed line. A non-NONE result ends the exchange. */
  Verdict accept(const char *data, size_t length);
  /** Offer an already parsed line. */
  Verdict accept(const Line &line);

  const Command &command(void) const { return m_Command; }
  uint8_t attempts(void) const { return m_Attempts; }
  uint32_t deadline(void) const { return m_Deadline; }
  Verdict verdict(void) const { return m_Verdict; }

 private:
  Command m_Command;
  bool m_Active = false;
  uint8_t m_Attempts = 0;
  uint32_t m_Deadline = 0;
  Verdict m_Verdict = Verdict::NONE;
};

}  // namespace GpsText
}  // namespace Furble

#endif
