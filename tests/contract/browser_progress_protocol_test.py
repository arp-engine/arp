import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "scripts"))
from browser_contract_runner import BrowserProgressJournal


class BrowserProgressProtocolTest(unittest.TestCase):
    def test_ordered_receipts_and_final_equality(self):
        journal = BrowserProgressJournal()
        self.assertEqual(journal.accept({"contractVersion": 1, "first": 0, "messages": ["a", "😀"]}, 2),
                         {"contractVersion": 1, "acceptedThrough": 2})
        self.assertEqual(journal.code_units, 3)
        self.assertTrue(journal.matches(["a", "😀"]))
        self.assertFalse(journal.matches(["😀", "a"]))
        self.assertFalse(journal.matches(["a"]))
        self.assertEqual(journal.observations[-1], {"message": "😀", "elapsedSeconds": 2})

    def test_rejected_batch_never_partially_changes_progress(self):
        journal = BrowserProgressJournal()
        for batch in [{"contractVersion": 0, "first": 0, "messages": ["a"]},
                      {"contractVersion": True, "first": 0, "messages": ["a"]},
                      {"contractVersion": 1, "first": 1, "messages": ["a"]},
                      {"contractVersion": 1, "first": False, "messages": ["a"]},
                      {"contractVersion": 1, "first": 0, "messages": []},
                      {"contractVersion": 1, "first": 0, "messages": ["a", 1]},
                      {"contractVersion": 1, "first": 0, "messages": ["a"] * 129},
                      {"contractVersion": 1, "first": 0, "messages": ["a", "x" * 65537]}]:
            with self.assertRaises(ValueError):
                journal.accept(batch, 1)
            self.assertEqual(journal.messages, [])
            self.assertEqual(journal.observations, [])
            self.assertEqual(journal.code_units, 0)

    def test_total_limits_and_duplicate_receipts(self):
        journal = BrowserProgressJournal()
        journal.limits = dict(journal.limits, maxMessages=3, maxCodeUnits=3)
        journal.accept({"contractVersion": 1, "first": 0, "messages": ["ab"]}, 1)
        for first, messages in [(0, ["x"]), (1, ["xy"]), (1, ["", "", ""])]:
            with self.assertRaises(ValueError):
                journal.accept({"contractVersion": 1, "first": first, "messages": messages}, 2)
            self.assertTrue(journal.matches(["ab"]))


if __name__ == "__main__":
    unittest.main()
