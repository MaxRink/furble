#include "GpsText.h"

#include <cstdio>
#include <cstring>

namespace Furble {
namespace GpsText {

namespace {

/** Names that identify a vendor text message, longest first is not needed. */
struct Name {
  const char *text;
  Dialect dialect;
};

/*
 * The receive direction only. Every Unicore command name starts with CFG (UC
 * 3.3, pages 31 to 68) and the two general responses are OK and FAIL (UC 3.2,
 * pages 30 and 31). Quectel uses PAIR and PQTM (Q76 2.3 and 2.4).
 */
constexpr Name NAMES[] = {
    {"PAIR",    Dialect::QUECTEL},
    {"PQTM",    Dialect::QUECTEL},
    {"CFG",     Dialect::UNICORE},
    {"PDTINFO", Dialect::UNICORE},
    {"OK",      Dialect::UNICORE},
    {"FAIL",    Dialect::UNICORE},
};

bool isHex(char c) {
  return ((c >= '0') && (c <= '9')) || ((c >= 'A') && (c <= 'F')) || ((c >= 'a') && (c <= 'f'));
}

uint8_t hexValue(char c) {
  if ((c >= '0') && (c <= '9')) {
    return static_cast<uint8_t>(c - '0');
  }
  if ((c >= 'A') && (c <= 'F')) {
    return static_cast<uint8_t>((c - 'A') + 10);
  }
  return static_cast<uint8_t>((c - 'a') + 10);
}

/** Decimal or empty: the reply fields furble compares are all integers. */
bool parseUint(const std::string &text, uint32_t &out) {
  if (text.empty() || (text.size() > 10)) {
    return false;
  }
  uint32_t value = 0;
  for (const char c : text) {
    if ((c < '0') || (c > '9')) {
      return false;
    }
    value = (value * 10) + static_cast<uint32_t>(c - '0');
  }
  out = value;
  return true;
}

}  // namespace

const char *opName(Op op) {
  switch (op) {
    case Op::VERSION:
      return "version";
    case Op::FIX_RATE:
      return "fix_rate";
    case Op::SENTENCES:
      return "sentences";
    case Op::GNSS:
      return "gnss";
    case Op::SBAS:
      return "sbas";
    case Op::NAV_MODE:
      return "nav_mode";
    case Op::BAUD:
      return "baud";
    case Op::SAVE:
      return "save";
    case Op::STANDBY:
      return "standby";
    case Op::POWER:
      return "power";
    case Op::RESET:
      return "reset";
    case Op::MSG_RATE:
      return "msg_rate";
    case Op::PRODUCT_INFO:
      return "product_info";
    case Op::UNKNOWN:
      break;
  }
  return "unknown";
}

const char *verdictName(Verdict verdict) {
  switch (verdict) {
    case Verdict::ACKED:
      return "acked";
    case Verdict::PENDING:
      return "pending";
    case Verdict::NACKED:
      return "nacked";
    case Verdict::UNSUPPORTED:
      return "unsupported";
    case Verdict::PARAM:
      return "param";
    case Verdict::BUSY:
      return "busy";
    case Verdict::BAD_REPLY:
      return "bad_reply";
    case Verdict::TIMEOUT:
      return "timeout";
    case Verdict::STALLED:
      return "stalled";
    case Verdict::NONE:
      break;
  }
  return "none";
}

bool refused(Verdict verdict) {
  return (verdict == Verdict::NACKED) || (verdict == Verdict::UNSUPPORTED)
         || (verdict == Verdict::PARAM) || (verdict == Verdict::BUSY)
         || (verdict == Verdict::BAD_REPLY);
}

uint8_t checksum(const char *body, size_t length) {
  uint8_t sum = 0;
  for (size_t i = 0; i < length; i++) {
    sum ^= static_cast<uint8_t>(body[i]);
  }
  return sum;
}

std::string frame(const std::string &body) {
  if (body.empty() || (body.size() > MAX_BODY)) {
    return {};
  }
  char tail[8];
  std::snprintf(tail, sizeof(tail), "*%02X\r\n", checksum(body.data(), body.size()));
  std::string out;
  out.reserve(body.size() + 7);
  out.push_back('$');
  out.append(body);
  out.append(tail);
  return out;
}

Prefix prefix(const uint8_t *data, size_t length) {
  if ((length == 0) || (data[0] != '$')) {
    return Prefix::NO;
  }
  const char *name = reinterpret_cast<const char *>(data + 1);
  const size_t available = length - 1;
  bool maybe = false;
  for (const auto &candidate : NAMES) {
    const size_t size = std::strlen(candidate.text);
    if (available >= size) {
      if (std::memcmp(name, candidate.text, size) == 0) {
        return Prefix::YES;
      }
      continue;
    }
    if (std::memcmp(name, candidate.text, available) == 0) {
      maybe = true;
    }
  }
  return maybe ? Prefix::MAYBE : Prefix::NO;
}

Dialect classify(const char *name, size_t length) {
  for (const auto &candidate : NAMES) {
    const size_t size = std::strlen(candidate.text);
    if ((length >= size) && (std::memcmp(name, candidate.text, size) == 0)) {
      return candidate.dialect;
    }
  }
  return Dialect::NONE;
}

bool parse(const char *data, size_t length, Line &out) {
  out = Line();
  if ((length < 6) || (data[0] != '$')) {
    return false;
  }

  size_t star = length;
  for (size_t i = 1; i < length; i++) {
    const char c = data[i];
    if ((c == '\r') || (c == '\n')) {
      // a terminator before the checksum is a broken line, not a reply
      return false;
    }
    if (c == '*') {
      star = i;
      break;
    }
  }
  if ((star == length) || ((star + 2) >= length) || !isHex(data[star + 1])
      || !isHex(data[star + 2])) {
    return false;
  }

  const uint8_t expected =
      static_cast<uint8_t>((hexValue(data[star + 1]) << 4) | hexValue(data[star + 2]));
  out.checksum_ok = (checksum(data + 1, star - 1) == expected);
  out.terminated = (star + 3 < length) && ((data[star + 3] == '\r') || (data[star + 3] == '\n'));

  const size_t body = star - 1;
  size_t split = body;
  for (size_t i = 0; i < body; i++) {
    if (data[1 + i] == ',') {
      split = i;
      break;
    }
  }
  out.name.assign(data + 1, split);
  out.dialect = classify(out.name.data(), out.name.size());
  if (out.name.empty()) {
    return false;
  }

  if (split == body) {
    // a name with no data fields, for example $OK or $PQTMSAVEPAR
    return true;
  }

  size_t start = split + 1;
  while (true) {
    size_t end = body;
    for (size_t i = start; i < body; i++) {
      if (data[1 + i] == ',') {
        end = i;
        break;
      }
    }
    out.fields.emplace_back(data + 1 + start, end - start);
    if (end == body) {
      break;
    }
    start = end + 1;
  }
  return true;
}

Verdict match(const Line &line, const Command &command) {
  if ((command.dialect == Dialect::NONE) || (line.dialect != command.dialect)
      || command.ack.empty()) {
    return Verdict::NONE;
  }
  /*
   * $OK and $FAIL carry no command name, so they answer whichever command is in
   * flight (UC 3.2, pages 30 and 31). Every other reply has to name the command
   * it answers.
   */
  const bool bare =
      (command.dialect == Dialect::UNICORE) && ((line.name == "OK") || (line.name == "FAIL"));
  if (!bare && (line.name != command.ack)) {
    return Verdict::NONE;
  }

  /*
   * Compare the acknowledged packet id before the checksum. A PAIR001 for
   * another command says nothing about this one even when the line is corrupt,
   * and one flipped bit on the UART must not turn into "the receiver refused
   * your setting" for a command it never answered.
   */
  uint32_t id = 0;
  uint32_t result = 0;
  const bool pairAck = (line.name == "PAIR001");
  if (command.dialect == Dialect::QUECTEL) {
    if (pairAck) {
      if (line.fields.size() < 2) {
        return Verdict::BAD_REPLY;
      }
      if (!parseUint(line.fields[0], id) || !parseUint(line.fields[1], result)) {
        return Verdict::BAD_REPLY;
      }
      if ((command.ack_id != 0) && (id != command.ack_id)) {
        return Verdict::NONE;
      }
    }
  }

  if (!line.checksum_ok || !line.terminated) {
    return Verdict::BAD_REPLY;
  }

  if (command.dialect == Dialect::QUECTEL) {
    /*
     * $PAIR001,<CommandID>,<Result> (Q76 2.3.1 pages 32 and 33, Q29 2.4.1
     * page 51).
     */
    if (pairAck) {
      switch (result) {
        case 0:
          return Verdict::ACKED;
        case 1:
          return Verdict::PENDING;
        case 2:
          return Verdict::NACKED;
        case 3:
          return Verdict::UNSUPPORTED;
        case 4:
          return Verdict::PARAM;
        case 5:
          return Verdict::BUSY;
        default:
          return Verdict::BAD_REPLY;
      }
    }
  }

  if (command.dialect == Dialect::UNICORE) {
    // $OK (UC 3.2 page 30) and $FAIL,<code> (UC 3.2 page 31). Neither carries
    // the command it answers, so the session allows only one in flight.
    if (line.name == "OK") {
      return Verdict::ACKED;
    }
    if (line.name == "FAIL") {
      uint32_t code = 0;
      if (line.fields.empty() || !parseUint(line.fields[0], code)) {
        return Verdict::BAD_REPLY;
      }
      // 0 is a rejected parameter, 1 is a checksum the receiver refused
      return (code == 1) ? Verdict::BAD_REPLY : Verdict::PARAM;
    }
    return Verdict::ACKED;
  }

  /*
   * A PQTM reply repeats the command name (Q76 2.4, pages 95 to 100). A get
   * carries its data, a set carries OK, and a failure carries ERROR and a code
   * from Table 5 of Q29 page 33: 1 invalid parameters, 2 execution failed.
   */
  if (line.fields.empty()) {
    return Verdict::BAD_REPLY;
  }
  if (line.fields[0] == "ERROR") {
    return Verdict::PARAM;
  }
  return Verdict::ACKED;
}

void Session::begin(const Command &command, uint32_t now) {
  m_Command = command;
  m_Active = true;
  m_Attempts = 1;
  m_Pending = 0;
  m_Busy = 0;
  m_Deadline = now + ACK_TIMEOUT_MS;
  m_Verdict = Verdict::NONE;
}

void Session::clear(void) {
  m_Command = Command();
  m_Active = false;
  m_Attempts = 0;
  m_Pending = 0;
  m_Busy = 0;
  m_Deadline = 0;
  m_Verdict = Verdict::NONE;
}

Session::Step Session::service(uint32_t now) {
  if (!m_Active) {
    return Step::IDLE;
  }
  if (m_Verdict == Verdict::ACKED) {
    m_Active = false;
    return Step::ACKED;
  }
  if (m_Verdict == Verdict::PENDING) {
    // the receiver accepted the packet and is still working on it (Q76 2.3.1
    // result 1), so this is not a lost line and does not consume an attempt. It
    // is bounded all the same: a receiver that never finishes must not hold the
    // queue forever.
    m_Verdict = Verdict::NONE;
    if (m_Pending >= PENDING_MAX) {
      m_Active = false;
      m_Verdict = Verdict::STALLED;
      return Step::TIMEOUT;
    }
    m_Pending++;
    m_Deadline = now + ACK_TIMEOUT_MS;
    return Step::WAIT;
  }
  if (m_Verdict == Verdict::BUSY) {
    // "MNL service is busy, you can try again soon" (Q76 2.3.1 result 5). That
    // is a retry, not a refusal, so the deadline is re-armed without spending
    // an attempt until the cap is reached.
    m_Verdict = Verdict::NONE;
    if (m_Busy >= BUSY_MAX) {
      m_Active = false;
      m_Verdict = Verdict::BUSY;
      return Step::FAILED;
    }
    m_Busy++;
    m_Deadline = now + ACK_TIMEOUT_MS;
    return Step::WAIT;
  }
  if (refused(m_Verdict)) {
    m_Active = false;
    return Step::FAILED;
  }
  if (static_cast<int32_t>(now - m_Deadline) < 0) {
    return Step::WAIT;
  }
  if (m_Attempts < ATTEMPTS) {
    m_Attempts++;
    m_Deadline = now + ACK_TIMEOUT_MS;
    return Step::SEND;
  }
  m_Active = false;
  m_Verdict = Verdict::TIMEOUT;
  return Step::TIMEOUT;
}

Verdict Session::accept(const char *data, size_t length) {
  Line line;
  if (!parse(data, length, line)) {
    return Verdict::NONE;
  }
  return accept(line);
}

Verdict Session::accept(const Line &line) {
  if (!m_Active) {
    return Verdict::NONE;
  }
  const Verdict verdict = match(line, m_Command);
  if (verdict == Verdict::NONE) {
    return Verdict::NONE;
  }
  m_Verdict = verdict;
  return verdict;
}

bool windowed(const Line &line) {
  if (line.dialect == Dialect::QUECTEL) {
    // a PAIR001 names its packet id, so it cannot answer the wrong command
    return false;
  }
  if (line.dialect != Dialect::UNICORE) {
    return false;
  }
  if (line.name == "PDTINFO") {
    // identification is not an acknowledgement, drop it and a late answer can
    // never name the receiver
    return false;
  }
  return true;
}

}  // namespace GpsText
}  // namespace Furble
