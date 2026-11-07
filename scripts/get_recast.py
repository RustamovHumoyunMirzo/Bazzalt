"""Fetch pinned Recast source only; never configure or build a dependency locally."""

from pathlib import Path
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def Run(*args):
    return subprocess.run(
        args, check=True, text=True, encoding="utf-8", capture_output=True
    ).stdout.strip()


def Fetch():
    lock = json.loads(
        (ROOT / "scripts/dependencies/recast.json").read_text(encoding="utf-8")
    )
    destination = ROOT / "deps/recast"
    if not destination.exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [
                "git",
                "clone",
                "--depth",
                "1",
                "--branch",
                lock["tag"],
                "--",
                lock["repository"],
                str(destination),
            ],
            check=True,
        )
    if not (destination / ".git").exists():
        raise RuntimeError("Incomplete Recast directory; refusing to overwrite it")
    if Run("git", "-C", str(destination), "rev-parse", "HEAD") != lock["commit"]:
        raise RuntimeError("Recast revision integrity check failed")
    if Run(
        "git",
        "-C",
        str(destination),
        "status",
        "--porcelain",
        "--untracked-files=normal",
    ):
        raise RuntimeError(
            "Recast source has local changes; refusing to use modified dependency source"
        )
    if not (destination / "CMakeLists.txt").is_file():
        raise RuntimeError("Invalid Recast source layout")
    print(f"Recast {lock['version']} source is ready for the private AI module.")


if __name__ == "__main__":
    Fetch()
