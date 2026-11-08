"""Validate contributor links and make every internal guide discoverable."""
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]


def Check(root=ROOT):
    root = Path(root).resolve()
    handbook = root / "docs/private"
    guides = set(handbook.rglob("*.md"))
    errors = []
    indexed = set()
    for source in sorted(guides | {root / "CONTRIBUTING.md"}):
        text = source.read_text(encoding="utf-8")
        text = re.sub(r"(?ms)^(`{3,}|~{3,})[^\n]*\n.*?^\1[^\n]*", "", text)
        for target in re.findall(r"\[[^\]]*\]\(([^)]+)\)", text):
            url = urlsplit(target.strip("<>"))
            if url.scheme or url.netloc or not url.path:
                continue
            path = (source.parent / unquote(url.path)).resolve()
            if not path.exists():
                errors.append(f"{source.relative_to(root)} links to missing {target}")
            if source == handbook / "README.md":
                indexed.add(path)
    for guide in sorted(guides - indexed - {handbook / "README.md"}):
        errors.append(f"Handbook index is missing {guide.relative_to(root)}")
    return errors


if __name__ == "__main__":
    errors = Check()
    if errors:
        raise SystemExit("\n".join(errors))
    print("Contributor documentation links and index checked.")
