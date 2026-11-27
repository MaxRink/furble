// Host test for the simulator's virtual vendor receivers, driven end to end
// through the production seam: the commands come from Quectel::plan and
// Unicore::plan, the frames from GpsText::frame, and the verdicts from
// GpsText::Session, so a model that stops answering like the real part fails
// here rather than only in a scenario.
//
// Coverage:
//   a. Quectel   identification, a good command, a nack, an unsupported id,
//                a bad checksum, a timeout, standby and the save path
//   b. Unicore   identification, a good command, a FAIL, a bad checksum, a
//                command the receiver ignores, restart and the save path
//
// The datasheet citations name the document, section and page of every
// protocol fact the assertions read.

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "GpsQuectel.h"
#include "GpsText.h"
#include "GpsUnicore.h"
#include "vendor_gnss.h"

using namespace Furble;

namespace {

int g_Failures = 0;
int g_Checks = 0;

bool check(bool condition, const char *message) {
  g_Checks++;
  if (!condition) {
    std::cerr << "  FAIL: " << message << '\n';
    g_Failures++;
  }
  return condition;
}

/**
 * One command in flight against a virtual receiver.
 *
 * Writes the frame, feeds it to the model, offers every reply line to the
 * session, and advances a virtual clock until the session decides. No real
 * time, no UART, so the result is deterministic.
 */
template <typename Receiver>
struct link_t {
  explicit link_t(Receiver &target) : receiver(target) {}

  Receiver &receiver;
  GpsText::Session session;
  std::vector<std::string> writes;
  uint32_t now = 1000;

  /** Offer a block of reply bytes to the session, one line at a time. */
  GpsText::Verdict feed(const std::vector<uint8_t> &reply) {
    GpsText::Verdict verdict = GpsText::Verdict::NONE;
    std::string line;
    for (const uint8_t byte : reply) {
      const char c = static_cast<char>(byte);
      line.push_back(c);
      if ((c == '\n') || (c == '\r')) {
        const GpsText::Verdict result = session.accept(line.data(), line.size());
        if (result != GpsText::Verdict::NONE) {
          verdict = result;
        }
        line.clear();
      }
    }
    return verdict;
  }

  /** Corrupt the checksum of every frame this link sends, to model a bad line. */
  bool corruptFrame = false;

  std::string frameOf(const GpsText::Command &command) const {
    std::string frame = GpsText::frame(command.body);
    if (corruptFrame && !frame.empty()) {
      // flip the low nibble of the checksum's second digit
      char &digit = frame[frame.size() - 3];
      digit = (digit == '0') ? '1' : '0';
    }
    return frame;
  }

  GpsText::Verdict run(const GpsText::Command &command, uint32_t budget_ms = 6000) {
    const std::string frame = frameOf(command);
    const uint32_t start = now;
    session.begin(command, now);
    writes.push_back(frame);
    GpsText::Verdict verdict =
        feed(receiver.receive(reinterpret_cast<const uint8_t *>(frame.data()), frame.size(), now));
    if (verdict != GpsText::Verdict::NONE) {
      session.service(now);
      return verdict;
    }
    while ((now - start) < budget_ms) {
      now += 250;
      switch (session.service(now)) {
        case GpsText::Session::Step::SEND:
        {
          writes.push_back(frame);
          const GpsText::Verdict again = feed(
              receiver.receive(reinterpret_cast<const uint8_t *>(frame.data()), frame.size(), now));
          if (again != GpsText::Verdict::NONE) {
            return again;
          }
          break;
        }
        case GpsText::Session::Step::ACKED:
          return GpsText::Verdict::ACKED;
        case GpsText::Session::Step::FAILED:
          return session.verdict();
        case GpsText::Session::Step::TIMEOUT:
          return GpsText::Verdict::TIMEOUT;
        case GpsText::Session::Step::IDLE:
        case GpsText::Session::Step::WAIT:
          break;
      }
    }
    return session.verdict();
  }
};

// a. Quectel.
bool testQuectel() {
  std::cout << "test: the virtual Quectel receiver answers like the documents\n";
  const int before = g_Failures;

  Sim::VirtualQuectel receiver;
  Sim::VirtualQuectel::Options options;
  options.version = "LC29HAANR01A04S";
  receiver.reset(options, 0);
  link_t<Sim::VirtualQuectel> link {receiver};

  // identification, Q76 2.4.5 pages 99 to 100
  const GpsText::Verdict version = link.run(Quectel::verNo());
  check(version == GpsText::Verdict::ACKED, "the receiver identifies with PQTMVERNO");
  check(receiver.commands == 1, "one command reached the receiver");

  // a good command
  check(link.run(Quectel::fixRate(200)) == GpsText::Verdict::ACKED, "PAIR050 is acknowledged");
  check(receiver.settings().rate_ms == 200, "the fix rate landed in RAM");
  check(receiver.acks == 1, "PAIR050 is the only PAIR ack so far: PQTMVERNO answers by name");

  // a nacked command: this variant does not implement PAIR050
  Sim::VirtualQuectel slow;
  Sim::VirtualQuectel::Options noRate;
  noRate.noFixRate = true;
  slow.reset(noRate, 0);
  link_t<Sim::VirtualQuectel> slowLink {slow};
  check(slowLink.run(Quectel::fixRate(200)) == GpsText::Verdict::UNSUPPORTED,
        "an LC76G (PA) refuses PAIR050 with result 3");
  check(slow.settings().rate_ms == 1000, "a refused rate leaves the receiver at 1 Hz");

  // a search mode refusal: LC29H (BA) and friends answer result 3
  Sim::VirtualQuectel single;
  Sim::VirtualQuectel::Options noSearch;
  noSearch.noSearchMode = true;
  single.reset(noSearch, 0);
  link_t<Sim::VirtualQuectel> singleLink {single};
  const GpsText::Command search = Quectel::searchMode(GpsText::GNSS_GPS);
  check(singleLink.run(search) == GpsText::Verdict::UNSUPPORTED,
        "a variant without PAIR066 answers result 3");

  // an id this part does not have at all
  GpsText::Command unknown;
  unknown.dialect = GpsText::Dialect::QUECTEL;
  unknown.op = GpsText::Op::UNKNOWN;
  unknown.body = "PAIR999";
  unknown.ack = "PAIR001";
  unknown.ack_id = 999;
  check(link.run(unknown) == GpsText::Verdict::UNSUPPORTED,
        "an unknown packet id is refused with result 3");

  // a malformed frame: the receiver refuses a bad checksum
  link.corruptFrame = true;
  check(link.run(Quectel::fixRate(500)) == GpsText::Verdict::PARAM,
        "a corrupt PAIR frame is a parameter error (Q76 2.3.1 page 32)");
  link.corruptFrame = false;
  check(receiver.badChecksum == 1, "the receiver saw the checksum error");
  check(receiver.settings().rate_ms == 200, "a refused frame changes nothing");

  // a timeout: a Quectel receiver has no $PDTINFO, so the line is ignored
  GpsText::Command foreign;
  foreign.dialect = GpsText::Dialect::QUECTEL;
  foreign.body = "PDTINFO";
  foreign.ack = "PDTINFO";
  link.session.clear();
  const size_t writesBefore = link.writes.size();
  check(link.run(foreign) == GpsText::Verdict::TIMEOUT,
        "a command the receiver ignores ends in a timeout");
  check(link.session.attempts() == GpsText::Session::ATTEMPTS, "the timeout spent every attempt");
  check((link.writes.size() - writesBefore) == GpsText::Session::ATTEMPTS, "one write per attempt");

  // standby: PAIR650, and the receiver stops producing epochs
  GpsText::Command standby;
  check(Quectel::standby(10, standby), "10 s is a documented backup interval");
  check(link.run(standby) == GpsText::Verdict::ACKED, "PAIR650 is acknowledged");
  check(receiver.backups == 1, "the backup was entered once");
  check(receiver.standbyS == 10, "the interval reached the receiver");
  check(receiver.sleeping(link.now), "the receiver is in backup mode");
  check(!receiver.outputting(link.now), "a receiver in backup mode produces no epochs");
  receiver.tick(link.now + 11000);
  check(!receiver.sleeping(link.now + 11000), "the backup timer wakes the receiver");

  // and the 5 s interval furble offers is refused by the driver, not sent
  check(!Quectel::standby(5, standby), "5 s is not a documented backup interval");

  // the save path: PAIR513, then the settings survive a rail cut
  check(link.run(Quectel::saveSettings()) == GpsText::Verdict::ACKED, "PAIR513 is acknowledged");
  check(receiver.saved(), "the save reached the receiver");
  check(receiver.saves == 1, "one save");
  receiver.setPower(false, link.now);
  receiver.setPower(true, link.now);
  check(receiver.settings().rate_ms == 200, "the saved rate survives a rail cut");
  check(receiver.state() == Sim::VirtualQuectel::State::ON, "the receiver is powered again");

  // a receiver with no backup and no save comes up at the defaults
  Sim::VirtualQuectel fresh;
  Sim::VirtualQuectel::Options plain;
  fresh.reset(plain, 0);
  link_t<Sim::VirtualQuectel> freshLink {fresh};
  freshLink.run(Quectel::fixRate(100));
  check(fresh.settings().rate_ms == 100, "the rate is in RAM");
  fresh.setPower(false, freshLink.now);
  fresh.setPower(true, freshLink.now);
  check(fresh.settings().rate_ms == 1000, "without a save the rate is back at 1 Hz");

  // a receiver with a backup supply keeps it without a save
  Sim::VirtualQuectel backup;
  Sim::VirtualQuectel::Options kept;
  kept.backup = true;
  backup.reset(kept, 0);
  link_t<Sim::VirtualQuectel> backupLink {backup};
  backupLink.run(Quectel::fixRate(100));
  backup.setPower(false, backupLink.now);
  backup.setPower(true, backupLink.now);
  check(backup.settings().rate_ms == 100, "with a backup supply the rate survives");

  return g_Failures == before;
}

// b. Unicore.
bool testUnicore() {
  std::cout << "test: the virtual Unicore receiver answers like the documents\n";
  const int before = g_Failures;

  Sim::VirtualUnicore receiver;
  Sim::VirtualUnicore::Options options;
  receiver.reset(options, 0);
  link_t<Sim::VirtualUnicore> link {receiver};

  // identification, UC 3.2 Table 3-1 page 26
  check(link.run(Unicore::productInfo()) == GpsText::Verdict::ACKED,
        "the receiver identifies with PDTINFO");
  check(receiver.commands == 1, "one command reached the receiver");

  // a good command: $OK (UC 3.2 page 30)
  check(link.run(Unicore::navRate(200)) == GpsText::Verdict::ACKED, "CFGNAV is acknowledged");
  check(receiver.settings().rate_ms == 200, "the navigation rate landed in RAM");

  check(link.run(Unicore::messageRate(Unicore::MSG_GGA, 0)) == GpsText::Verdict::ACKED,
        "a sentence rate is acknowledged");
  check(receiver.settings().sentence[0] == 0, "GGA is off in the model");

  check(link.run(Unicore::system(GpsText::GNSS_GPS | GpsText::GNSS_GAL)) == GpsText::Verdict::ACKED,
        "CFGSYS is acknowledged");
  check(receiver.settings().sys == (Unicore::SYS_GPS_L1CA | Unicore::SYS_GAL_E1),
        "the receiver kept GPS L1CA and Galileo E1 only");
  check(receiver.resets == 1, "CFGSYS resets the receiver (UC 3.3 page 44)");

  // a FAIL: QZSS alone is refused by the seam before it is sent, so send the
  // body directly to prove the receiver refuses it too
  GpsText::Command qzss;
  qzss.dialect = GpsText::Dialect::UNICORE;
  qzss.op = GpsText::Op::GNSS;
  qzss.body = "CFGSYS,h10000";
  qzss.ack = "CFGSYS";
  check(link.run(qzss) == GpsText::Verdict::PARAM, "QZSS without GPS is a $FAIL,0");
  check(receiver.fails == 1, "the receiver refused it once");

  // a mismatched CFGNAV is the documented FAIL case
  GpsText::Command unequal;
  unequal.dialect = GpsText::Dialect::UNICORE;
  unequal.op = GpsText::Op::FIX_RATE;
  unequal.body = "CFGNAV,1000,200,";
  unequal.ack = "CFGNAV";
  check(link.run(unequal) == GpsText::Verdict::PARAM,
        "measRate and navRate must match (UC 3.3 page 43)");

  // a bad checksum: $FAIL,1. The session has already saved once by then, so a
  // refused save must not be counted.
  const uint32_t savesBefore = receiver.saves;
  link.corruptFrame = true;
  check(link.run(Unicore::save()) == GpsText::Verdict::BAD_REPLY,
        "a corrupt CFG frame is $FAIL,1 (UC 3.2 page 31)");
  link.corruptFrame = false;
  check(receiver.saves == savesBefore, "a refused save saves nothing");
  check(receiver.badChecksum == 1, "the receiver saw the checksum error");

  // a command this receiver does not know: no reply at all
  GpsText::Command unknown;
  unknown.dialect = GpsText::Dialect::UNICORE;
  unknown.body = "CFGZZZ,1";
  unknown.ack = "OK";
  link.session.clear();
  check(link.run(unknown) == GpsText::Verdict::TIMEOUT,
        "an illegal command gets no reply (UC 3.2 page 31)");
  check(receiver.unknown == GpsText::Session::ATTEMPTS,
        "every attempt was counted as an unknown command");

  // a receiver that answers nothing at all, for the absent case
  Sim::VirtualUnicore mute;
  Sim::VirtualUnicore::Options silent;
  silent.silent = true;
  mute.reset(silent, 0);
  link_t<Sim::VirtualUnicore> muteLink {mute};
  check(muteLink.run(Unicore::productInfo()) == GpsText::Verdict::TIMEOUT,
        "a silent receiver never identifies");

  // restart, then the save path
  check(link.run(Unicore::restart(2)) == GpsText::Verdict::ACKED, "$RESET is acknowledged");
  check(receiver.resets == 2, "the cold start reset the receiver");
  check(link.run(Unicore::save()) == GpsText::Verdict::ACKED, "CFGSAVE is acknowledged");
  check(receiver.saved() && (receiver.saves == 1), "the save reached the receiver");
  receiver.setPower(false, link.now);
  receiver.setPower(true, link.now);
  check(receiver.settings().rate_ms == 200, "the saved configuration survives a rail cut");

  // a product without flash refuses CFGSAVE
  Sim::VirtualUnicore flashless;
  Sim::VirtualUnicore::Options noFlash;
  noFlash.flash = false;
  flashless.reset(noFlash, 0);
  link_t<Sim::VirtualUnicore> flashlessLink {flashless};
  check(flashlessLink.run(Unicore::save()) == GpsText::Verdict::PARAM,
        "a flashless product refuses CFGSAVE (UC 3.3 page 47)");

  return g_Failures == before;
}

// The planner and the model agree on the whole queue, not just one command.
bool testPlansEndToEnd() {
  std::cout << "test: a whole plan runs against the virtual receivers\n";
  const int before = g_Failures;

  GpsText::Request request;
  request.rate_ms = 200;
  request.sentences = GpsText::SentenceSet::PRUNE;
  request.sentences_set = true;
  request.gnss = GpsText::GNSS_GPS | GpsText::GNSS_GAL;
  request.gnss_set = GpsText::GNSS_ALL;
  request.platform = 1;

  Sim::VirtualQuectel quectel;
  quectel.reset(Sim::VirtualQuectel::Options(), 0);
  link_t<Sim::VirtualQuectel> link {quectel};
  const Quectel::Plan plan = Quectel::plan(request);
  size_t acked = 0;
  for (const auto &command : plan.commands) {
    if (link.run(command) == GpsText::Verdict::ACKED) {
      acked++;
    }
  }
  check(acked == plan.commands.size(), "every Quectel plan command is acknowledged");
  check(quectel.settings().rate_ms == 200, "the receiver took the plan's rate");
  check(quectel.settings().sentence[0] == 1 && quectel.settings().sentence[1] == 0,
        "the receiver took the pruned sentence set");

  Sim::VirtualUnicore unicore;
  unicore.reset(Sim::VirtualUnicore::Options(), 0);
  link_t<Sim::VirtualUnicore> uniLink {unicore};
  const Unicore::Plan uniPlan = Unicore::plan(request);
  acked = 0;
  for (const auto &command : uniPlan.commands) {
    if (uniLink.run(command) == GpsText::Verdict::ACKED) {
      acked++;
    }
  }
  check(acked == uniPlan.commands.size(), "every Unicore plan command is acknowledged");
  check(unicore.settings().rate_ms == 200, "the receiver took the plan's rate");
  check(unicore.settings().sys == (Unicore::SYS_GPS_L1CA | Unicore::SYS_GAL_E1),
        "the receiver took the plan's constellation mask");

  return g_Failures == before;
}

}  // namespace

int main() {
  testQuectel();
  testUnicore();
  testPlansEndToEnd();

  if (g_Failures != 0) {
    std::cout << "gps text sim harness: FAIL (" << g_Failures << " of " << g_Checks << " checks)\n";
    return 1;
  }
  std::cout << "gps text sim harness: PASS (" << g_Checks << " checks)\n";
  return 0;
}
