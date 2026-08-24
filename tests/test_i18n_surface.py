#!/usr/bin/env python3
"""Static guard for untranslated QML labels and missing catalogue keys."""

import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
QML = ROOT / "src/gui/qml"


class I18nSurfaceTest(unittest.TestCase):
    def test_qml_user_facing_literals_are_catalogued(self):
        catalogue = json.loads((ROOT / "i18n/en.json").read_text(encoding="utf-8"))
        literal = re.compile(r"\b(label|text|title|hint|Accessible\.name)\s*:\s*\"([^\"]*)\"")
        translated = re.compile(r'T\.t\("([^\"]+)"\)')
        literals = []
        calls = 0
        for path in sorted(QML.glob("*.qml")):
            body = path.read_text(encoding="utf-8")
            literals.extend(
                f"{path.relative_to(ROOT)}:{match.start()}={match.group(2)!r}"
                for match in literal.finditer(body)
                if match.group(2)
            )
            calls += sum(1 for match in translated.finditer(body) if match.group(1) not in catalogue)
        self.assertEqual(literals, [])
        self.assertEqual(calls, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
