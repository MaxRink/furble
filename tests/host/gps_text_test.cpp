// Host unit test for the vendor GNSS text command seam and the two dialects
// behind it: GpsText (framing, checksum, reply parsing, ack matching, timeout
// and retry), GpsQuectel ($PAIR and $PQTM) and GpsUnicore ($CFG). Pure code with
// no UART, so this pins the protocol half of plan 388 on its own.
//
// Coverage:
//   a. frame           the '$' body '*' XX CR LF form and the documented examples
//   b. prefix          which '$' names the stream splitter sends to a vendor
//   c. parse           name and fields, a bad checksum, a broken line
//   d. match           PAIR result codes, PQTM OK and ERROR, UNICORE OK and FAIL
//   e. Session         ack, nack, timeout, retry, and "processing" not costing a try
//   f. Quectel::plan   rate, sentence set, constellations, SBAS, mode, standby
//   g. Unicore::plan   rate, sentence set, sys mask, reset, save
//   h. demux           a vendor line among NMEA, split across reads, oversize
//
// The datasheet citations name the document, section and page of every protocol
// fact the assertions read.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "GpsDemux.h"
#include "GpsQuectel.h"
#include "GpsText.h"
#include "GpsUbx.h"
#include "GpsUnicore.h"

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
 * Parse one line into a GpsText::Line, failing the check when it will not.
 *
 * The terminator is appended when the literal has none: the splitter only ever
 * hands the driver a terminated line, and match() treats an unterminated one as
 * a malformed reply.
 */
GpsText::Line lineOf(const std::string &text, const char *message) {
  const std::string line_text = (text.find('\r') == std::string::npos) ? (text + "\r\n") : text;
  GpsText::Line line;
  if (!GpsText::parse(line_text.data(), line_text.size(), line)) {
    check(false, message);
  }
  return line;
}

// a. frame. Every expectation is the checksum a Quectel document prints next to
// the same sentence, so a frame() change fails on the vendor's own examples.
bool testFrame() {
  std::cout << "test: the frame is '$' body '*' XX CR LF\n";
  const int before = g_Failures;

  check(GpsText::frame("PAIR050,1000") == "$PAIR050,1000*12\r\n",
        "$PAIR050,1000 frames to the checksum Q76 2.3.9 page 38 prints");
  check(GpsText::frame("PQTMVERNO") == "$PQTMVERNO*58\r\n",
        "$PQTMVERNO frames to the checksum Q76 2.4.5 page 100 prints");
  check(GpsText::frame("PQTMSAVEPAR") == "$PQTMSAVEPAR*5A\r\n",
        "$PQTMSAVEPAR frames to the checksum Q76 2.4.3 page 98 prints");
  check(GpsText::frame("PAIR513") == "$PAIR513*3D\r\n",
        "$PAIR513 frames to the checksum Q76 2.3.45 page 68 prints");
  check(GpsText::frame("PAIR410,1") == "$PAIR410,1*22\r\n",
        "$PAIR410,1 frames to the checksum Q76 2.3.34 page 60 prints");
  check(GpsText::frame("PAIR066,1,1,1,1,0,0") == "$PAIR066,1,1,1,1,0,0*3A\r\n",
        "$PAIR066 frames to the checksum Q76 2.3.15 page 44 prints");
  check(GpsText::frame("OK") == "$OK*04\r\n", "$OK frames to the checksum UC 3.2 page 30 prints");
  check(GpsText::frame("FAIL,0") == "$FAIL,0*1E\r\n",
        "$FAIL,0 frames to the checksum UC 3.2 page 31 prints");
  check(GpsText::frame("").empty(), "an empty body is refused");
  check(GpsText::frame(std::string(GpsText::MAX_BODY + 1, 'A')).empty(),
        "a body past MAX_BODY is refused");
  check(GpsText::checksum("OK", 2) == 0x04, "the checksum is the XOR between $ and *");

  return g_Failures == before;
}

// b. prefix. The splitter must never take a $PCAS or a standard NMEA sentence
// for a vendor line, because that would starve the CASIC path and TinyGPSPlus.
bool testPrefix() {
  std::cout << "test: only vendor names route away from NMEA\n";
  const int before = g_Failures;

  auto kind = [](const std::string &text) {
    return GpsText::prefix(reinterpret_cast<const uint8_t *>(text.data()), text.size());
  };

  check(kind("$PAIR001,050,0*3E") == GpsText::Prefix::YES, "$PAIR001 is a vendor line");
  check(kind("$PQTMVERNO") == GpsText::Prefix::YES, "$PQTMVERNO is a vendor line");
  check(kind("$CFGSYS,h1") == GpsText::Prefix::YES, "$CFGSYS is a vendor line");
  check(kind("$PDTINFO") == GpsText::Prefix::YES, "$PDTINFO is a vendor line");
  check(kind("$OK*04") == GpsText::Prefix::YES, "$OK is a vendor line");
  check(kind("$FAIL,0*1E") == GpsText::Prefix::YES, "$FAIL is a vendor line");
  check(kind("$PAI") == GpsText::Prefix::MAYBE, "a partial name waits for more bytes");
  check(kind("$PQ") == GpsText::Prefix::MAYBE, "a two byte prefix waits for more bytes");
  check(kind("$P") == GpsText::Prefix::MAYBE, "a one byte prefix waits for more bytes");
  check(kind("$PCAS03,1,0,1") == GpsText::Prefix::NO, "$PCAS is not a vendor line");
  check(kind("$GPGGA,1") == GpsText::Prefix::NO, "a standard sentence is not a vendor line");
  check(kind("$Z") == GpsText::Prefix::NO, "an impossible prefix is rejected at once");
  check(kind("GP") == GpsText::Prefix::NO, "a line without '$' is not a vendor line");

  return g_Failures == before;
}

// c. parse. The name and the fields, the checksum verdict, and the shapes that
// must be refused rather than half decoded.
bool testParse() {
  std::cout << "test: a reply splits into a name and its fields\n";
  const int before = g_Failures;

  auto ack = lineOf("$PAIR001,050,0*3E", "a PAIR ack parses");
  check(ack.dialect == GpsText::Dialect::QUECTEL, "a PAIR ack is Quectel");
  check(ack.name == "PAIR001", "the name is PAIR001");
  check(ack.fields.size() == 2, "a PAIR ack carries two fields");
  check(!ack.fields.empty() && ack.fields[0] == "050", "field 0 is the acknowledged id");
  check(ack.fields.size() == 2 && ack.fields[1] == "0", "field 1 is the result");
  check(ack.checksum_ok, "the checksum of Q76 2.3.1 page 32 example is valid");
  check(ack.terminated, "the line ends with CR LF");

  auto version =
      lineOf("$PQTMVERNO,LC76GABNR02A01S,2022/09/14,11:47:03*3D", "a PQTM version reply parses");
  check(version.name == "PQTMVERNO", "the version reply carries the command name");
  check(version.fields.size() == 3, "the version reply carries three fields");
  check(version.fields.size() == 3 && version.fields[0] == "LC76GABNR02A01S",
        "field 0 is the version string");
  check(version.checksum_ok, "the checksum of the Q76 2.4.5 example is valid");

  auto product =
      lineOf("$PDTINFO,UC6580,G1B1L1E1,V1.0,R6.0.3.0Build7926,PN,SN*01", "a product reply parses");
  check(product.dialect == GpsText::Dialect::UNICORE, "a product reply is Unicore");
  check(product.name == "PDTINFO", "the name is PDTINFO");
  check(product.fields.size() == 6, "the product reply carries six fields");
  check(!product.checksum_ok, "a checksum copied from another reply is refused");

  auto ok = lineOf("$OK*04", "an OK reply parses");
  check(ok.name == "OK" && ok.fields.empty(), "$OK carries no fields");
  check(ok.dialect == GpsText::Dialect::UNICORE, "$OK is Unicore");

  auto fail = lineOf("$FAIL,0*1E", "a FAIL reply parses");
  check(fail.name == "FAIL" && fail.fields.size() == 1 && fail.fields[0] == "0",
        "$FAIL,0 carries the error code");

  GpsText::Line broken;
  const std::string noDollar = "PAIR001,050,0*3E";
  check(!GpsText::parse(noDollar.data(), noDollar.size(), broken), "a line without '$' is refused");
  const std::string noChecksum = "$PAIR001,050,0";
  check(!GpsText::parse(noChecksum.data(), noChecksum.size(), broken),
        "a line without a checksum is refused");
  const std::string shortChecksum = "$PAIR001,050,0*";
  check(!GpsText::parse(shortChecksum.data(), shortChecksum.size(), broken),
        "a truncated checksum is refused");
  const std::string earlyCr = "$PAIR001,0\r\n*3E";
  check(!GpsText::parse(earlyCr.data(), earlyCr.size(), broken),
        "a terminator before '*' is refused");

  auto bad = lineOf("$PAIR001,050,0*3F", "a bad checksum still parses");
  check(!bad.checksum_ok, "a wrong checksum is reported, not swallowed as silence");

  auto short_line = lineOf("$PAIR001,050,0*3E\r\n", "a full line parses");
  check(short_line.terminated, "the CR LF is recorded");

  return g_Failures == before;
}

// d. match. The verdicts the two dialects define, including the wrong-id case
// that must not count as an answer at all.
bool testMatch() {
  std::cout << "test: a reply is matched to the command in flight\n";
  const int before = g_Failures;

  const GpsText::Command fix = Quectel::fixRate(1000);
  check(fix.body == "PAIR050,1000", "the fix rate body matches Q76 2.3.9 page 38");
  check(fix.ack == "PAIR001" && fix.ack_id == 50, "it waits for PAIR001 of id 050");

  check(GpsText::match(lineOf("$PAIR001,050,0*3E", "ack"), fix) == GpsText::Verdict::ACKED,
        "result 0 is an acknowledgement");
  check(GpsText::match(lineOf("$PAIR001,050,1*3F", "pending"), fix) == GpsText::Verdict::PENDING,
        "result 1 is still processing");
  check(GpsText::match(lineOf("$PAIR001,050,2*3C", "failed"), fix) == GpsText::Verdict::NACKED,
        "result 2 is a refusal");
  check(GpsText::match(lineOf("$PAIR001,050,3*3D", "unsupported"), fix)
            == GpsText::Verdict::UNSUPPORTED,
        "result 3 is not supported");
  check(GpsText::match(lineOf("$PAIR001,050,4*3A", "param"), fix) == GpsText::Verdict::PARAM,
        "result 4 is a parameter error");
  check(GpsText::match(lineOf("$PAIR001,050,5*3B", "busy"), fix) == GpsText::Verdict::BUSY,
        "result 5 is busy");
  check(GpsText::match(lineOf("$PAIR001,051,0*3F", "other id"), fix) == GpsText::Verdict::NONE,
        "an ack for another packet id is not this command's answer");
  check(GpsText::match(lineOf("$PAIR001,050,0*3F", "bad checksum"), fix)
            == GpsText::Verdict::BAD_REPLY,
        "a corrupt ack is a malformed reply");
  check(GpsText::match(lineOf("$PAIR001,050*22", "short ack"), fix) == GpsText::Verdict::BAD_REPLY,
        "an ack without a result is malformed");

  const GpsText::Command save = Quectel::saveParameters();
  check(save.body == "PQTMSAVEPAR" && save.ack == "PQTMSAVEPAR", "the save waits for its own name");
  check(GpsText::match(lineOf("$PQTMSAVEPAR,OK*72", "save ok"), save) == GpsText::Verdict::ACKED,
        "PQTMSAVEPAR,OK is an acknowledgement (Q76 2.4.3 page 98)");
  check(GpsText::match(lineOf("$PQTMSAVEPAR,ERROR,2*30", "save error"), save)
            == GpsText::Verdict::PARAM,
        "PQTMSAVEPAR,ERROR is a refusal");

  const GpsText::Command info = Unicore::productInfo();
  check(GpsText::match(lineOf("$PDTINFO,UC6580,G1B1.,V1.0,R6,PN,SN*00", "pdt"), info)
            == GpsText::Verdict::BAD_REPLY,
        "a product reply with a wrong checksum is malformed");
  const GpsText::Command sys = Unicore::system(GpsText::GNSS_GPS | GpsText::GNSS_GAL);
  check(GpsText::match(lineOf("$CFGSYS,h1001*00", "sys"), sys) == GpsText::Verdict::BAD_REPLY,
        "a CFGSYS reply with a wrong checksum is malformed");

  GpsText::Command any = Unicore::save();
  check(GpsText::match(lineOf("$OK*04", "ok"), any) == GpsText::Verdict::ACKED,
        "$OK acknowledges any Unicore command (UC 3.2 page 30)");
  check(GpsText::match(lineOf("$FAIL,0*1E", "fail"), any) == GpsText::Verdict::PARAM,
        "$FAIL,0 is a rejected parameter (UC 3.2 page 31)");
  check(GpsText::match(lineOf("$FAIL,1*1F", "fail checksum"), any) == GpsText::Verdict::BAD_REPLY,
        "$FAIL,1 is a checksum the receiver refused");

  check(refused(GpsText::Verdict::UNSUPPORTED) && !refused(GpsText::Verdict::ACKED)
            && !refused(GpsText::Verdict::PENDING),
        "a stated refusal is told apart from an acceptance");
  check(std::strcmp(GpsText::verdictName(GpsText::Verdict::UNSUPPORTED), "unsupported") == 0,
        "a verdict has a printable name");

  return g_Failures == before;
}

// e. Session. One command in flight, a retry when the line is lost, and a
// "processing" answer that does not spend an attempt.
bool testSession() {
  std::cout << "test: the session owns the timeout and the retry\n";
  const int before = g_Failures;

  GpsText::Session session;
  const GpsText::Command fix = Quectel::fixRate(200);
  check(!session.active(), "an untouched session is idle");
  check(session.service(0) == GpsText::Session::Step::IDLE, "service on an idle session is idle");

  session.begin(fix, 1000);
  check(session.active() && (session.attempts() == 1), "begin starts the first attempt");
  check(session.service(1500) == GpsText::Session::Step::WAIT, "inside the window it waits");
  check(session.service(2000) == GpsText::Session::Step::SEND, "past the window it resends");
  check(session.attempts() == 2, "the resend counts an attempt");
  check(session.service(3000) == GpsText::Session::Step::SEND, "the third attempt is sent");
  check(session.service(4000) == GpsText::Session::Step::TIMEOUT, "after three it gives up");
  check(session.verdict() == GpsText::Verdict::TIMEOUT, "the verdict is a timeout");
  check(!session.active(), "the session is closed after a timeout");

  // The splitter only hands the driver a terminated line.
  auto feed = [&session](const char *text) {
    const std::string line = std::string(text) + "\r\n";
    return session.accept(line.data(), line.size());
  };

  session.begin(fix, 0);
  check(feed("$PAIR001,050,0*3E") == GpsText::Verdict::ACKED, "a matching ack is accepted");
  check(session.service(10) == GpsText::Session::Step::ACKED, "the session reports the ack");
  check(!session.active(), "the session is closed after an ack");

  session.begin(fix, 0);
  check(feed("$PAIR001,050,1*3F") == GpsText::Verdict::PENDING, "result 1 is pending");
  check(session.service(500) == GpsText::Session::Step::WAIT, "pending keeps waiting");
  check(session.attempts() == 1, "pending does not spend an attempt");
  check(session.service(1500) == GpsText::Session::Step::SEND,
        "a pending answer that never resolves still retries");

  session.begin(fix, 0);
  check(feed("$PAIR001,050,3*3D") == GpsText::Verdict::UNSUPPORTED,
        "the receiver refuses an unsupported command");
  check(session.service(0) == GpsText::Session::Step::FAILED, "the session reports the refusal");
  check(session.verdict() == GpsText::Verdict::UNSUPPORTED, "the verdict keeps the reason");

  session.begin(Unicore::save(), 0);
  check(feed("$FAIL,0*1E") == GpsText::Verdict::PARAM, "$FAIL,0 is a refusal");
  check(session.service(0) == GpsText::Session::Step::FAILED, "the Unicore refusal closes it");

  session.begin(fix, 0);
  check(feed("$PAIR001,051,0*3F") == GpsText::Verdict::NONE,
        "an ack for another command leaves the session waiting");
  check(session.service(0) == GpsText::Session::Step::WAIT, "and it keeps waiting");
  session.clear();
  check(!session.active(), "clear drops the command");

  return g_Failures == before;
}

// f. Quectel::plan. Every value here comes from the command table of plan 388.
bool testQuectelPlan() {
  std::cout << "test: the Quectel planner\n";
  const int before = g_Failures;

  GpsText::Request request;
  request.rate_ms = 200;
  request.sentences = GpsText::SentenceSet::PRUNE;
  request.sentences_set = true;
  request.gnss = GpsText::GNSS_GPS | GpsText::GNSS_GAL;
  request.gnss_set = GpsText::GNSS_ALL;
  request.platform = 1;
  Quectel::Plan plan = Quectel::plan(request);

  check(plan.rate_ms == 200 && !plan.rate_refused, "200 ms is inside PAIR050's range");
  check(plan.gnss == (GpsText::GNSS_GPS | GpsText::GNSS_GAL), "the plan keeps the mask");
  check(!plan.gnss_refused, "GPS plus Galileo is a documented combination");
  check(!plan.platform_refused && plan.nav_mode == 0, "portable is PAIR080 normal mode");
  check(!plan.sbas, "SBAS off is sent because the mask decides it");

  // six sentence commands, the fix rate, the mode, SBAS, the search mode
  check(plan.commands.size() == 10, "the plan queues ten commands");
  check(plan.commands[0].body == "PAIR062,0,1", "PRUNE keeps GGA");
  check(plan.commands[1].body == "PAIR062,1,0", "PRUNE drops GLL");
  check(plan.commands[2].body == "PAIR062,2,0", "PRUNE drops GSA");
  check(plan.commands[3].body == "PAIR062,3,0", "PRUNE drops GSV");
  check(plan.commands[4].body == "PAIR062,4,1", "PRUNE keeps RMC");
  check(plan.commands[5].body == "PAIR062,5,0", "PRUNE drops VTG");
  check(plan.commands[6].body == "PAIR050,200", "the fix rate follows the sentence set");
  check(plan.commands[7].body == "PAIR080,0", "portable maps to normal");
  check(plan.commands[8].body == "PAIR410,0", "SBAS is decided by the mask");
  check(plan.commands[9].body == "PAIR066,1,0,1,0,0,0",
        "PAIR066 is last because it reboots an LC76G (Q76 2.3.15 page 43)");

  // the two-of-three gap: GPS plus GLONASS plus Galileo is not in the list
  request = GpsText::Request();
  request.gnss = GpsText::GNSS_GPS | GpsText::GNSS_GLO | GpsText::GNSS_GAL;
  request.gnss_set = GpsText::GNSS_ALL;
  plan = Quectel::plan(request);
  check(plan.gnss_refused, "GPS plus GLONASS plus Galileo is refused");
  check(plan.commands.size() == 1, "only the SBAS command is left");
  check(plan.commands[0].body == "PAIR410,0", "SBAS is still decided");

  // all three plus QZSS is documented
  request = GpsText::Request();
  request.gnss = GpsText::GNSS_GPS | GpsText::GNSS_GLO | GpsText::GNSS_GAL | GpsText::GNSS_BDS
                 | GpsText::GNSS_QZSS;
  request.gnss_set = GpsText::GNSS_ALL;
  plan = Quectel::plan(request);
  check(!plan.gnss_refused, "the full set is documented");
  check(plan.commands.size() == 2 && plan.commands[0].body == "PAIR410,0"
            && plan.commands[1].body == "PAIR066,1,1,1,1,1,0",
        "the full set is written in PAIR066 field order, Galileo before BDS");

  // pedestrian is fitness mode, which forbids SBAS on the same receiver
  request = GpsText::Request();
  request.gnss = GpsText::GNSS_GPS | GpsText::GNSS_SBAS;
  request.gnss_set = GpsText::GNSS_ALL;
  request.platform = 3;
  plan = Quectel::plan(request);
  check(plan.nav_mode == 1, "pedestrian maps to PAIR080 fitness");
  check(plan.sbas_refused && !plan.sbas, "fitness mode refuses SBAS (Q76 2.3.34 page 60)");
  check(plan.commands.size() == 2, "the mode and the constellation are queued");
  check(plan.commands[0].body == "PAIR080,1", "the fitness mode goes first");
  check(plan.commands[1].body == "PAIR066,1,0,0,0,0,0", "the search mode goes last");

  // stationary and vehicle have no documented Quectel mode
  request = GpsText::Request();
  request.platform = 2;
  plan = Quectel::plan(request);
  check(plan.platform_refused && plan.commands.empty(), "stationary is refused, not faked");
  request.platform = 4;
  plan = Quectel::plan(request);
  check(plan.platform_refused, "vehicle is refused, not faked");

  // a rate outside PAIR050's documented range is refused
  request = GpsText::Request();
  request.rate_ms = 50;
  plan = Quectel::plan(request);
  check(plan.rate_refused && plan.rate_ms == 0, "50 ms is outside PAIR050's 100 to 1000 ms");

  // standby: PAIR650 accepts 0 and 10 s upward, so a 5 s duty gap is refused
  GpsText::Command standby;
  check(!Quectel::standby(5, standby), "a 5 s backup interval is refused");
  check(Quectel::standby(10, standby) && standby.body == "PAIR650,10",
        "10 s is the documented minimum (Q76 2.3.46 page 69)");
  check(Quectel::standby(15, standby) && standby.body == "PAIR650,15", "15 s is accepted");
  check(!Quectel::standby(Quectel::BACKUP_MAX_S + 1, standby), "past two years is refused");

  check(Quectel::restart(0).body == "PAIR004", "hot start is PAIR004");
  check(Quectel::restart(1).body == "PAIR005", "warm start is PAIR005");
  check(Quectel::restart(2).body == "PAIR006", "cold start is PAIR006");
  check(Quectel::baud(115200).body == "PAIR864,0,0,115200", "115200 is documented");
  check(Quectel::baud(4800).body.empty(), "4800 is only in the LC29H list, so it is not written");
  check(Quectel::lockSleep().body == "PAIR382,1", "the sleep lock precedes a GNSS stop");
  check(Quectel::saveSettings().body == "PAIR513", "the settings save is PAIR513");
  check(Quectel::saveNavigation().body == "PAIR511", "the navigation save is PAIR511");
  check(Quectel::verNo().body == "PQTMVERNO" && Quectel::verNo().ack == "PQTMVERNO",
        "identification is a bare PQTMVERNO");
  check(Quectel::messageRate(1).body == "PQTMCFGMSGRATE,W,PQTMEPE,1,2",
        "a PQTM message rate carries its version");

  return g_Failures == before;
}

// g. Unicore::plan.
bool testUnicorePlan() {
  std::cout << "test: the Unicore planner\n";
  const int before = g_Failures;

  GpsText::Request request;
  request.rate_ms = 100;
  request.sentences = GpsText::SentenceSet::SATELLITE;
  request.sentences_set = true;
  request.gnss = GpsText::GNSS_GPS | GpsText::GNSS_GAL | GpsText::GNSS_BDS;
  request.gnss_set = GpsText::GNSS_ALL;
  Unicore::Plan plan = Unicore::plan(request);

  check(plan.rate_ms == 100 && !plan.rate_refused, "100 ms is inside CFGNAV's range");
  check(plan.commands.size() == 8, "six sentence commands, the rate and the system mask");
  check(plan.commands[0].body == "CFGMSG,0,0,1", "SATELLITE keeps GGA");
  check(plan.commands[2].body == "CFGMSG,0,2,1", "SATELLITE adds GSA");
  check(plan.commands[3].body == "CFGMSG,0,3,1", "SATELLITE adds GSV");
  check(plan.commands[1].body == "CFGMSG,0,1,0", "SATELLITE drops GLL");
  check(plan.commands[6].body == "CFGNAV,100,100,", "the two rates must match (UC 3.3 page 43)");
  check(plan.commands[7].body == "CFGSYS,h1011",
        "GPS L1CA, BDS B1I and Galileo E1 are bits 0, 4 and 12 (UC 3.3 page 45)");
  check(plan.sys_mask == (Unicore::SYS_GPS_L1CA | Unicore::SYS_BDS_B1I | Unicore::SYS_GAL_E1),
        "the plan keeps the mask it sent");
  check(!plan.gnss_refused, "the mask is expressible");

  request = GpsText::Request();
  request.gnss = GpsText::GNSS_QZSS;
  request.gnss_set = GpsText::GNSS_ALL;
  plan = Unicore::plan(request);
  check(plan.gnss_refused && plan.commands.empty(), "QZSS without GPS is refused");

  request = GpsText::Request();
  request.gnss = GpsText::GNSS_QZSS | GpsText::GNSS_GPS;
  request.gnss_set = GpsText::GNSS_ALL;
  plan = Unicore::plan(request);
  check(!plan.gnss_refused, "QZSS with GPS is accepted");
  check(plan.commands.size() == 1 && plan.commands[0].body == "CFGSYS,h10001",
        "QZSS L1CA is bit 16");

  request = GpsText::Request();
  request.gnss = GpsText::GNSS_SBAS | GpsText::GNSS_GLO;
  request.gnss_set = GpsText::GNSS_ALL;
  plan = Unicore::plan(request);
  check(plan.gnss_refused, "SBAS without GPS or BDS is refused");

  request = GpsText::Request();
  request.gnss = GpsText::GNSS_SBAS | GpsText::GNSS_BDS;
  request.gnss_set = GpsText::GNSS_ALL;
  plan = Unicore::plan(request);
  check(
      !plan.gnss_refused && plan.commands.size() == 1 && plan.commands[0].body == "CFGSYS,h100010",
      "SBAS is bit 20 and BDS B1I is bit 4");

  request = GpsText::Request();
  request.psm = GpsText::PSM_CYCLIC;
  request.platform = 3;
  request.rate_ms = 2000;
  plan = Unicore::plan(request);
  check(plan.psm_refused, "Unicore documents no standby command, so it is refused");
  check(plan.platform_refused, "Unicore documents no dynamic model, so it is refused");
  check(plan.rate_refused, "2 s is outside CFGNAV's 50 to 1000 ms");

  GpsText::Command port;
  check(Unicore::port(115200, port) && port.body == "CFGPRT,1,0,115200,1,3",
        "the port keeps UNICORE input and adds NMEA output (UC 3.3 page 33)");
  check(!Unicore::port(1200, port), "1200 baud is not in CFGPRT's list");
  check(Unicore::save().body == "CFGSAVE", "the save is CFGSAVE");
  check(Unicore::restart(0).body == "RESET,0,h00", "hot start is RESET with a zero mask");
  check(Unicore::restart(1).body == "RESET,0,h01", "warm start clears bit 0");
  check(Unicore::restart(2).body == "RESET,0,h85", "cold start is h85 (UC 3.2 page 29)");
  check(Unicore::productInfo().body == "PDTINFO", "identification is a bare PDTINFO");

  return g_Failures == before;
}

// h. demux. A vendor line is its own piece, NMEA is not disturbed, and a line
// split across reads or over the length cap is handled without growing the tail.
bool testDemux() {
  std::cout << "test: the splitter routes vendor lines without touching NMEA\n";
  const int before = g_Failures;

  const std::string stream =
      "$GPGGA,120000,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*49\r\n"
      "$PAIR001,050,0*3E\r\n"
      "$GNRMC,120000,A,4807.038,N,01131.000,E,0.0,0.0,060926,,,A*64\r\n"
      "$OK*04\r\n";
  std::vector<GpsDemux::Piece> pieces;
  const size_t used =
      GpsDemux::split(reinterpret_cast<const uint8_t *>(stream.data()), stream.size(), pieces);
  check(used == stream.size(), "the whole stream is consumed");

  std::string nmea;
  std::vector<std::string> vendors;
  for (const auto &piece : pieces) {
    const std::string text = stream.substr(piece.offset, piece.length);
    if (piece.kind == GpsDemux::Kind::NMEA) {
      nmea += text;
    } else if (piece.kind == GpsDemux::Kind::VENDOR) {
      vendors.push_back(text);
    }
  }
  check(nmea.find("$GPGGA") != std::string::npos && nmea.find("$GNRMC") != std::string::npos,
        "both NMEA sentences stay NMEA");
  check(nmea.find("$PAIR") == std::string::npos && nmea.find("$OK") == std::string::npos,
        "neither vendor line reaches the NMEA parser");
  check(vendors.size() == 2, "both vendor lines are their own piece");
  check(!vendors.empty() && vendors[0] == "$PAIR001,050,0*3E\r\n",
        "the vendor piece carries the whole sentence and its terminator");

  // the CASIC text command still goes to NMEA, so the existing path is intact
  const std::string casic = "$PCAS03,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0*1E\r\n";
  pieces.clear();
  GpsDemux::split(reinterpret_cast<const uint8_t *>(casic.data()), casic.size(), pieces);
  check(pieces.size() == 1 && pieces[0].kind == GpsDemux::Kind::NMEA,
        "$PCAS stays on the NMEA path");

  // a vendor line split across two reads is buffered, not misrouted
  const std::string line = "$PAIR001,050,0*3E\r\n";
  const size_t terminator = line.find('\r');
  for (size_t cut = 1; cut < terminator; cut++) {
    pieces.clear();
    const size_t used_first =
        GpsDemux::split(reinterpret_cast<const uint8_t *>(line.data()), cut, pieces);
    bool saw_vendor_first = false;
    for (const auto &piece : pieces) {
      saw_vendor_first = saw_vendor_first || (piece.kind == GpsDemux::Kind::VENDOR);
    }
    check(!saw_vendor_first, "a partial vendor line is never emitted");
    check(used_first == 0, "a partial vendor line is kept whole");
  }

  // a runaway line cannot grow the tail past MAX_PENDING
  std::string runaway = "$PAIR001,";
  runaway.append(GpsText::MAX_LINE + 20, '9');
  pieces.clear();
  const size_t kept =
      GpsDemux::split(reinterpret_cast<const uint8_t *>(runaway.data()), runaway.size(), pieces);
  check((runaway.size() - kept) <= GpsText::MAX_LINE, "the tail stays bounded");
  bool dropped = false;
  for (const auto &piece : pieces) {
    dropped = dropped || (piece.kind == GpsDemux::Kind::DROP);
  }
  check(dropped, "an over-long vendor line is dropped a byte at a time");

  // a vendor line with no terminator yet is kept, and completed on the next read
  std::vector<uint8_t> buffer;
  const std::string tail = "$CFGSYS,h1001*00";
  buffer.insert(buffer.end(), tail.begin(), tail.end());
  pieces.clear();
  size_t done = GpsDemux::split(buffer.data(), buffer.size(), pieces);
  check(done == 0 && pieces.empty(), "an unterminated vendor line waits");
  const std::string rest = "\r\n";
  buffer.insert(buffer.end(), rest.begin(), rest.end());
  pieces.clear();
  done = GpsDemux::split(buffer.data(), buffer.size(), pieces);
  check(done == buffer.size() && pieces.size() == 1 && pieces[0].kind == GpsDemux::Kind::VENDOR,
        "the completed line is one vendor piece");

  // the constellation mask is one setting shared with the UBX planner
  check((GpsText::GNSS_GPS == Ubx::GNSS_MASK_GPS) && (GpsText::GNSS_BDS == Ubx::GNSS_MASK_BDS)
            && (GpsText::GNSS_GLO == Ubx::GNSS_MASK_GLO)
            && (GpsText::GNSS_GAL == Ubx::GNSS_MASK_GAL)
            && (GpsText::GNSS_QZSS == Ubx::GNSS_MASK_QZSS)
            && (GpsText::GNSS_SBAS == Ubx::GNSS_MASK_SBAS)
            && (GpsText::GNSS_ALL == Ubx::GNSS_MASK_ALL),
        "GPS_GNSS bit values match the UBX planner");
  check((GpsText::PSM_OFF == Ubx::PSM_OFF) && (GpsText::PSM_CYCLIC == Ubx::PSM_CYCLIC)
            && (GpsText::PSM_ON_OFF == Ubx::PSM_ON_OFF),
        "GPS_PSM values match the UBX planner");

  return g_Failures == before;
}

}  // namespace

int main() {
  testFrame();
  testPrefix();
  testParse();
  testMatch();
  testSession();
  testQuectelPlan();
  testUnicorePlan();
  testDemux();

  if (g_Failures != 0) {
    std::cout << "gps text harness: FAIL (" << g_Failures << " of " << g_Checks << " checks)\n";
    return 1;
  }
  std::cout << "gps text harness: PASS (" << g_Checks << " checks)\n";
  return 0;
}
