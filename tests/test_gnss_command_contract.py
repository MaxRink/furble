"""Guard the CASIC `$PCAS` commands furble writes against the vendor documents.

The three commands below are the ones the audit in
`plans/377-gnss-vendor-audit.md` found a rule for. The protocol revisions are
the CASIC multi-mode receiver specification V6.3.2 (2025-06-24) and V4.2.0.3
(2021-03-01), plus the u-blox M10 ROM 5.10 release notes.

- `$PCAS10,rs` is a receiver restart, not a working mode: 0 hot, 1 warm, 2 cold
  and 3 factory (V6.3.2 2.7.9, V4.2.0.3 1.6.8). Sending it to select a mode
  would reboot the receiver and lose the configuration writes that follow, so
  the driver must keep it inside `GPS::restart()`.
- `$PCAS03` carries the sentence rates. V6.3.2 2.7.4 documents fourteen rate
  arguments and V4.2.0.3 1.6.4 documents eighteen; the first fourteen are at
  the same positions in both, and an empty field keeps the receiver's setting.
  A short sentence therefore cannot turn a sentence off, so the body is built
  in one place that writes every field it means to change.
- `$PCAS04,mode` is the constellation. V4.2.0.3 1.6.5 defines a decimal mode 1
  to 7 and V6.3.2 2.7.5 defines a hexadecimal band bitmask. The two agree for
  1 to 7 because GPS, BeiDou and GLONASS sit in bits 0, 1 and 2 in that order,
  which is the only reason one value can serve both.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "src/FurbleGPS.cpp"
DRIVER_HEADER = ROOT / "include/FurbleGPS.h"
CASIC = ROOT / "lib/furble/protocol/GpsCasic.cpp"


def pcas_sentences(text):
    """Every `$PCAS` sentence body this source writes, as a string literal."""
    return re.findall(r'"(PCAS[0-9]{2},[^"]*)"', text)


def sentence_rate_tables(text):
    """Every `$PCAS03` rate table in the codec, as a list of field values."""
    tables = []
    for match in re.finditer(r"SentenceRates\s+\w+\s*=\s*\{([^}]*)\}", text):
        fields = [field.strip() for field in match.group(1).split(",") if field.strip()]
        tables.append(fields)
    return tables


class GnssCommandContractTest(unittest.TestCase):
    def test_pcas10_is_only_a_restart(self):
        """A `$PCAS10` write must sit in the restart path, never a config path."""
        lines = DRIVER.read_text(encoding="utf-8").splitlines()
        writes = [number for number, line in enumerate(lines) if '"PCAS10' in line]
        self.assertTrue(writes, "the driver no longer restarts a CASIC receiver")
        for number in writes:
            function = ""
            for line in reversed(lines[: number + 1]):
                match = re.match(r"\w[\w:<>,*& ]*GPS::(\w+)\(", line)
                if match:
                    function = match.group(1)
                    break
            self.assertEqual(
                function,
                "restart",
                f"src/FurbleGPS.cpp:{number + 1} sends $PCAS10 outside GPS::restart(); "
                "it is a receiver restart (V6.3.2 2.7.9), not a working mode",
            )

    def test_pcas03_bodies_come_from_the_documented_builder(self):
        """No hand-written `$PCAS03` in the driver: the field count is load bearing."""
        bodies = pcas_sentences(DRIVER.read_text(encoding="utf-8"))
        bodies = [body for body in bodies if body.startswith("PCAS03")]
        self.assertEqual(
            bodies,
            [],
            "src/FurbleGPS.cpp writes $PCAS03 by hand; build every form through "
            "Casic::sentenceCommand() so all fourteen documented rate fields are sent",
        )

    def test_pcas03_sends_every_documented_rate_field(self):
        """V6.3.2 2.7.4 has fourteen rate arguments; an empty field keeps its rate."""
        codec = CASIC.read_text(encoding="utf-8")
        self.assertIn("sentenceCommand(SentenceSet set)", codec, "the codec builds $PCAS03")
        widths = re.findall(r"std::array<int8_t, (\d+)>", codec)
        self.assertEqual(widths, ["14"], "one rate field per documented argument")
        tables = sentence_rate_tables(codec)
        self.assertEqual(len(tables), 3, "prune, satellite and standard forms")
        for fields in tables:
            self.assertEqual(len(fields), 14, f"{fields}: fourteen rate fields")
            for field in fields:
                self.assertIn(field, ("0", "1", "KEEP"), f"{fields}: a rate or the keep marker")

    def test_pcas04_stays_in_the_shared_mode_range(self):
        """1 to 7 are the only values a decimal-mode and a bitmask receiver share."""
        header = DRIVER_HEADER.read_text(encoding="utf-8")
        match = re.search(r"CONSTELLATION_MAX = (\d+);", header)
        self.assertIsNotNone(match, "GPS::CONSTELLATION_MAX is the constellation bound")
        self.assertEqual(
            int(match.group(1)),
            7,
            "a value above 7 means a different constellation on V6.3.2 (hex bitmask) "
            "than on V4.2.0.3 (decimal mode), so the setting cannot use it",
        )
        driver = DRIVER.read_text(encoding="utf-8")
        self.assertNotRegex(
            driver,
            r'"PCAS04,\d',
            "the constellation must come from the setting, not a literal",
        )


if __name__ == "__main__":
    unittest.main()
