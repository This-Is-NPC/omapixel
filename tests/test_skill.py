#!/usr/bin/env python3
"""Portable agent-skill content and installer contract."""

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
CLI = Path(os.environ.get("OMAPIXEL_CLI", ROOT / "build/bin/omapixel"))
SKILL = ROOT / "agents/skills/omapixel"
DESTINATION = Path(".agents/skills/omapixel")


def run(home, *args, cli=CLI):
    return subprocess.run(
        [str(cli), *args],
        cwd=ROOT,
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


def check_content():
    expected = {"SKILL.md", "drawing.md", "animation.md", "layers.md", "studio.md"}
    assert {path.name for path in SKILL.iterdir()} == expected

    text = (SKILL / "SKILL.md").read_text(encoding="utf-8")
    assert text.startswith("---\nname: omapixel\ndescription:")
    assert "Excludes development of the Omapixel source code." in text
    assert "omapixel where <file>" in text
    assert "Use `batch`" in text

    references = re.findall(r"\]\(([^)]+\.md)\)", text)
    assert references == ["drawing.md", "animation.md", "layers.md", "studio.md"]
    for reference in references:
        assert (SKILL / reference).is_file(), reference


def check_install():
    with tempfile.TemporaryDirectory(prefix="omapixel skill ") as directory:
        home = Path(directory)

        status = run(home, "skill")
        assert status.returncode == 0, (status.stdout, status.stderr)
        assert f"source  {SKILL}" in status.stdout
        assert f"missing  {home / DESTINATION}" in status.stdout

        installed = run(home, "skill", "install")
        assert installed.returncode == 0, (installed.stdout, installed.stderr)
        assert "restart your agent" in installed.stdout
        destination = home / DESTINATION
        assert destination.is_symlink()
        assert destination.resolve() == SKILL.resolve()

        repeated = run(home, "skill", "install")
        assert repeated.returncode == 0, (repeated.stdout, repeated.stderr)
        assert "already installed" in repeated.stdout

        status = run(home, "skill")
        assert status.returncode == 0
        assert f"linked   {destination}" in status.stdout


def check_conflict_is_refused():
    with tempfile.TemporaryDirectory(prefix="omapixel skill conflict ") as directory:
        home = Path(directory)
        conflict = home / DESTINATION
        conflict.mkdir(parents=True)
        (conflict / "SKILL.md").write_text("custom\n", encoding="utf-8")

        refused = run(home, "skill", "install")
        assert refused.returncode == 1, (refused.stdout, refused.stderr)
        assert "already exists" in refused.stderr
        assert (conflict / "SKILL.md").read_text(encoding="utf-8") == "custom\n"
        status = run(home, "skill")
        assert status.returncode == 1
        assert f"conflict {conflict}" in status.stdout
        assert "resolve conflicts" in status.stdout


def check_dangling_link_is_a_conflict():
    with tempfile.TemporaryDirectory(prefix="omapixel skill dangling ") as directory:
        home = Path(directory)
        dangling = home / DESTINATION
        dangling.parent.mkdir(parents=True)
        dangling.symlink_to(home / "absent")
        refused = run(home, "skill", "install")
        assert refused.returncode == 1
        assert dangling.is_symlink()


def check_parent_failure_is_safe():
    with tempfile.TemporaryDirectory(prefix="omapixel skill parent ") as directory:
        home = Path(directory)
        blocker = home / ".agents"
        blocker.write_text("not a directory\n", encoding="utf-8")

        refused = run(home, "skill", "install")
        assert refused.returncode == 1, (refused.stdout, refused.stderr)
        assert "could not link" in refused.stderr
        assert blocker.read_text(encoding="utf-8") == "not a directory\n"


def check_installed_layout():
    with tempfile.TemporaryDirectory(prefix="omapixel skill layout ") as directory:
        root = Path(directory) / "prefix"
        installed_cli = root / "bin/omapixel"
        installed_skill = root / "share/omapixel/agents/skills/omapixel"
        installed_cli.parent.mkdir(parents=True)
        shutil.copy2(CLI, installed_cli)
        shutil.copytree(SKILL, installed_skill)

        home = Path(directory) / "home"
        status = run(home, "skill", cli=installed_cli)
        assert status.returncode == 0, (status.stdout, status.stderr)
        assert f"source  {installed_skill}" in status.stdout

        installed = run(home, "skill", "install", cli=installed_cli)
        assert installed.returncode == 0, (installed.stdout, installed.stderr)
        assert (home / DESTINATION).resolve() == installed_skill.resolve()


def check_usage():
    with tempfile.TemporaryDirectory(prefix="omapixel skill usage ") as directory:
        home = Path(directory)
        assert run(home, "skill", "remove").returncode == 2
        assert run(home, "skill", "install", "extra").returncode == 2


def main():
    check_content()
    check_install()
    check_conflict_is_refused()
    check_dangling_link_is_a_conflict()
    check_parent_failure_is_safe()
    check_installed_layout()
    check_usage()


if __name__ == "__main__":
    main()
