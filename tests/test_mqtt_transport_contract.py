"""Guard the MQTT transport contract encoded in every MQTT sdkconfig.

The firmware supports mqtt:// and mqtts://. Every board that compiles MQTT must
therefore enable the SSL transport and MQTT 3.1.1. The WebSocket transport is
deliberately not part of the contract, because the URI validator and the
secure-worker selection no longer accept wss://.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
MQTT_SDKCONFIGS = (
    "sdkconfig.m5stick-s3",
    "sdkconfig.m5stack-core2",
    "sdkconfig.esp32-s3-headless",
    "sdkconfig.waveshare-s3-eth",
)


class MqttTransportContractTest(unittest.TestCase):
    def test_every_mqtt_build_enables_ssl_and_311(self):
        for name in MQTT_SDKCONFIGS:
            text = (ROOT / name).read_text(encoding="utf-8")
            self.assertIn("CONFIG_MQTT_PROTOCOL_311=y", text, name)
            self.assertIn("CONFIG_MQTT_TRANSPORT_SSL=y", text, name)
            self.assertNotIn("CONFIG_MQTT_TRANSPORT_WEBSOCKET=y", text, name)

    def test_uri_validator_rejects_websocket_schemes(self):
        text = (ROOT / "src" / "FurbleNetworkSettings.cpp").read_text(encoding="utf-8")
        self.assertNotIn('"wss://"', text)
        self.assertNotIn('"ws://"', text)


if __name__ == "__main__":
    unittest.main()
