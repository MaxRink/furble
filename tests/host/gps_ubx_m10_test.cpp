// Host test for receiver detection (Ubx::Detect) and the simulator's virtual
// u-blox M10 (sim/ubx_m10.{h,cpp}). The virtual receiver owns no clock or UART,
// so the scenarios' state machine is exercised here byte for byte: VALSET
// validity rules, configuration layers, software standby, rail cuts with and
// without a backup supply, UPD-SOS, CFG-RST, and the current model.

#include <iostream>
#include <string>
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
  check(std::string(flash.startName()) == "warm", "a restored backup starts warm");

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

/** A virtual NEO-F10N: the dual-band firmware with no power save and no GLONASS. */
void resetDual(VirtualM10 &m10, uint32_t now) {
  VirtualM10::Options options;
  options.module = "NEO-F10N";
  options.bands = static_cast<uint8_t>(Ubx::BAND_L1 | Ubx::BAND_L5);
  m10.reset(options, now);
}

/**
 * The two variants of the virtual receiver.
 *
 * What this fixes, from the documents: the L1 firmware has no L5 signal key, so
 * a request for one is refused (M10 IFD UBX-21035062 R03, 4.9.21 page 152,
 * Table 42, and 3.10.5 for the refusal); the dual-band firmware has all four
 * (F10 IFD UBX-23002975 R02, 4.9.20 pages 158 and 159, Table 46) and refuses a
 * plan that splits the bands of one constellation (NEO-F10N integration manual
 * UBXDOC-963802114-12193 R04, 2.1.3 page 10); and it has neither a CFG-PM key
 * nor a GLONASS key (same interface description 4.9, and the module manual's
 * appendix migration table, page 64).
 */
void testDualBandVariant() {
  // MON-VER tells the two apart: protocol 34.10 against 40.00.
  VirtualM10 l1;
  l1.reset({}, 0);
  auto reply = frames(send(l1, Ubx::pollMonVer(), 10));
  Ubx::MonVer ver;
  check(reply.size() == 1 && Ubx::parseMonVer(reply[0].data() + 6, reply[0].size() - 8, ver)
            && ver.field("PROTVER") == "34.10" && ver.field("MOD") == "MAX-M10S",
        "the L1 variant reports protocol 34.10");
  VirtualM10 dual;
  resetDual(dual, 0);
  reply = frames(send(dual, Ubx::pollMonVer(), 10));
  check(reply.size() == 1 && Ubx::parseMonVer(reply[0].data() + 6, reply[0].size() - 8, ver)
            && ver.field("PROTVER") == "40.00" && ver.field("MOD") == "NEO-F10N",
        "the dual-band variant reports protocol 40.00");
  check(Ubx::capsFromVersion(ver).dualBand(), "MON-VER names the dual-band family");

  // The capability probe: the L1 variant refuses the four L5 keys, the
  // dual-band variant answers with one item each.
  std::vector<uint32_t> keys;
  for (const uint32_t key : Ubx::l5Keys()) {
    keys.push_back(key);
  }
  const auto poll = Ubx::valGet(Ubx::GET_DEFAULT, keys);
  check(acked(send(l1, poll, 20), false), "the L1 variant refuses the L5 capability poll");
  const auto answer = frames(send(dual, poll, 20));
  Ubx::ValGet get = {};
  check(!answer.empty() && Ubx::parseValGet(answer[0].data() + 6, answer[0].size() - 8, get)
            && get.items.size() == 4 && get.layer == Ubx::GET_DEFAULT,
        "the dual-band variant answers the L5 capability poll");

  // Each L5 key alone: refused on L1, accepted on the dual-band part.
  const uint32_t l5[] = {Ubx::Key::SIGNAL_GPS_L5, Ubx::Key::SIGNAL_GAL_E5A,
                         Ubx::Key::SIGNAL_BDS_B2A, Ubx::Key::SIGNAL_QZSS_L5};
  for (const uint32_t key : l5) {
    VirtualM10 fresh;
    fresh.reset({}, 0);
    check(acked(send(fresh,
                     Ubx::valSet(Ubx::LAYER_RAM,
                                 {
                                     {key, 1}
    }),
                     10),
                false),
          "an L1 receiver refuses an L5 signal key");
    VirtualM10 freshDual;
    resetDual(freshDual, 0);
    check(acked(send(freshDual,
                     Ubx::valSet(Ubx::LAYER_RAM,
                                 {
                                     {key, 1}
    }),
                     10),
                true),
          "a dual-band receiver accepts an L5 signal key");
    check(freshDual.value(key) == 1, "the dual-band receiver defaults the L5 key on");
  }

  // The documented exclusions, enforced as refusals: a plan that splits one
  // constellation's bands, B1I on a receiver that has no B1I, a GLONASS write
  // and any power save write.
  VirtualM10 plan;
  resetDual(plan, 0);
  check(acked(send(plan,
                   Ubx::valSet(Ubx::LAYER_RAM,
                               {
                                   {Ubx::Key::SIGNAL_GPS_L1CA, 1},
                                   {Ubx::Key::SIGNAL_GPS_L5,   0}
  }),
                   10),
              false),
        "splitting the two GPS bands is refused");
  check(acked(send(plan,
                   Ubx::valSet(Ubx::LAYER_RAM,
                               {
                                   {Ubx::Key::SIGNAL_BDS_B1, 1}
  }),
                   20),
              false),
        "BeiDou B1I is refused on a dual-band receiver");
  check(acked(send(plan,
                   Ubx::valSet(Ubx::LAYER_RAM,
                               {
                                   {Ubx::Key::SIGNAL_GLO_ENA, 1}
  }),
                   30),
              false),
        "GLONASS is refused on a dual-band receiver");
  check(acked(send(plan,
                   Ubx::valSet(Ubx::LAYER_RAM,
                               {
                                   {Ubx::Key::PM_OPERATEMODE, 2}
  }),
                   40),
              false),
        "power save is refused on a dual-band receiver");
  // The BandExclusion rule is not what refused those: the keys do not exist.
  check(!plan.hasBbr(Ubx::Key::SIGNAL_GLO_ENA), "the refused writes stored nothing");

  // A whole dual-band signal batch, as the driver builds it, is accepted and
  // reads back as written.
  const std::vector<Ubx::KeyValue> batch = {
      {Ubx::Key::SIGNAL_GPS_ENA,   1},
      {Ubx::Key::SIGNAL_GPS_L1CA,  1},
      {Ubx::Key::SIGNAL_GPS_L5,    1},
      {Ubx::Key::SIGNAL_GAL_ENA,   1},
      {Ubx::Key::SIGNAL_GAL_E1,    1},
      {Ubx::Key::SIGNAL_GAL_E5A,   1},
      {Ubx::Key::SIGNAL_BDS_ENA,   1},
      {Ubx::Key::SIGNAL_BDS_B1C,   1},
      {Ubx::Key::SIGNAL_BDS_B2A,   1},
      {Ubx::Key::SIGNAL_QZSS_ENA,  1},
      {Ubx::Key::SIGNAL_QZSS_L1CA, 1},
      {Ubx::Key::SIGNAL_QZSS_L5,   1},
      {Ubx::Key::SIGNAL_QZSS_L1S,  1},
      {Ubx::Key::SIGNAL_SBAS_ENA,  1},
      {Ubx::Key::SIGNAL_SBAS_L1CA, 1},
  };
  check(acked(send(plan, Ubx::valSet(Ubx::LAYER_RAM | Ubx::LAYER_BBR, batch), 50), true),
        "a dual-band signal batch is accepted");
  const auto readBack = frames(send(plan, Ubx::valGet(Ubx::GET_RAM, keys), 60));
  check(!readBack.empty() && Ubx::parseValGet(readBack[0].data() + 6, readBack[0].size() - 8, get)
            && get.items.size() == 4,
        "the L5 keys read back");

  // The published ceilings the model reports for each variant.
  check(l1.maxRateHz() == 3, "the L1 module default set publishes 3 Hz");
  check(plan.maxRateHz() == 10, "the dual-band default set publishes 10 Hz");
  VirtualM10 single;
  resetDual(single, 0);
  send(single,
       Ubx::valSet(Ubx::LAYER_RAM,
                   {
                       {Ubx::Key::SIGNAL_GAL_ENA,  0},
                       {Ubx::Key::SIGNAL_BDS_ENA,  0},
                       {Ubx::Key::SIGNAL_QZSS_ENA, 0},
                       {Ubx::Key::SIGNAL_SBAS_ENA, 0}
  }),
       70);
  check(single.maxRateHz() == 20, "one dual-band constellation publishes 20 Hz");

  // The model reads the data sheet tables independently of the codec. Compare
  // the two on every mask that names a major constellation, for both variants,
  // so a change to one table without the other fails here.
  const uint8_t masks[] = {
      Ubx::GNSS_MASK_GPS,
      Ubx::GNSS_MASK_GAL,
      Ubx::GNSS_MASK_BDS,
      Ubx::GNSS_MASK_GLO,
      static_cast<uint8_t>(Ubx::GNSS_MASK_GPS | Ubx::GNSS_MASK_GAL),
      static_cast<uint8_t>(Ubx::GNSS_MASK_GPS | Ubx::GNSS_MASK_GLO),
      static_cast<uint8_t>(Ubx::GNSS_MASK_GPS | Ubx::GNSS_MASK_BDS),
      static_cast<uint8_t>(Ubx::GNSS_MASK_GPS | Ubx::GNSS_MASK_GAL | Ubx::GNSS_MASK_GLO),
      static_cast<uint8_t>(Ubx::GNSS_MASK_GPS | Ubx::GNSS_MASK_GAL | Ubx::GNSS_MASK_BDS),
      static_cast<uint8_t>(Ubx::GNSS_MASK_GPS | Ubx::GNSS_MASK_BDS | Ubx::GNSS_MASK_GLO),
      static_cast<uint8_t>(Ubx::GNSS_MASK_GPS | Ubx::GNSS_MASK_GAL | Ubx::GNSS_MASK_BDS
                           | Ubx::GNSS_MASK_GLO),
  };
  for (const uint8_t mask : masks) {
    // L1: drive a receiver into the mask, then read its published ceiling.
    VirtualM10 table;
    std::vector<Ubx::KeyValue> items;
    for (uint8_t bit = 0; bit < 6; bit++) {
      const uint8_t value = (mask & (1u << bit)) != 0 ? 1 : 0;
      const uint32_t key = (bit == 0)   ? Ubx::Key::SIGNAL_GPS_ENA
                           : (bit == 1) ? Ubx::Key::SIGNAL_BDS_ENA
                           : (bit == 2) ? Ubx::Key::SIGNAL_GLO_ENA
                           : (bit == 3) ? Ubx::Key::SIGNAL_GAL_ENA
                           : (bit == 4) ? Ubx::Key::SIGNAL_QZSS_ENA
                                        : Ubx::Key::SIGNAL_SBAS_ENA;
      items.push_back({key, value});
    }
    if (((mask & Ubx::GNSS_MASK_BDS) != 0) && ((mask & Ubx::GNSS_MASK_GLO) != 0)) {
      // B1I cannot run beside GLONASS, so the receiver would refuse the batch
      // and the comparison would read the wrong state. Switch BeiDou to B1C the
      // way the plan does.
      items.push_back({Ubx::Key::SIGNAL_BDS_B1, 0});
      items.push_back({Ubx::Key::SIGNAL_BDS_B1C, 1});
    }
    table.reset({}, 0);
    send(table, Ubx::valSet(Ubx::LAYER_RAM, items), 10);
    check(Ubx::maxRateHz(mask, Ubx::CAPS_M10) == table.maxRateHz(),
          "the M10 codec ceiling matches the modelled data sheet table");
  }
  // Dual-band: single and multi-GNSS, on receivers whose masks the model can
  // enter without splitting a band pair.
  VirtualM10 dualSingle;
  resetDual(dualSingle, 0);
  send(dualSingle,
       Ubx::valSet(Ubx::LAYER_RAM,
                   {
                       {Ubx::Key::SIGNAL_GAL_ENA,  0},
                       {Ubx::Key::SIGNAL_BDS_ENA,  0},
                       {Ubx::Key::SIGNAL_QZSS_ENA, 0},
                       {Ubx::Key::SIGNAL_SBAS_ENA, 0}
  }),
       20);
  check(Ubx::maxRateHz(Ubx::GNSS_MASK_GPS, Ubx::CAPS_F10) == dualSingle.maxRateHz(),
        "the dual-band single-constellation ceiling matches the model");
  check(Ubx::maxRateHz(Ubx::GNSS_MASK_MODULE_DEFAULT, Ubx::CAPS_F10) == plan.maxRateHz(),
        "the dual-band multi-GNSS ceiling matches the model");

  // What the band costs: the dual-band part tracks at 21 mA where the L1 part
  // tracks at 9.5 mA, and neither figure is an L1-only measurement of the same
  // part, which the module cannot do (NEO-F10N data sheet UBX-23002117 R05,
  // section 1.3 page 5 and Table 15 page 12; MAX-M10S data sheet UBX-20035208
  // R08, Table 15 page 13).
  VirtualM10 l1Power;
  l1Power.reset({}, 0);
  VirtualM10 dualPower;
  resetDual(dualPower, 0);
  l1Power.epoch(30000, 2026, 9, 6, 0);
  dualPower.epoch(30000, 2026, 9, 6, 0);
  check(l1Power.averageMicroamps(120000) == 9500, "the L1 part tracks at 9.5 mA");
  check(dualPower.averageMicroamps(120000) == 21000, "the dual-band part tracks at 21 mA");
  check(!l1Power.fixed(26999) && l1Power.fixed(27000), "the L1 cold start is 27 s");
  check(!dualPower.fixed(27999) && dualPower.fixed(28000), "the dual-band cold start is 28 s");
}

void testB1cConfigOrder() {
  // A power save mode an earlier session stored in BBR is active when this
  // session starts. The receiver rejects a signal configuration that includes
  // BeiDou B1C while a power save mode is in use (RN 7.2 page 15), which the
  // model enforces, so the order of the two writes decides whether the
  // constellation change lands at all.
  constexpr uint8_t LAYERS = Ubx::LAYER_RAM | Ubx::LAYER_BBR;
  Ubx::Request request = {200, 3, 0x0f, Ubx::GNSS_MASK_ALL, Ubx::PSM_OFF, 10, true, Ubx::CAPS_M10};
  const Ubx::Plan plan = Ubx::plan(request);
  const std::vector<Ubx::KeyValue> psm_on = {
      {Ubx::Key::PM_OPERATEMODE, 2}
  };

  VirtualM10 signals_first;
  signals_first.reset({}, 0);
  check(acked(send(signals_first, Ubx::valSet(Ubx::LAYER_RAM, psm_on), 10), true),
        "a stored power save mode is active");
  check(acked(send(signals_first, Ubx::valSet(LAYERS, plan.signals), 20), false),
        "the signal write is refused while power save is on");
  check(acked(send(signals_first, Ubx::valSet(LAYERS, plan.power), 30), true),
        "leaving power save afterwards is accepted but too late");

  check(plan.power_before_signals, "the plan leaves power save before the signal write");
  VirtualM10 plan_order;
  plan_order.reset({}, 0);
  check(acked(send(plan_order, Ubx::valSet(Ubx::LAYER_RAM, psm_on), 10), true),
        "a stored power save mode is active");
  check(acked(send(plan_order, Ubx::valSet(LAYERS, plan.power), 20), true),
        "leaving power save is accepted first");
  check(acked(send(plan_order, Ubx::valSet(LAYERS, plan.signals), 30), true),
        "the signal write is then accepted");
  check(plan_order.value(Ubx::Key::PM_OPERATEMODE) == 0
            && plan_order.value(Ubx::Key::SIGNAL_BDS_B1C) == 1
            && plan_order.value(Ubx::Key::SIGNAL_GLO_ENA) == 1
            && plan_order.value(Ubx::Key::SIGNAL_BDS_B1) == 0,
        "the receiver ends on full power with B1C and GLONASS, B1I off");
}

}  // namespace

int main() {
  testDetect();
  testMonVerAndLayers();
  testValidity();
  testStartsAndPower();
  testEpoch();
  testDualBandVariant();
  testB1cConfigOrder();
  if (g_Failures != 0) {
    std::cout << "gps ubx m10 model: FAIL (" << g_Failures << " of " << g_Checks << " checks)\n";
    return 1;
  }
  std::cout << "gps ubx m10 model: PASS (" << g_Checks << " checks)\n";
  return 0;
}
