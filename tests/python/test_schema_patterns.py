"""Regression for A045: JSON Schema pattern search must reject a final newline."""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SchemaPatternTests(unittest.TestCase):
    def test_identifiers_and_hashes_reject_trailing_line_breaks(self):
        specimens = json.loads((ROOT / "schemas/specimen.schema.json").read_text())
        sessions = json.loads((ROOT / "schemas/measurement-session.schema.json").read_text())
        components = json.loads((ROOT / "schemas/components.schema.json").read_text())
        cases = [
            (specimens["properties"]["profile_id"]["pattern"], "stage-profile"),
            (sessions["properties"]["session_id"]["pattern"], "session-v1"),
            (components["properties"]["components"]["items"]["properties"]["id"]["pattern"], "COMPONENT-1"),
            (sessions["properties"]["artifacts"]["items"]["properties"]["sha256"]["pattern"], "a" * 64),
        ]
        for pattern, valid in cases:
            with self.subTest(value=valid):
                self.assertIsNotNone(re.search(pattern, valid))
                for ending in ("\n", "\r", "\r\n"):
                    self.assertIsNone(re.search(pattern, valid + ending))


if __name__ == "__main__":
    unittest.main()
