#!/usr/bin/env python3
"""Deterministic local plugin discovery and inspection checks."""

import json
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
CLI = Path(os.environ.get("OMAPIXEL_CLI", ROOT / "build/bin/omapixel"))


def run(*args, env=None):
    return subprocess.run(
        [str(CLI), *map(str, args)],
        cwd=ROOT,
        env={
            **os.environ,
            "QT_QPA_PLATFORM": "offscreen",
            "QT_QPA_PLATFORMTHEME": "",
            **(env or {}),
        },
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def manifest(plugin_id, name=None, executable="run.sh"):
    return {
        "schemaVersion": 1,
        "id": plugin_id,
        "name": name or plugin_id,
        "version": "1.0.0",
        "pluginApi": 1,
        "executable": executable,
        "actions": [{"name": "export", "kind": "export"}],
    }


def install(root, plugin_id, *, name=None, executable="run.sh", executable_text=None):
    plugin = root / plugin_id
    plugin.mkdir(parents=True)
    (plugin / "omapixel-plugin.json").write_text(
        json.dumps(manifest(plugin_id, name, executable)), encoding="utf-8"
    )
    if executable_text is None:
        executable_text = "#!/bin/sh\n"
    executable_path = plugin / executable
    executable_path.parent.mkdir(parents=True, exist_ok=True)
    executable_path.write_text(executable_text, encoding="utf-8")
    executable_path.chmod(0o755)
    return plugin


def main():
    with tempfile.TemporaryDirectory(prefix="omapixel plugin ") as directory:
        root = Path(directory)
        first = root / "first"
        second = root / "second"
        data_home = root / "xdg"
        first.mkdir()
        second.mkdir()
        (data_home / "omapixel" / "plugins").mkdir(parents=True)

        sentinel = root / "sentinel"
        winner = install(
            first,
            "zeta",
            name="First winner",
            executable_text='#!/bin/sh\n: > "$OMAPIXEL_SENTINEL"\n',
        )
        install(second, "zeta", name="Later duplicate")
        install(second, "bravo")
        install(data_home / "omapixel" / "plugins", "alpha", name="A \u03c0 plugin")

        broken = first / "broken"
        broken.mkdir()
        (broken / "omapixel-plugin.json").write_text("{", encoding="utf-8")
        missing = first / "missing"
        missing.mkdir()

        env = {
            "OMAPIXEL_PLUGIN_PATH": os.pathsep.join((str(first), str(second))),
            "XDG_DATA_HOME": str(data_home),
            "HOME": str(root / "home"),
            "OMAPIXEL_SENTINEL": str(sentinel),
        }
        listed = run("plugin", "list", "--json", env=env)
        assert listed.returncode == 1, (listed.stdout, listed.stderr)
        report = json.loads(listed.stdout)
        assert [plugin["id"] for plugin in report["plugins"]] == ["alpha", "bravo", "zeta"]
        assert report["plugins"][-1]["path"] == str(winner)
        assert any("duplicate plugin id" in item["message"] for item in report["diagnostics"])
        assert any("missing" in item["path"] for item in report["diagnostics"])
        assert any("invalid JSON" in item["message"] or "malformed JSON" in item["message"]
                   for item in report["diagnostics"])

        checked = run("plugin", "check", str(winner), "--json", env=env)
        assert checked.returncode == 0, (checked.stdout, checked.stderr)
        assert json.loads(checked.stdout)["plugin"]["id"] == "zeta"

        checked_id = run("plugin", "check", "zeta", "--json", env=env)
        assert checked_id.returncode == 1
        assert any("duplicate plugin id" in item["message"]
                   for item in json.loads(checked_id.stdout)["diagnostics"])

        absent = run("plugin", "check", "not-installed", "--json", env=env)
        assert absent.returncode == 1
        assert json.loads(absent.stdout)["plugin"] is None

        assert not sentinel.exists()

        unsafe = first / "unsafe"
        unsafe.mkdir()
        (unsafe / "omapixel-plugin.json").write_text(
            json.dumps(manifest("unsafe", executable="../outside")), encoding="utf-8"
        )
        no_exec = install(first, "no-exec")
        (no_exec / "run.sh").chmod(0o644)
        invalid = run("plugin", "check", str(unsafe), "--json", env=env)
        assert invalid.returncode == 1
        assert "$.executable" in json.loads(invalid.stdout)["diagnostics"][0]["message"]
        invalid = run("plugin", "check", str(no_exec), "--json", env=env)
        assert invalid.returncode == 1
        assert json.loads(invalid.stdout)["plugin"] is None

        for field in ("schemaVersion", "pluginApi"):
            slug = "schema-version" if field == "schemaVersion" else "plugin-api"
            fractional = first / f"fractional-{slug}"
            fractional.mkdir()
            fractional_manifest = manifest(f"fractional-{slug}")
            fractional_manifest[field] = 1.0
            (fractional / "omapixel-plugin.json").write_text(
                json.dumps(fractional_manifest), encoding="utf-8"
            )
            (fractional / "run.sh").write_text("#!/bin/sh\n", encoding="utf-8")
            (fractional / "run.sh").chmod(0o755)
            invalid = run("plugin", "check", str(fractional), "--json", env=env)
            assert invalid.returncode == 1
            assert any("must be exactly integer 1" in item["message"]
                       for item in json.loads(invalid.stdout)["diagnostics"])

        duplicate_keys = first / "duplicate-keys"
        duplicate_keys.mkdir()
        duplicate_manifest = json.dumps(manifest("duplicate-keys"))[:-1]
        (duplicate_keys / "omapixel-plugin.json").write_text(
            duplicate_manifest + ',"id":"duplicate-keys"}', encoding="utf-8"
        )
        invalid = run("plugin", "check", str(duplicate_keys), "--json", env=env)
        assert invalid.returncode == 1
        assert "duplicate" in json.loads(invalid.stdout)["diagnostics"][0]["message"]

        oversized = first / "oversized"
        oversized.mkdir()
        (oversized / "omapixel-plugin.json").write_bytes(
            b'{"schemaVersion":1}' + b" " * (16 * 1024 * 1024)
        )
        invalid = run("plugin", "check", str(oversized), "--json", env=env)
        assert invalid.returncode == 1
        assert "hard limit" in json.loads(invalid.stdout)["diagnostics"][0]["message"]

        symlinked = install(first, "symlinked")
        (symlinked / "run.sh").unlink()
        (symlinked / "real-run.sh").write_text("#!/bin/sh\n", encoding="utf-8")
        (symlinked / "real-run.sh").chmod(0o755)
        (symlinked / "run.sh").symlink_to("real-run.sh")
        invalid = run("plugin", "check", str(symlinked), "--json", env=env)
        assert invalid.returncode == 1

        fallback_home = root / "fallback-home"
        fallback_plugin_root = fallback_home / ".local" / "share" / "omapixel" / "plugins"
        install(fallback_plugin_root, "fallback")
        fallback = run(
            "plugin", "check", "fallback", "--json",
            env={"OMAPIXEL_PLUGIN_PATH": "", "XDG_DATA_HOME": "", "HOME": str(fallback_home)},
        )
        assert fallback.returncode == 0, (fallback.stdout, fallback.stderr)
        assert json.loads(fallback.stdout)["plugin"]["id"] == "fallback"

        assert run("plugin", env=env).returncode == 2
        assert run("plugin", "list", "extra", env=env).returncode == 2
        assert run("plugin", "check", env=env).returncode == 2


if __name__ == "__main__":
    main()
