#!/usr/bin/env python3
"""Check the staged release contains and discovers the complete agent skill."""

import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


EXPECTED = {"SKILL.md", "drawing.md", "animation.md", "layers.md", "studio.md"}
MIME_TYPE = "application/x-omapixel"
MIME_NAMESPACE = "http://www.freedesktop.org/standards/shared-mime-info"


def main():
    assert len(sys.argv) == 2, "usage: test_skill_dist.py DIST_ROOT"
    root = Path(sys.argv[1]).resolve()
    cli = root / "bin/omapixel"
    skill = root / "share/omapixel/agents/skills/omapixel"
    repo = Path(__file__).resolve().parents[1]
    examples = root / "share/doc/omapixel/examples"
    desktop = root / "share/applications/omapixel-studio.desktop"
    mime = root / "share/mime/packages/omapixel.xml"

    assert cli.is_file(), cli
    assert skill.is_dir(), skill
    assert {path.name for path in skill.iterdir()} == EXPECTED

    assert desktop.is_file(), desktop
    assert "MimeType=application/x-omapixel;" in desktop.read_text(encoding="utf-8")

    assert mime.is_file(), mime
    document = ET.parse(mime).getroot()
    mime_types = document.findall(f"{{{MIME_NAMESPACE}}}mime-type")
    assert [element.get("type") for element in mime_types] == [MIME_TYPE]
    assert [
        glob.get("pattern")
        for glob in mime_types[0].findall(f"{{{MIME_NAMESPACE}}}glob")
    ] == ["*.omapixel"]

    source_examples = repo / "examples"
    source_files = {
        path.relative_to(source_examples).as_posix()
        for path in source_examples.rglob("*")
        if path.is_file()
    }
    assert source_files
    assert all(
        path == "README.md" or path.endswith((".omapixel", ".gif"))
        for path in source_files
    )
    staged_files = {
        path.relative_to(examples).as_posix()
        for path in examples.rglob("*")
        if path.is_file()
    }
    assert staged_files == source_files

    readme = examples / "README.md"
    assert readme.is_file(), readme
    for match in re.finditer(r'href="([^"]+)"|src="([^"]+)"|\]\(([^)]+)\)', readme.read_text(encoding="utf-8")):
        target = next(value for value in match.groups() if value is not None)
        assert (readme.parent / target).resolve().exists(), target

    doc_files = root / "share/doc/omapixel"
    assert not any(
        path.name == "releasing.md" or "internal" in path.parts
        for path in doc_files.rglob("*")
    )

    pkgbuild = (repo / "pkgbuild/PKGBUILD").read_text(encoding="utf-8")
    assert "sha256sums=('SKIP')" in pkgbuild
    assert "packaging/omapixel.xml" in pkgbuild
    assert "/usr/share/mime/packages/omapixel.xml" in pkgbuild
    assert "examples/README.md" in pkgbuild
    assert "/usr/share/doc/$pkgname/examples" in pkgbuild
    assert json.loads((repo / "pkgbuild/.omarchy/package.json").read_text()) == {
        "source": "local"
    }

    mise = (repo / "mise.toml").read_text(encoding="utf-8")
    assert "packaging/omapixel.xml" in mise
    assert "share/mime/packages" in mise
    assert "examples/README.md" in mise
    assert "share/doc/omapixel/examples" in mise

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
