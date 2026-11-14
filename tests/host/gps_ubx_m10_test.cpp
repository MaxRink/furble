// Host test for receiver detection (Ubx::Detect) and the simulator's virtual
// u-blox M10 (sim/ubx_m10.{h,cpp}). The virtual receiver owns no clock or UART,
// so the scenarios' state machine is exercised here byte for byte: VALSET
// validity rules, configuration layers, software standby, rail cuts with and
// without a backup supply, UPD-SOS, CFG-RST, and the current model.

#include <iostream>
#include <vector>

#include "protocol/GpsUbx.h"
#include "ubx_m10.h"

using namespace Furble;
using Furble::Sim::VirtualM10;

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

/** Split receiver output into frames. */
std::vector<std::vector<uint8_t>> frames(const std::vector<uint8_t> &bytes) {
  Ubx::Stream stream;
  std::vector<std::vector<uint8_t>> out;
  for (const uint8_t b : bytes) {
    if (stream.push(b)) {
      out.push_back(stream.frame());
    }
  }
  return out;
}

bool acked(const std::vector<uint8_t> &reply, bool ack) {
  const auto list = frames(reply);
  return !list.empty() && list.back()[2] == Ubx::CLASS_ACK
         && list.back()[3] == (ack ? Ubx::ID_ACK_ACK : Ubx::ID_ACK_NAK);
}

std::vector<uint8_t> send(VirtualM10 &m10, const std::vector<uint8_t> &frame, uint32_t now) {
  return m10.receive(frame.data(), frame.size(), now);
}

void testDetect() {
  Ubx::Detect detect;
  bool poll = true;
  check(detect.result() == Ubx::Detect::Result::IDLE, "idle before begin");
  check(detect.service(0, poll) == Ubx::Detect::Result::IDLE && !poll, "idle does nothing");

  detect.begin(100);
  check(detect.service(1099, poll) == Ubx::Detect::Result::WAITING && !poll, "first wait");
  check(detect.service(1100, poll) == Ubx::Detect::Result::WAITING && poll, "second poll");
  check(detect.attempts() == 2 && detect.deadline() == 2100, "second deadline");
  check(detect.service(2099, poll) == Ubx::Detect::Result::WAITING && !poll, "second wait");
  check(detect.service(2100, poll) == Ubx::Detect::Result::OTHER && !poll, "gives up");

  detect.begin(0xfffffe00u);  // across the tick wrap
  check(detect.service(0x100, poll) == Ubx::Detect::Result::WAITING && !poll, "wrap waits");
  detect.answered();
  check(detect.service(0x10000, poll) == Ubx::Detect::Result::UBX && !poll, "answer wins");

  detect.begin(0);
  check(detect.service(5000, poll) == Ubx::Detect::Result::WAITING && poll, "late poll");
  detect.answered();
  check(detect.result() == Ubx::Detect::Result::UBX, "an answer after the retry still counts");
}

void testMonVerAndLayers() {
  VirtualM10 m10;
  m10.reset({}, 0);
  const auto reply = frames(send(m10, Ubx::pollMonVer(), 10));
  Ubx::MonVer ver;
  check(reply.size() == 1 && Ubx::parseMonVer(reply[0].data() + 6, reply[0].size() - 8, ver)
            && ver.field("MOD") == "MAX-M10S" && ver.field("FWVER") == "SPG 5.10",
        "MON-VER answer");

  // RAM only: lost by a standby; RAM+BBR survives it.
  check(acked(send(m10,
                   Ubx::valSet(Ubx::LAYER_RAM,
                               {
                                   {Ubx::Key::RATE_MEAS, 500}
  }),
                   20),
              true),
        "RAM VALSET acked");
  check(acked(send(m10,
                   Ubx::valSet(Ubx::LAYER_RAM | Ubx::LAYER_BBR,
                               {
                                   {Ubx::Key::MSGOUT_NAV_PVT_UART1, 1}
  }),
                   30),
              true),
        "RAM+BBR VALSET acked");
  check(m10.value(Ubx::Key::RATE_MEAS) == 500 && m10.periodMs() == 500, "RAM rate applies");
  send(m10,
       Ubx::pmReq({2000, Ubx::PMREQ_FLAG_BACKUP | Ubx::PMREQ_FLAG_FORCE, Ubx::PMREQ_WAKE_UARTRX}),
       40);
  check(m10.state() == VirtualM10::State::STANDBY && m10.pmreqs == 1, "standby entered");
  check(!m10.outputting(1000), "silent in standby");
  check(m10.outputting(2040), "wakes after its duration");
  check(m10.value(Ubx::Key::RATE_MEAS) == 1000, "standby dropped the RAM-only value");
  check(m10.value(Ubx::Key::MSGOUT_NAV_PVT_UART1) == 1, "BBR value reloaded on wake");

  // VALGET by layer.
  const auto get = frames(send(
      m10, Ubx::valGet(Ubx::GET_BBR, {Ubx::Key::RATE_MEAS, Ubx::Key::MSGOUT_NAV_PVT_UART1}), 3000));
  Ubx::ValGet value;
  check(get.size() == 2 && Ubx::parseValGet(get[0].data() + 6, get[0].size() - 8, value)
            && value.items.size() == 1 && value.items[0].key == Ubx::Key::MSGOUT_NAV_PVT_UART1,
        "BBR VALGET returns only BBR keys");
  check(acked(send(m10, Ubx::valGet(Ubx::GET_FLASH, {Ubx::Key::RATE_MEAS}), 3100), false),
        "flash VALGET without a flash is refused");
  check(acked(send(m10, Ubx::valDel(Ubx::LAYER_BBR, {Ubx::Key::MSGOUT_NAV_PVT_UART1}), 3200), true)
            && !m10.hasBbr(Ubx::Key::MSGOUT_NAV_PVT_UART1),
        "VALDEL clears BBR");
}

void testValidity() {
  VirtualM10 m10;
  m10.reset({}, 0);
  struct Case {
    std::vector<Ubx::KeyValue> items;
    bool ok;
    const char *what;
  };
  const Case cases[] = {
      {{{Ubx::Key::SIGNAL_GLO_ENA, 1}},                                                              false, "GLONASS with the default B1I is refused"},
      {{{Ubx::Key::SIGNAL_BDS_B1, 0}, {Ubx::Key::SIGNAL_BDS_B1C, 1}, {Ubx::Key::SIGNAL_GLO_ENA, 1}},
       true,                                                                                                "four GNSS with B1C is accepted"         },
      {{{Ubx::Key::SIGNAL_BDS_B1C, 1}},                                                              false, "B1I with B1C is refused"                },
      {{{Ubx::Key::SIGNAL_GPS_ENA, 0},
        {Ubx::Key::SIGNAL_GAL_ENA, 0},
        {Ubx::Key::SIGNAL_BDS_ENA, 0}},
       false,                                                                                               "no major GNSS is refused"               },
      {{{Ubx::Key::RATE_MEAS, 24}},                                                                  false, "rate below 25 ms is refused"            },
      {{{Ubx::Key::NAVSPG_DYNMODEL, 1}},                                                             false, "dynamic model 1 is refused"             },
      {{{Ubx::Key::NAVSPG_DYNMODEL, 3}},                                                             true,  "pedestrian is accepted"                 },
      {{{Ubx::Key::PM_OPERATEMODE, 1}, {Ubx::Key::PM_POSUPDATEPERIOD, 4}},
       false,                                                                                               "PSMOO below 5 s is refused"             },
      {{{Ubx::Key::PM_OPERATEMODE, 2}, {Ubx::Key::RATE_MEAS, 200}},
       false,                                                                                               "PSMCT at 5 Hz is refused"               },
      {{{Ubx::Key::PM_OPERATEMODE, 2}},                                                              true,  "PSMCT at 1 Hz is accepted"              },
      {{{0x10ff0001, 1}},                                                                            false, "an unknown key is refused"              },
  };
  for (const auto &item : cases) {
    VirtualM10 fresh;
    fresh.reset({}, 0);
    check(acked(send(fresh, Ubx::valSet(Ubx::LAYER_RAM, item.items), 10), item.ok), item.what);
  }

  // Power save is refused once B1C is in use (RN 7.2).
  check(acked(send(m10,
                   Ubx::valSet(Ubx::LAYER_RAM,
                               {
                                   {Ubx::Key::SIGNAL_BDS_B1,  0},
                                   {Ubx::Key::SIGNAL_BDS_B1C, 1},
                                   {Ubx::Key::PM_OPERATEMODE, 2}
  }),
                   10),
              false),
        "power save with B1C is refused");
  // A refused VALSET changes nothing.
  check(m10.value(Ubx::Key::SIGNAL_BDS_B1) == 1, "refused VALSET left RAM unchanged");
  // Validity is only checked for RAM: a BBR-only write is stored.
  check(acked(send(m10,
                   Ubx::valSet(Ubx::LAYER_BBR,
                               {
                                   {Ubx::Key::SIGNAL_GLO_ENA, 1}
  }),
                   20),
              true),
        "BBR-only write is not validated");
}

void testStartsAndPower() {
  // No backup supply: a rail cut loses BBR and costs a cold start.
  VirtualM10 m10;
  m10.reset({}, 0);
  check(std::string(m10.startName()) == "cold" && !m10.fixed(26999) && m10.fixed(27000),
        "first power up is cold, 27 s");
  m10.epoch(30000, 2026, 9, 6, 0);
  send(m10,
       Ubx::valSet(Ubx::LAYER_BBR,
                   {
                       {Ubx::Key::NAVSPG_DYNMODEL, 3}
  }),
       30100);
  m10.setPower(false, 31000);
  m10.setPower(true, 32000);
  check(std::string(m10.startName()) == "cold" && !m10.hasBbr(Ubx::Key::NAVSPG_DYNMODEL),
        "rail cut without V_BCKP is cold and loses BBR");

  // With a backup supply the same cut is a hot start and BBR survives.
  VirtualM10 backed;
  backed.reset({true, false, "MAX-M10S", false, 0}, 0);
  backed.epoch(30000, 2026, 9, 6, 0);
  send(backed,
       Ubx::valSet(Ubx::LAYER_BBR,
                   {
                       {Ubx::Key::NAVSPG_DYNMODEL, 3}
  }),
       30100);
  backed.setPower(false, 31000);
  check(backed.state() == VirtualM10::State::OFF && !backed.outputting(31500), "off is silent");
  backed.setPower(true, 32000);
  check(std::string(backed.startName()) == "hot" && backed.fixed(33000)
            && backed.value(Ubx::Key::NAVSPG_DYNMODEL) == 3,
        "rail cut with V_BCKP is hot and keeps BBR");
  // Five hours later the ephemeris has expired: warm, or assisted with ANA.
  backed.setPower(false, 40000);
  backed.setPower(true, 40000 + 5u * 3600u * 1000u);
  check(std::string(backed.startName()) == "warm", "stale ephemeris is a warm start");
  send(backed,
       Ubx::valSet(Ubx::LAYER_RAM | Ubx::LAYER_BBR,
                   {
                       {Ubx::Key::ANA_USE_ANA, 1}
  }),
       40000 + 5u * 3600u * 1000u + 10);
  const uint32_t later = 40000 + 5u * 3600u * 1000u + 30000;
  backed.epoch(later, 2026, 9, 6, 0);
  backed.setPower(false, later + 1);
  backed.setPower(true, later + 5u * 3600u * 1000u);
  check(std::string(backed.startName()) == "assisted", "AssistNow Autonomous shortens it");

  // UPD-SOS without a flash is not acknowledged; with one it restores.
  VirtualM10 plain;
  plain.reset({}, 0);
  Ubx::Sos sos = {};
  auto reply = frames(send(plain, Ubx::sosCommand(Ubx::SOS_CREATE), 10));
  check(reply.size() == 1 && Ubx::parseSos(reply[0].data() + 6, 8, sos) && sos.cmd == Ubx::SOS_ACK
            && sos.response == 0,
        "SOS create without flash is not acknowledged");
  reply = frames(send(plain, Ubx::sosPoll(), 20));
  check(reply.size() == 1 && Ubx::parseSos(reply[0].data() + 6, 8, sos)
            && sos.response == Ubx::SOS_RESTORE_NONE,
        "SOS poll reports no backup");

  VirtualM10 flash;
  flash.reset({false, true, "MAX-M10S", false, 0}, 0);
  send(flash,
       Ubx::valSet(Ubx::LAYER_RAM | Ubx::LAYER_BBR,
                   {
                       {Ubx::Key::NAVSPG_DYNMODEL, 2}
  }),
       10);
  reply = frames(send(flash, Ubx::sosCommand(Ubx::SOS_CREATE), 20));
  check(reply.size() == 1 && Ubx::parseSos(reply[0].data() + 6, 8, sos) && sos.response == 1,
        "SOS create with flash is acknowledged");
  flash.setPower(false, 30);
  flash.setPower(true, 40);
  reply = frames(send(flash, Ubx::sosPoll(), 50));
  check(reply.size() == 1 && Ubx::parseSos(reply[0].data() + 6, 8, sos)
            && sos.response == Ubx::SOS_RESTORE_OK && flash.value(Ubx::Key::NAVSPG_DYNMODEL) == 2,
        "SOS restores BBR on the next start");

  // CFG-RST is never acknowledged and sets the start type.
  check(frames(send(plain, Ubx::cfgRst(Ubx::RST_WARM, Ubx::RST_MODE_GNSS_ONLY), 100)).empty()
            && std::string(plain.startName()) == "warm" && plain.resets == 1,
        "CFG-RST warm");
  send(plain, Ubx::cfgRst(Ubx::RST_HOT, Ubx::RST_MODE_GNSS_STOP), 200);
  check(plain.state() == VirtualM10::State::STOPPED && !plain.outputting(300), "GNSS stop");

  // PMREQ without the force flag does not enter standby.
  VirtualM10 standby;
  standby.reset({}, 0);
  send(standby, Ubx::pmReq({1000, Ubx::PMREQ_FLAG_BACKUP, 0}), 10);
  check(standby.state() == VirtualM10::State::ON, "PMREQ without force is ignored");
  // Duration 0 with UART wake: any byte wakes it.
  send(standby, Ubx::pmReq({0, Ubx::PMREQ_FLAG_FORCE, Ubx::PMREQ_WAKE_UARTRX}), 20);
  check(!standby.outputting(100000), "indefinite standby");
  const uint8_t wake = 0xff;
  standby.receive(&wake, 1, 100001);
  check(standby.outputting(100002), "a UART byte wakes it");

  // Current: cyclic tracking draws less than continuous, standby far less.
  VirtualM10 full;
  full.reset({true, false, "MAX-M10S", true, 0}, 0);
  VirtualM10 cyclic;
  cyclic.reset({true, false, "MAX-M10S", true, 0}, 0);
  send(cyclic,
       Ubx::valSet(Ubx::LAYER_RAM,
                   {
                       {Ubx::Key::PM_OPERATEMODE, 2}
  }),
       0);
  check(full.averageMicroamps(60000) == 9500, "continuous tracking 9.5 mA");
  check(cyclic.averageMicroamps(60000) == 5000, "cyclic tracking 5 mA");
  VirtualM10 mia;
  mia.reset({false, false, "MIA-M10Q", true, 0}, 0);
  check(mia.averageMicroamps(1000) == 10500, "MIA-M10Q continuous 10.5 mA");
}

void testEpoch() {
  VirtualM10 m10;
  m10.reset({false, false, "MAX-M10S", true, 0}, 0);
  check(m10.epoch(500, 2026, 9, 6, 45319).empty(), "no UBX output by default");
  send(m10,
       Ubx::valSet(Ubx::LAYER_RAM,
                   {
                       {Ubx::Key::MSGOUT_NAV_PVT_UART1, 1},
                       {Ubx::Key::MSGOUT_NAV_SAT_UART1, 1}
  }),
       600);
  const auto out = frames(m10.epoch(1500, 2026, 9, 6, 45319));
  Ubx::NavPvt pvt = {};
  std::vector<Ubx::NavSatSv> svs;
  check(out.size() == 2 && Ubx::parseNavPvt(out[0].data() + 6, out[0].size() - 8, pvt)
            && pvt.positionValid() && pvt.timeValid() && pvt.hour == 12 && pvt.min == 35
            && pvt.sec == 19,
        "NAV-PVT epoch");
  // Module default set: GPS 4, GAL 3, BDS 3, QZSS 1, SBAS 1.
  check(out.size() == 2 && Ubx::parseNavSat(out[1].data() + 6, out[1].size() - 8, svs)
            && svs.size() == 12,
        "NAV-SAT follows the enabled signals");
  // A poll answers with one copy even when the periodic output is off.
  VirtualM10 polled;
  polled.reset({false, false, "MAX-M10S", true, 0}, 0);
  const auto poll = frames(send(polled, Ubx::frame(Ubx::CLASS_NAV, Ubx::ID_NAV_SAT, {}), 2000));
  check(poll.size() == 1 && poll[0][3] == Ubx::ID_NAV_SAT, "NAV-SAT poll");
  check(polled.value(Ubx::Key::MSGOUT_NAV_SAT_UART1) == 0, "the poll left the rate alone");
}

}  // namespace

int main() {
  testDetect();
  testMonVerAndLayers();
  testValidity();
  testStartsAndPower();
  testEpoch();
  if (g_Failures != 0) {
    std::cout << "gps ubx m10 model: FAIL (" << g_Failures << " of " << g_Checks << " checks)\n";
    return 1;
  }
  std::cout << "gps ubx m10 model: PASS (" << g_Checks << " checks)\n";
  return 0;
}
