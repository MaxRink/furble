#ifndef FURBLE_GPS_UBX_H
#define FURBLE_GPS_UBX_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/*
 * Pure u-blox UBX protocol codec for the L1 and dual-band receivers.
 *
 * Free of ESP-IDF, NVS, the board support library and the UI toolkit, so the
 * host tests, the fuzz target and the simulator's virtual receiver link it
 * unchanged. Two interface descriptions are in play, and a comment naming a
 * section says which one it is:
 *
 * - "IFD" is the u-blox M10 SPG 5.10 Interface description, UBX-21035062 R03
 *   (protocol 34.10), the firmware MAX-M10S and MIA-M10Q run from ROM. It has
 *   no L5 signal key at all (IFD 4.9.21 page 152, Table 42).
 * - "F10-IFD" is the u-blox F10 SPG 6.00 Interface description, UBX-23002975
 *   R02 (protocol 40.00), the firmware the L1/L5 parts run. Its CFG-SIGNAL
 *   table carries the L5 keys (F10-IFD 4.9.20 pages 158 and 159, Table 46).
 *
 * Every message and key cites its section and page. plans/implemented/321-ublox-m10-driver.md
 * holds the M10 source list, plans/512-dual-band-l5.md the dual-band one.
 */

namespace Furble {
namespace Ubx {

/* Frame structure, IFD 3.2; checksum, IFD 3.4. */
constexpr uint8_t SYNC_1 = 0xB5;
constexpr uint8_t SYNC_2 = 0x62;
constexpr size_t HEADER_SIZE = 6;
constexpr size_t CHECKSUM_SIZE = 2;
constexpr size_t FRAME_OVERHEAD = HEADER_SIZE + CHECKSUM_SIZE;

/*
 * Largest payload furble accepts from the receiver. The protocol allows 65535;
 * the largest message furble asks for is NAV-SAT, 8 + 12 bytes per satellite,
 * which is 776 bytes at 64 tracked signals. A longer length field is noise and
 * resynchronises the stream rather than buffering it.
 */
constexpr uint16_t MAX_PAYLOAD = 1024;

/* Message classes and ids, IFD 3.8 "UBX messages overview". */
constexpr uint8_t CLASS_NAV = 0x01;
constexpr uint8_t CLASS_RXM = 0x02;
constexpr uint8_t CLASS_ACK = 0x05;
constexpr uint8_t CLASS_CFG = 0x06;
constexpr uint8_t CLASS_UPD = 0x09;
constexpr uint8_t CLASS_MON = 0x0A;

constexpr uint8_t ID_ACK_ACK = 0x01;    /* IFD 3.9.1 */
constexpr uint8_t ID_ACK_NAK = 0x00;    /* IFD 3.9.2 */
constexpr uint8_t ID_CFG_RST = 0x04;    /* IFD 3.10.2 */
constexpr uint8_t ID_CFG_VALDEL = 0x8C; /* IFD 3.10.3 */
constexpr uint8_t ID_CFG_VALGET = 0x8B; /* IFD 3.10.4 */
constexpr uint8_t ID_CFG_VALSET = 0x8A; /* IFD 3.10.5 */
constexpr uint8_t ID_MON_VER = 0x04;    /* IFD 3.14.9 */
constexpr uint8_t ID_NAV_PVT = 0x07;    /* IFD 3.15.11 */
constexpr uint8_t ID_NAV_SAT = 0x35;    /* IFD 3.15.13 */
constexpr uint8_t ID_RXM_PMREQ = 0x41;  /* IFD 3.16.6 */
constexpr uint8_t ID_UPD_SOS = 0x14;    /* IFD 3.19.1 */

/* Configuration layers, IFD 4.3 and the VALSET layers field, IFD 3.10.5. */
constexpr uint8_t LAYER_RAM = 0x01;
constexpr uint8_t LAYER_BBR = 0x02;
constexpr uint8_t LAYER_FLASH = 0x04;

/* CFG-VALGET layer selectors, which are codes rather than a bit mask. */
constexpr uint8_t GET_RAM = 0;
constexpr uint8_t GET_BBR = 1;
constexpr uint8_t GET_FLASH = 2;
constexpr uint8_t GET_DEFAULT = 7;

/* A CFG-VALSET, VALGET or VALDEL carries at most 64 keys, IFD 3.10.3 to 3.10.5. */
constexpr size_t MAX_KEYS = 64;

/*
 * Configuration keys, IFD 4.9 "Configuration reference". The value size is
 * encoded in bits 28 to 30 of the key itself (IFD 4.2), so the codec never
 * needs a type table. Defaults are the IFD appendix values for ROM SPG 5.10;
 * the modules ship a production preset on top (plan 321).
 */
namespace Key {
constexpr uint32_t RATE_MEAS = 0x30210001;            /* U2 ms, IFD 4.9.17, default 1000 */
constexpr uint32_t RATE_NAV = 0x30210002;             /* U2 cycles, default 1 */
constexpr uint32_t NAVSPG_DYNMODEL = 0x20110021;      /* E1, IFD 4.9.12, default 0 */
constexpr uint32_t SIGNAL_GPS_ENA = 0x1031001f;       /* L, IFD 4.9.21 */
constexpr uint32_t SIGNAL_GPS_L1CA = 0x10310001;      /* L */
constexpr uint32_t SIGNAL_GPS_L5 = 0x10310004;        /* L, F10-IFD 4.9.20 page 158 */
constexpr uint32_t SIGNAL_SBAS_ENA = 0x10310020;      /* L */
constexpr uint32_t SIGNAL_SBAS_L1CA = 0x10310005;     /* L */
constexpr uint32_t SIGNAL_GAL_ENA = 0x10310021;       /* L */
constexpr uint32_t SIGNAL_GAL_E1 = 0x10310007;        /* L */
constexpr uint32_t SIGNAL_GAL_E5A = 0x10310009;       /* L, F10-IFD 4.9.20 page 158 */
constexpr uint32_t SIGNAL_BDS_ENA = 0x10310022;       /* L */
constexpr uint32_t SIGNAL_BDS_B1 = 0x1031000d;        /* L, B1I */
constexpr uint32_t SIGNAL_BDS_B1C = 0x1031000f;       /* L */
constexpr uint32_t SIGNAL_BDS_B2A = 0x10310028;       /* L, F10-IFD 4.9.20 page 158 */
constexpr uint32_t SIGNAL_QZSS_ENA = 0x10310024;      /* L */
constexpr uint32_t SIGNAL_QZSS_L1CA = 0x10310012;     /* L */
constexpr uint32_t SIGNAL_QZSS_L1S = 0x10310014;      /* L */
constexpr uint32_t SIGNAL_QZSS_L5 = 0x10310017;       /* L, F10-IFD 4.9.20 page 159 */
constexpr uint32_t SIGNAL_GLO_ENA = 0x10310025;       /* L, IFD only */
constexpr uint32_t SIGNAL_GLO_L1 = 0x10310018;        /* L, IFD only */
constexpr uint32_t PM_OPERATEMODE = 0x20d00001;       /* E1, IFD 4.9.15, default FULL */
constexpr uint32_t PM_POSUPDATEPERIOD = 0x40d00002;   /* U4 s */
constexpr uint32_t PM_ACQPERIOD = 0x40d00003;         /* U4 s */
constexpr uint32_t PM_ONTIME = 0x30d00005;            /* U2 s */
constexpr uint32_t PM_DONOTENTEROFF = 0x10d00008;     /* L */
constexpr uint32_t PM_WAITTIMEFIX = 0x10d00009;       /* L */
constexpr uint32_t PM_UPDATEEPH = 0x10d0000a;         /* L */
constexpr uint32_t ANA_USE_ANA = 0x10230001;          /* L, IFD 4.9.1, default 0 */
constexpr uint32_t MSGOUT_NAV_PVT_UART1 = 0x20910007; /* U1, IFD 4.9.11, default 0 */
constexpr uint32_t MSGOUT_NAV_SAT_UART1 = 0x20910016; /* U1 */
constexpr uint32_t UART1OUTPROT_UBX = 0x10740001;     /* L, IFD 4.9.29, default 1 */
constexpr uint32_t UART1OUTPROT_NMEA = 0x10740002;    /* L */
}  // namespace Key

/** Byte count of a key's value, from its size field. 0 for a reserved size. */
size_t valueSize(uint32_t key);

/** One configuration item. Values wider than the key's size are truncated. */
struct KeyValue {
  uint32_t key;
  uint64_t value;
};

/** The 8-bit Fletcher checksum over class, id, length and payload, IFD 3.4. */
std::array<uint8_t, 2> checksum(const uint8_t *data, size_t length);

/** Frame a payload: B5 62 class id length payload ck_a ck_b. Empty if too long. */
std::vector<uint8_t> frame(uint8_t cls, uint8_t id, const std::vector<uint8_t> &payload);

/** What the head of a buffer that starts with B5 62 holds. */
enum class Scan : uint8_t {
  NEED_MORE, /**< a frame may still complete, wait for more bytes */
  BAD,       /**< not a frame here, drop one byte and resynchronise */
  FRAME,     /**< a whole, checksum-valid frame of `length` bytes */
};

struct ScanResult {
  Scan status;
  size_t length;
};

/** Classify a buffer whose first two bytes are B5 62, or a prefix of them. */
ScanResult scan(const uint8_t *data, size_t length);

/**
 * Byte-at-a-time frame splitter with a bounded buffer.
 *
 * Bytes before a sync pair, a length past MAX_PAYLOAD and a failed checksum all
 * resynchronise on the next sync pair inside the rejected bytes, so a frame
 * that starts inside garbage is still found.
 */
class Stream {
 public:
  /** Feed one byte. Returns true when frame() holds a complete valid frame. */
  bool push(uint8_t byte);

  /** The last complete frame, header and checksum included. */
  const std::vector<uint8_t> &frame(void) const { return m_Frame; }
  uint8_t cls(void) const { return m_Frame[2]; }
  uint8_t id(void) const { return m_Frame[3]; }
  const uint8_t *payload(void) const { return m_Frame.data() + HEADER_SIZE; }
  size_t payloadLength(void) const { return m_Frame.size() - FRAME_OVERHEAD; }

  /** Frames rejected on checksum or length since construction. */
  uint32_t rejected(void) const { return m_Rejected; }
  void clear(void);

 private:
  std::vector<uint8_t> m_Buffer;
  std::vector<uint8_t> m_Frame;
  uint32_t m_Rejected = 0;
};

/* ACK-ACK and ACK-NAK, IFD 3.9.1 and 3.9.2: clsID, msgID. */
struct Ack {
  uint8_t cls;
  uint8_t id;
  bool ack;
};
bool parseAck(uint8_t msg_id, const uint8_t *payload, size_t length, Ack &out);

/* NAV-PVT, IFD 3.15.11, 92 bytes. */
struct NavPvt {
  uint32_t iTOW;
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t min;
  uint8_t sec;
  uint8_t valid;   /**< bit0 validDate, bit1 validTime, bit2 fullyResolved */
  uint32_t tAcc;   /**< ns */
  int32_t nano;    /**< ns, -1e9 to 1e9 */
  uint8_t fixType; /**< 0 none, 1 DR, 2 2D, 3 3D, 4 GNSS+DR, 5 time only */
  uint8_t flags;   /**< bit0 gnssFixOK, bits 2..4 psmState */
  uint8_t flags2;
  uint8_t numSV;
  int32_t lon;     /**< 1e-7 deg */
  int32_t lat;     /**< 1e-7 deg */
  int32_t height;  /**< mm above the ellipsoid */
  int32_t hMSL;    /**< mm above mean sea level */
  uint32_t hAcc;   /**< mm */
  uint32_t vAcc;   /**< mm */
  int32_t gSpeed;  /**< mm/s */
  int32_t headMot; /**< 1e-5 deg */
  uint32_t sAcc;   /**< mm/s */
  uint16_t pDOP;   /**< 0.01 */
  uint16_t flags3; /**< bit0 invalidLlh */

  static constexpr size_t LENGTH = 92;

  bool validDate(void) const { return (valid & 0x01) != 0; }
  bool validTime(void) const { return (valid & 0x02) != 0; }
  bool fullyResolved(void) const { return (valid & 0x04) != 0; }
  bool gnssFixOk(void) const { return (flags & 0x01) != 0; }
  uint8_t psmState(void) const { return (flags >> 2) & 0x07; }
  bool invalidLlh(void) const { return (flags3 & 0x01) != 0; }

  /** A 2D, 3D or GNSS plus dead reckoning fix the receiver vouches for. */
  bool positionValid(void) const;
  /** validDate and validTime with in-range fields: the date and time are usable. */
  bool dateTimeValid(void) const;
  /** dateTimeValid and fullyResolved: safe to set a clock from (IFD 3.15.11). */
  bool timeValid(void) const;
};
bool parseNavPvt(const uint8_t *payload, size_t length, NavPvt &out);
/**
 * Encode, for the simulator receiver and the round-trip tests. The receiver
 * encoders return an empty frame for a payload above MAX_PAYLOAD, so every
 * frame they produce passes scan(). NAV-SAT holds at most 84 satellites.
 */
std::vector<uint8_t> encodeNavPvt(const NavPvt &pvt);

/* NAV-SAT, IFD 3.15.13: 8 byte header, 12 bytes per signal. gnssId per IFD 1.5. */
constexpr uint8_t GNSS_GPS = 0;
constexpr uint8_t GNSS_SBAS = 1;
constexpr uint8_t GNSS_GALILEO = 2;
constexpr uint8_t GNSS_BEIDOU = 3;
constexpr uint8_t GNSS_QZSS = 5;
constexpr uint8_t GNSS_GLONASS = 6;

struct NavSatSv {
  uint8_t gnssId;
  uint8_t svId;
  uint8_t cno;    /**< dBHz */
  int8_t elev;    /**< deg */
  int16_t azim;   /**< deg */
  uint32_t flags; /**< bits 0..2 qualityInd, bit3 svUsed */

  bool used(void) const { return (flags & 0x08) != 0; }
};
bool parseNavSat(const uint8_t *payload, size_t length, std::vector<NavSatSv> &out);
std::vector<uint8_t> encodeNavSat(uint32_t iTOW, const std::vector<NavSatSv> &svs);

/* MON-VER, IFD 3.14.9: swVersion[30], hwVersion[10], extension[30] x N. */
struct MonVer {
  std::string sw;
  std::string hw;
  std::vector<std::string> extensions;

  /** Value of an extension "KEY=value", or empty. For example "FWVER". */
  std::string field(const char *key) const;
};
bool parseMonVer(const uint8_t *payload, size_t length, MonVer &out);
std::vector<uint8_t> encodeMonVer(const MonVer &ver);

/*
 * UPD-SOS, IFD 3.19.1. The backup lives in the flash file system, so it needs
 * an external flash. Neither MAX-M10S nor MIA-M10Q has one, so on those the
 * create command is expected to be refused; the driver records that.
 */
constexpr uint8_t SOS_CREATE = 0;   /**< command: create backup */
constexpr uint8_t SOS_CLEAR = 1;    /**< command: clear backup */
constexpr uint8_t SOS_ACK = 2;      /**< output: backup creation acknowledge */
constexpr uint8_t SOS_RESTORED = 3; /**< output: system restored from backup */

/** SOS_RESTORED response values. */
constexpr uint8_t SOS_RESTORE_UNKNOWN = 0;
constexpr uint8_t SOS_RESTORE_FAILED = 1;
constexpr uint8_t SOS_RESTORE_OK = 2;
constexpr uint8_t SOS_RESTORE_NONE = 3;

struct Sos {
  uint8_t cmd;
  uint8_t response;
};
/** Decode an 8-byte SOS_ACK or SOS_RESTORED output message. */
bool parseSos(const uint8_t *payload, size_t length, Sos &out);

/* CFG-VALGET response, IFD 3.10.4.2: version 1, layer, position, cfgData. */
struct ValGet {
  uint8_t layer;
  uint16_t position;
  std::vector<KeyValue> items;
};
bool parseValGet(const uint8_t *payload, size_t length, ValGet &out);

/* Request builders. Each returns a complete frame, empty when inputs are invalid. */
std::vector<uint8_t> pollMonVer(void);
std::vector<uint8_t> valSet(uint8_t layers, const std::vector<KeyValue> &items);
std::vector<uint8_t> valGet(uint8_t layer, const std::vector<uint32_t> &keys);
std::vector<uint8_t> valDel(uint8_t layers, const std::vector<uint32_t> &keys);
std::vector<uint8_t> sosCommand(uint8_t command);
/** Empty UPD-SOS poll; the receiver answers with SOS_RESTORED. */
std::vector<uint8_t> sosPoll(void);
/** Encode an 8-byte SOS output message, for the simulator receiver. */
std::vector<uint8_t> sosOutput(uint8_t command, uint8_t response);
std::vector<uint8_t> valGetResponse(uint8_t layer, const std::vector<KeyValue> &items);

/*
 * RXM-PMREQ, IFD 3.16.6. M10 documents only the 16-byte version 0 form. The
 * integration manual (MAX-M10S UBX-20053088 3.6.3.2) requires the force flag
 * for software standby and notes that standby clears the RAM configuration.
 */
constexpr uint32_t PMREQ_FLAG_BACKUP = 0x02;
constexpr uint32_t PMREQ_FLAG_FORCE = 0x04;
constexpr uint32_t PMREQ_WAKE_UARTRX = 0x08;
constexpr uint32_t PMREQ_WAKE_EXTINT0 = 0x20;
constexpr uint32_t PMREQ_WAKE_EXTINT1 = 0x40;
constexpr uint32_t PMREQ_WAKE_SPICS = 0x80;

struct PmReq {
  uint32_t duration_ms; /**< 0 sleeps until a wake-up source fires */
  uint32_t flags;
  uint32_t wakeup_sources;
};
std::vector<uint8_t> pmReq(const PmReq &request);
bool parsePmReq(const uint8_t *payload, size_t length, PmReq &out);

/** Split a VALSET or VALDEL request payload, for the simulator receiver. */
bool parseValSet(const uint8_t *payload,
                 size_t length,
                 uint8_t &layers,
                 std::vector<KeyValue> &items);
bool parseKeyList(const uint8_t *payload,
                  size_t length,
                  uint8_t &layer,
                  std::vector<uint32_t> &keys);

/*
 * CFG-RST, IFD 3.10.2. Never acknowledged. navBbrMask 0x0000 hot, 0x0001 warm,
 * 0xFFFF cold start.
 */
constexpr uint16_t RST_HOT = 0x0000;
constexpr uint16_t RST_WARM = 0x0001;
constexpr uint16_t RST_COLD = 0xFFFF;
constexpr uint8_t RST_MODE_GNSS_ONLY = 0x02; /**< controlled software reset, GNSS only */
constexpr uint8_t RST_MODE_GNSS_STOP = 0x08; /**< controlled GNSS stop */
constexpr uint8_t RST_MODE_GNSS_START = 0x09;
std::vector<uint8_t> cfgRst(uint16_t nav_bbr_mask, uint8_t reset_mode);

/**
 * A one-shot "not before" deadline that stays correct across the 32-bit tick
 * wrap. An unarmed gate is open, so no sentinel tick value can close it: a
 * plain `deadline = 0` compared with a signed difference reads as the future
 * for every tick in [2^31, 2^32), which stalled every configuration command
 * between 24.8 and 49.7 days of uptime.
 */
class Settle {
 public:
  void arm(uint32_t now, uint32_t ms) {
    m_Until = now + ms;
    m_Armed = true;
  }
  void clear(void) { m_Armed = false; }
  bool armed(void) const { return m_Armed; }
  /** Has the deadline passed? Disarms itself once it has. */
  bool open(uint32_t now) {
    if (m_Armed && (static_cast<int32_t>(now - m_Until) >= 0)) {
      m_Armed = false;
    }
    return !m_Armed;
  }
  /** Milliseconds left, 0 when open. */
  uint32_t remaining(uint32_t now) const {
    const int32_t left = static_cast<int32_t>(m_Until - now);
    return (m_Armed && (left > 0)) ? static_cast<uint32_t>(left) : 0;
  }

 private:
  uint32_t m_Until = 0;
  bool m_Armed = false;
};

/**
 * Receiver identification by MON-VER poll.
 *
 * A u-blox receiver answers MON-VER whatever its configuration. A receiver
 * that is not u-blox ignores the frame, so after ATTEMPTS polls without an
 * answer the receiver is something else and keeps the NMEA path. One attempt
 * waits ATTEMPT_MS: the answer is about 190 bytes queued behind up to a second
 * of NMEA at 9600 baud, and acknowledgements come "at least within one
 * second" (IFD 3.9.1, 3.9.2).
 */
/*
 * Constellation mask, shared by the GPS_GNSS setting. The low three bits are
 * the CASIC navSystem bits the GPS_CONSTEL setting already uses, so a CASIC
 * selection maps across unchanged.
 */
constexpr uint8_t GNSS_MASK_GPS = 0x01;
constexpr uint8_t GNSS_MASK_BDS = 0x02;
constexpr uint8_t GNSS_MASK_GLO = 0x04;
constexpr uint8_t GNSS_MASK_GAL = 0x08;
constexpr uint8_t GNSS_MASK_QZSS = 0x10;
constexpr uint8_t GNSS_MASK_SBAS = 0x20;
constexpr uint8_t GNSS_MASK_ALL = 0x3f;
/** The modules' production preset: GPS, Galileo, BeiDou B1I, QZSS, SBAS (DS 1.3). */
constexpr uint8_t GNSS_MASK_MODULE_DEFAULT =
    GNSS_MASK_GPS | GNSS_MASK_GAL | GNSS_MASK_BDS | GNSS_MASK_QZSS | GNSS_MASK_SBAS;

/** Power save setting values, as stored in GPS_PSM. */
constexpr uint8_t PSM_OFF = 0;
constexpr uint8_t PSM_CYCLIC = 1;
constexpr uint8_t PSM_ON_OFF = 2;

/*
 * Signal bands. A band belongs to the receiver firmware, not to a setting.
 *
 * The M10 firmware implements L1 signals only: its CFG-SIGNAL table stops at
 * GLONASS L1 and has no L5 key (IFD 4.9.21 page 152, Table 42). The F10
 * firmware implements GPS L5, Galileo E5a, BeiDou B2a and QZSS L5 (F10-IFD
 * 4.9.20 pages 158 to 159, Table 46). Within a dual-band firmware the two
 * bands of one constellation cannot be enabled apart: both are enabled or
 * disabled together, and an unsupported combination is answered with
 * UBX-ACK-NAK (NEO-F10N integration manual UBXDOC-963802114-12193 R04, 2.1.3
 * page 10 and the data sheet UBX-23002117 R05, 1.3 page 4).
 */
constexpr uint8_t BAND_L1 = 0x01;
constexpr uint8_t BAND_L5 = 0x02;

/** Firmware families furble can name. An unnamed part stays at L1. */
constexpr uint8_t FAMILY_UNKNOWN = 0;
constexpr uint8_t FAMILY_M10 = 1; /**< SPG 5.10, protocol 34.10, L1 only */
constexpr uint8_t FAMILY_F10 = 2; /**< SPG 6.00, protocol 40.00, L1 and L5 */

/**
 * What a receiver can do, as far as furble can tell.
 *
 * The defaults refuse everything beyond L1, so a receiver furble has not
 * identified never receives a key it may not implement. capsFromVersion()
 * reads the MON-VER hint and CapsProbe confirms it by asking the receiver.
 */
struct Caps {
  uint8_t bands;  /**< BAND_* bits the firmware implements */
  bool psm;       /**< power save modes: CFG-PM-OPERATEMODE and PSMOO/PSMCT */
  bool glo;       /**< GLONASS signals at all */
  uint8_t family; /**< FAMILY_* */

  bool dualBand(void) const { return (bands & BAND_L5) != 0; }
};

/**
 * Nothing beyond what furble always assumed.
 *
 * A receiver furble cannot name keeps the L1 signals, power save and GLONASS
 * keys the driver has always written; only the L5 band is withheld, because
 * that is the one capability this model adds and it needs positive evidence.
 * A signal VALSET carrying an unknown key is refused in full, so guessing it
 * costs the whole batch.
 */
constexpr Caps CAPS_UNKNOWN = {BAND_L1, true, true, FAMILY_UNKNOWN};

/** The M10 preset: L1 only, power save, GLONASS (IFD 4.9.21 page 152). */
constexpr Caps CAPS_M10 = {BAND_L1, true, true, FAMILY_M10};

/**
 * The F10 preset: L1 and L5, no power save, no GLONASS.
 *
 * The F10 interface description has no CFG-PM group at all, and the module
 * manual states that power save is not supported while hardware backup and
 * software standby are (NEO-F10N integration manual UBXDOC-963802114-12193
 * R04, appendix migration table, page 64). Its CFG-SIGNAL table lists no GLONASS key
 * (F10-IFD 4.9.20 pages 158 and 159).
 */
constexpr Caps CAPS_F10 = {BAND_L1 | BAND_L5, false, false, FAMILY_F10};

/** Capabilities named by a MON-VER answer. A hint until CapsProbe confirms. */
Caps capsFromVersion(const MonVer &ver);

/** The four L5 signal keys the capability probe asks for. */
const std::array<uint32_t, 4> &l5Keys(void);

/** Is this one of the four L5 signal keys? */
bool isL5Key(uint32_t key);

/** What the user asked for, in furble's setting terms. */
struct Request {
  uint16_t rate_ms;  /**< 0 leaves the receiver rate alone */
  uint8_t dyn_model; /**< CFG-NAVSPG-DYNMODEL value, 0xff leaves it alone */
  uint8_t gnss;      /**< GNSS_MASK_* bits */
  uint8_t gnss_set;  /**< which GNSS_MASK_* bits gnss decides; 0 leaves all alone */
  uint8_t psm;       /**< PSM_* */
  uint32_t period_s; /**< PSMOO update period */
  bool ana;          /**< AssistNow Autonomous */
  /** What the receiver can do, from the capability probe. */
  Caps caps;
};

/** What a plan decided; a driver keeps only this part. */
struct PlanSummary {
  uint16_t rate_ms;  /**< the rate actually sent, 0 when left alone */
  uint8_t gnss;      /**< effective constellation mask */
  uint8_t psm;       /**< the power save mode actually sent */
  uint8_t bands;     /**< BAND_* actually written, 0 when no signal write happened */
  bool gnss_refused; /**< the mask broke a rule, signals left alone */
  bool psm_refused;  /**< power save with BeiDou B1C, left at full power */
  bool rate_clamped; /**< the rate exceeded the constellation limit */
  /**
   * Power save was asked for on a receiver that has none.
   *
   * The F10 firmware has no CFG-PM group and its module manual states that
   * power save is not supported (NEO-F10N integration manual
   * UBXDOC-963802114-12193 R04, appendix migration table, page 64), so neither the
   * mode nor the period key is written.
   */
  bool psm_unsupported;
  /**
   * BeiDou B1I was asked for on a dual-band receiver.
   *
   * The NEO-F10N data sheet lists B1C and B2a only (UBX-23002117 R05, Table 4
   * page 5), so the plan writes B1C and B2A and leaves B1I alone.
   */
  bool b1i_refused;
  /**
   * The receiver refused an L5 key after the capability probe passed.
   *
   * The driver sets this when a signal batch carrying an L5 key is NAKed, and
   * demotes the receiver to L1 for the rest of the session. It is reported so
   * the refusal is never silent: the L1 signals in that batch were refused with
   * it, because a VALSET applies as a whole (IFD 3.10.5).
   */
  bool l5_refused;
  /**
   * AssistNow Autonomous actually written.
   *
   * Cleared when the plan selects BeiDou B1C: the ROM release notes state that
   * AssistNow, like power save, is not supported with B1C (RN 5.1.5 page 8),
   * and the M10Q data sheet lists B1I rather than B1C for the service (DS-MIA
   * table 5 page 6).
   */
  bool ana;
  /**
   * Send the power-save write before the signal write.
   *
   * Set when the plan enables BeiDou B1C with power save off. A power save mode
   * that an earlier session stored is still in RAM, and the receiver rejects a
   * GNSS configuration that includes B1C while a power save mode is in use
   * (RN 7.2 page 15). Requests apply as they arrive (IFD 4.6 page 127), so
   * OPERATEMODE=FULL has to go first, before the signals batch.
   */
  bool power_before_signals;
};

/**
 * The configuration furble sends for a request, in send order.
 *
 * base goes first (NAV-PVT output, rate, dynamic model, AssistNow
 * Autonomous), then signals (a GNSS restart, so the receiver needs 0.5 s
 * before the next command, IFD 4.9.21), then power save with OPERATEMODE last
 * (IM-MAX 3.6.2.6). unset lists keys furble manages but this request leaves
 * alone; they are deleted from BBR so an earlier session's value does not
 * outlive the setting that wrote it.
 */
struct Plan: PlanSummary {
  std::vector<KeyValue> base;
  std::vector<KeyValue> signals;
  std::vector<KeyValue> power;
  std::vector<uint32_t> unset;
};

/** GPS_CONSTEL to a mask: GPS, BeiDou, GLONASS as asked, QZSS and SBAS follow GPS. */
uint8_t gnssFromConstellation(uint8_t constellation);

/** A mask the receiver accepts: one major GNSS, QZSS and SBAS only with GPS. */
bool gnssValid(uint8_t mask);

/**
 * gnssValid plus the receiver's own limits.
 *
 * A dual-band part has no GLONASS signal keys (F10-IFD 4.9.20 pages 158 and
 * 159, Table 46), so a mask that asks for GLONASS on one is refused rather
 * than written and NAKed.
 */
bool gnssValid(uint8_t mask, const Caps &caps);

/** Highest navigation rate for a mask at the default CPU clock (DS Table 2/3). */
uint8_t maxRateHz(uint8_t mask);

/**
 * Highest navigation rate for a mask on a receiver with these bands.
 *
 * An L1 receiver keeps the M10 figures (MAX-M10S data sheet UBX-20035208 R08,
 * Table 2 page 4). A dual-band receiver takes the F10 figures: 20 Hz for one
 * constellation and 10 Hz for more (NEO-F10N data sheet UBX-23002117 R05,
 * Table 2 and Table 3 page 4).
 */
uint8_t maxRateHz(uint8_t mask, const Caps &caps);

/** GPS_PLATFORM 1..4 to DYNMODEL: portable 0, stationary 2, pedestrian 3, automotive 4. */
uint8_t dynModelForPlatform(uint8_t platform);

Plan plan(const Request &request);

class Detect {
 public:
  static constexpr uint32_t ATTEMPT_MS = 1000;
  static constexpr uint8_t ATTEMPTS = 2;

  enum class Result : uint8_t {
    IDLE,    /**< not started */
    WAITING, /**< a poll is outstanding */
    UBX,     /**< MON-VER answered */
    OTHER,   /**< every poll went unanswered */
  };

  /** Start. The caller sends the first poll. */
  void begin(uint32_t now);
  /** A MON-VER answer arrived; valid in any state. */
  void answered(void) { m_Result = Result::UBX; }
  /** Advance the deadline. Sets poll when the caller must send another poll. */
  Result service(uint32_t now, bool &poll);

  Result result(void) const { return m_Result; }
  uint32_t deadline(void) const { return m_Deadline; }
  uint8_t attempts(void) const { return m_Attempts; }

 private:
  Result m_Result = Result::IDLE;
  uint8_t m_Attempts = 0;
  uint32_t m_Deadline = 0;
};

/**
 * Which bands a receiver implements, decided by asking it.
 *
 * A CFG-VALGET for the four L5 keys at the default layer is answered with a
 * CFG-VALGET response by a receiver whose firmware knows them, and refused
 * with UBX-ACK-NAK by one whose firmware does not: both interface
 * descriptions state that this message NAKs when any key is unknown to the
 * firmware (IFD 3.10.4 page 53, F10-IFD 3.10.4 pages 54 and 55). The poll
 * writes nothing and needs no reset.
 *
 * The result is L1 until a positive answer arrives. A receiver that stops
 * answering part way through leaves furble at L1, so no L5 key is ever
 * written to a receiver that has not advertised one. That is the whole point:
 * a signal VALSET carrying an unknown key is refused in full, which would
 * take the rate, dynamic model and the L1 signals down with it.
 */
class CapsProbe {
 public:
  static constexpr uint32_t ATTEMPT_MS = 1000;
  static constexpr uint8_t ATTEMPTS = 2;

  enum class Result : uint8_t {
    IDLE,    /**< not started */
    WAITING, /**< a poll is outstanding */
    L5,      /**< the receiver answered with the L5 keys */
    L1,      /**< the receiver refused them, or never answered */
  };

  /** Start. The caller sends the first CFG-VALGET. */
  void begin(uint32_t now);
  /** An answer arrived: the VALGET data, or the NAK that ended it. */
  void answered(bool l5);
  /** Advance the deadline. Sets poll when the caller must send another poll. */
  Result service(uint32_t now, bool &poll);

  Result result(void) const { return m_Result; }
  bool l5(void) const { return m_Result == Result::L5; }
  uint32_t deadline(void) const { return m_Deadline; }
  uint8_t attempts(void) const { return m_Attempts; }

 private:
  Result m_Result = Result::IDLE;
  uint8_t m_Attempts = 0;
  uint32_t m_Deadline = 0;
};

}  // namespace Ubx
}  // namespace Furble

#endif
