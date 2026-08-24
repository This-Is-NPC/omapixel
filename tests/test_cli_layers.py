#!/usr/bin/env python3
"""Focused command-line contract tests for deterministic layer control."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
CLI = Path(os.environ.get("OMAPIXEL_CLI", ROOT / "build/bin/omapixel"))
FIXTURE = ROOT / "tests/fixtures/format-v2/valid/animated-shared.json"


def run(*args):
    result = subprocess.run(
        [str(CLI), *map(str, args)],
        cwd=ROOT,
        env={**os.environ, "QT_QPA_PLATFORM": "offscreen", "QT_QPA_PLATFORMTHEME": ""},
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return result


def expect(result, code=0, contains=None):
    assert result.returncode == code, (result.stdout, result.stderr)
    if contains:
        assert contains in result.stdout + result.stderr, result.stderr


def main():
    with tempfile.TemporaryDirectory(prefix="omapixel-cli-layer-") as directory:
        root = Path(directory)
        document = root / "layers.json"

        expect(run("new", document, "--size", "3x2"))
        expect(run("layer", document, "add", "--id", "hero", "--name", "Hero Layer"))
        expect(run("frame", document, "add", "--frame", "0"))
        listed = run("layer", document, "list")
        expect(listed)
        layers = json.loads(listed.stdout)
        assert [layer["id"] for layer in layers] == ["layer", "hero"]
        assert layers[1]["name"] == "Hero Layer"

        missing = run("paint", document, "--at", "0,0", "--slot", "I")
        expect(missing, 2, "E_LAYER_TARGET_REQUIRED")
        expect(run("paint", document, "--layer", "Hero Layer", "--scope", "frame",
                   "--at", "0,0", "--slot", "I"))
        expect(run("paint", document, "--layer-id", "hero", "--scope", "all-frames",
                   "--at", "2,0", "--slot", "I"))
        saved = json.loads(document.read_text(encoding="utf-8"))
        hero = next(layer for layer in saved["layers"] if layer["id"] == "hero")
        assert all("I" in "".join(cel["rows"]) for cel in hero["cels"])
        conflict = run("paint", document, "--layer-id", "layer", "--layer", "Hero Layer",
                       "--scope", "frame", "--at", "0,0", "--slot", "I")
        expect(conflict, 2, "E_LAYER_TARGET_CONFLICT")

        expect(run("layer", document, "rename", "--layer-id", "hero", "--name", "Main Hero"))
        expect(run("layer", document, "move", "--layer", "Main Hero", "--index", "0"))
        expect(run("layer", document, "set", "--layer-id", "hero", "--visible", "true",
                   "--opacity", "128"))
        expect(run("layer", document, "mode", "--layer-id", "hero", "--mode", "multiply"))
        negative = run("layer", document, "move", "--layer-id", "hero", "--index", "-1")
        expect(negative, 1, "E_LAYER_INDEX")

        expect(run("layer", document, "dup", "--layer-id", "hero", "--id", "copy",
                   "--name", "Main Hero Copy"))
        expect(run("layer", document, "rm", "--layer", "Main Hero Copy"))

        expect(run("layer", document, "set", "--layer-id", "hero", "--visible", "false"))
        hidden = run("paint", document, "--layer-id", "hero", "--scope", "frame",
                     "--at", "1,0", "--slot", "I")
        expect(hidden, 1, "E_LAYER_HIDDEN")
        expect(run("paint", document, "--layer-id", "hero", "--scope", "frame",
                   "--include-hidden", "--at", "1,0", "--slot", "I"))
        expect(run("layer", document, "set", "--layer-id", "hero", "--visible", "true"))

        expect(run("layer", document, "set", "--layer-id", "hero", "--locked", "true"))
        locked = run("paint", document, "--layer-id", "hero", "--scope", "frame",
                     "--at", "1,0", "--slot", "I")
        expect(locked, 1, "E_LAYER_LOCKED")
        expect(run("layer", document, "set", "--layer-id", "hero", "--locked", "false"))

        expect(run("layer", document, "move", "--layer-id", "hero", "--index", "1"))
        merged = run("layer", document, "merge-down", "--layer-id", "hero")
        expect(merged, 0, "removed-layers=1")
        assert json.loads(run("layer", document, "list").stdout)[0]["id"] == "layer"

        flat = root / "flat.json"
        refused = run("flatten", FIXTURE, "-o", flat)
        expect(refused, 1, "E_LAYER_LOCKED")
        assert not flat.exists()
        anyway_refused = run("flatten", FIXTURE, "-o", flat, "--anyway")
        expect(anyway_refused, 1, "E_LAYER_LOCKED")
        assert not flat.exists()

        composite = root / "composite.png"
        isolated = root / "isolated.png"
        expect(run("render", FIXTURE, "-o", composite))
        expect(run("render", FIXTURE, "-o", isolated, "--isolated", "--layer-id", "hero"))
        assert composite.read_bytes() != isolated.read_bytes()

        first = root / "batch-a.json"
        second = root / "batch-b.json"
        shutil.copyfile(document, first)
        shutil.copyfile(document, second)
        script = root / "layers.batch"
        script.write_text(
            'layer add --id batch --name "Batch Layer"\n'
            'layer rename --layer-id batch --name "Batch Layer Renamed"\n'
            'layer set --layer-id batch --opacity 96\n'
            'layer mode --layer-id batch --mode screen\n'
            'layer move --layer-id batch --index 0\n',
            encoding="utf-8",
        )
        expect(run("batch", first, "--script", script), 0, "5 command(s), 5 change(s)")
        expect(run("batch", second, "--script", script), 0, "5 command(s), 5 change(s)")
        assert first.read_bytes() == second.read_bytes()

        unsafe_path = root / "terminal\x1b]8;;bad\x07.json"
        created = run("new", unsafe_path)
        expect(created, 0)
        assert "\x1b" not in created.stdout + created.stderr
        assert "\\u001b" in created.stdout

        unsafe_size = run("new", root / "rejected.json", "--size", "3x2\r\n\t\x1b]0;bad\x07")
        expect(unsafe_size, 2)
        assert "\x1b" not in unsafe_size.stdout + unsafe_size.stderr
        assert "\\u001b" in unsafe_size.stderr

        line_limited = root / "too-many-lines.batch"
        line_limited.write_text("# comment\n" * 8193, encoding="utf-8")
        expect(run("batch", first, "--script", line_limited), 1,
               "hard line limit")

        command_limited = root / "too-many-commands.batch"
        command_limited.write_text("layer list\n" * 4097, encoding="utf-8")
        expect(run("batch", first, "--script", command_limited), 2,
               "hard command limit")

    print("cli-layer-tests: 36 command checks passed")


if __name__ == "__main__":
    main()
