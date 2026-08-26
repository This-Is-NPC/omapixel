#!/usr/bin/env python3
"""Check the staged release contains and discovers the complete agent skill."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


EXPECTED = {"SKILL.md", "drawing.md", "animation.md", "layers.md", "studio.md"}


def main():
    assert len(sys.argv) == 2, "usage: test_skill_dist.py DIST_ROOT"
    root = Path(sys.argv[1]).resolve()
    cli = root / "bin/omapixel"
    skill = root / "share/omapixel/agents/skills/omapixel"

    assert cli.is_file(), cli
    assert skill.is_dir(), skill
    assert {path.name for path in skill.iterdir()} == EXPECTED

    with tempfile.TemporaryDirectory(prefix="omapixel dist skill ") as directory:
        home = Path(directory)
        status = subprocess.run(
            [str(cli), "skill"],
            env={
                **os.environ,
                "HOME": str(home),
                "OMAPIXEL_CONFIG_PATH": str(home / "missing-config.toml"),
                "QT_QPA_PLATFORM": "offscreen",
                "QT_QPA_PLATFORMTHEME": "",
            },
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        assert status.returncode == 0, (status.stdout, status.stderr)
        assert f"source  {skill}" in status.stdout


if __name__ == "__main__":
    main()
