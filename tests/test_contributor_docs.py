from pathlib import Path
import tempfile
import unittest

from scripts.check_contributor_docs import Check


class ContributorDocsTests(unittest.TestCase):
    def test_repository_handbook(self):
        self.assertEqual(Check(), [])

    def test_links_index_and_code_examples(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            handbook = root / "docs/private"
            handbook.mkdir(parents=True)
            (root / "CONTRIBUTING.md").write_text("[Start](docs/private/README.md)")
            index = handbook / "README.md"
            index.write_text("[Guide](guide.md)\n[Online](https://example.com)\n")
            guide = handbook / "guide.md"
            guide.write_text("```cpp\n[](int value) {}\n```\n")
            self.assertEqual(Check(root), [])
            guide.write_text("[Missing](removed.md)\n")
            self.assertTrue(any("missing removed.md" in error for error in Check(root)))
            guide.write_text("")
            index.write_text("")
            self.assertTrue(any("index is missing" in error for error in Check(root)))


if __name__ == "__main__":
    unittest.main()
