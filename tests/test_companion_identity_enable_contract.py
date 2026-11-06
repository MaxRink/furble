"""Conformance guard for the companion identity enable transition.

The production CompanionGatt is not linked into the host harness because the
host NimBLE double has no GATT server database. This test therefore parses the
production source and pins the contract that matters: the durable identity is
applied in both the boot path and every disabled->enabled transition, before
any advertising entry point.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "FurbleCompanion.cpp"


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        return ""
    open_brace = text.find("{", start)
    if open_brace < 0:
        return ""
    depth = 0
    for index in range(open_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace + 1:index]
    return ""


class CompanionIdentityEnableContractTest(unittest.TestCase):
    def setUp(self):
        self.text = strip_comments(SOURCE.read_text(encoding="utf-8"))

    def test_init_applies_the_persisted_identity(self):
        body = function_body(self.text, "void CompanionGatt::init(void)")
        self.assertIn("CompanionIdentity::applyPersisted()", body)

    def test_enable_applies_the_identity_before_advertising(self):
        body = function_body(self.text, "void CompanionGatt::enable(bool pairingWindow)")
        apply_at = body.find("CompanionIdentity::applyPersisted()")
        self.assertGreaterEqual(apply_at, 0, "enable() does not apply the persisted identity")
        enabled_at = body.find("m_Enabled = true;")
        self.assertGreaterEqual(enabled_at, 0)
        self.assertGreater(apply_at, enabled_at,
                           "the identity is applied before the disabled->enabled transition")
        for advertising in ("startPairingWindow(", "startReconnectAdvertising("):
            position = body.find(advertising)
            if position >= 0:
                self.assertLess(apply_at, position,
                                f"{advertising} runs before the identity is applied")

    def test_reidentify_rotates_before_dropping_the_link_or_bond(self):
        body = function_body(self.text, "bool CompanionGatt::reidentify(void)")
        rotate_at = body.find("CompanionIdentity::rotate()")
        self.assertGreaterEqual(rotate_at, 0, "reidentify does not rotate the identity")
        for destructive in ("stopAdvertising()", "m_Server->disconnect(", "forgetBond()"):
            position = body.find(destructive)
            self.assertGreaterEqual(position, 0, f"reidentify is missing {destructive}")
            self.assertLess(rotate_at, position,
                            f"{destructive} runs before identity rotation can fail")


if __name__ == "__main__":
    unittest.main()
