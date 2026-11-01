"""Guard the phone location contract for the companion apps."""

from pathlib import Path
import plistlib
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CompanionLocationContractTest(unittest.TestCase):
    def test_ios_declares_background_location(self):
        info = plistlib.loads(
            (ROOT / "companion/apple/AppResources/iOS-Info.plist").read_bytes()
        )
        modes = info.get("UIBackgroundModes", [])
        self.assertIn("bluetooth-central", modes)
        self.assertIn("location", modes)
        self.assertIn("NSLocationWhenInUseUsageDescription", info)
        self.assertIn("NSLocationAlwaysAndWhenInUseUsageDescription", info)

    def test_ios_provider_keeps_fixes_flowing_in_background(self):
        text = (
            ROOT
            / "companion/apple/Sources/FurbleCompanionCore/FurbleLocationProvider.swift"
        ).read_text(encoding="utf-8")
        self.assertIn("allowsBackgroundLocationUpdates = true", text)
        self.assertIn("showsBackgroundLocationIndicator = true", text)
        self.assertIn("pausesLocationUpdatesAutomatically = false", text)
        self.assertIn("requestWhenInUseAuthorization()", text)

    def test_ios_status_distinguishes_receiver_and_phone(self):
        text = (
            ROOT
            / "companion/apple/Sources/FurbleCompanionApp/FurbleCompanionApp.swift"
        ).read_text(encoding="utf-8")
        self.assertIn("gpsStatusLabel(status)", text)
        self.assertIn('return "Receiver, ', text)
        self.assertIn('return "Phone"', text)

    def test_android_declares_precise_location(self):
        manifest = (
            ROOT / "companion/android/app/src/main/AndroidManifest.xml"
        ).read_text(encoding="utf-8")
        self.assertIn("ACCESS_FINE_LOCATION", manifest)

    def test_reference_documents_the_phone_source_and_android_gap(self):
        text = (ROOT / "docs/companion.md").read_text(encoding="utf-8")
        self.assertIn("## Phone location", text)
        self.assertIn("gps_source", text)
        self.assertIn("foreground location service", text)


if __name__ == "__main__":
    unittest.main()
