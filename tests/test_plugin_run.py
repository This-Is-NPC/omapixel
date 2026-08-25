#!/usr/bin/env python3
"""End-to-end checks for the synchronous one-shot plugin runner."""

import json
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
CLI = Path(os.environ.get("OMAPIXEL_CLI", ROOT / "build/bin/omapixel"))
FIXTURES = ROOT / "tests" / "fixtures" / "plugin"
DOCUMENT = ROOT / "tests" / "fixtures" / "format-v2" / "valid" / "minimal.json"


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


def plugin_workspaces():
    return set(Path(tempfile.gettempdir()).glob("omapixel-plugin-*"))


def assert_workspace_clean(before):
    assert plugin_workspaces() == before, plugin_workspaces() - before


def install_fixture(root):
    plugin = root / "example-exporter"
    plugin.mkdir()
    (plugin / "omapixel-plugin.json").write_text(
        (FIXTURES / "valid" / "omapixel-plugin.json").read_text(), encoding="utf-8"
    )
    fixture = plugin / "plugin_fixture.py"
    fixture.write_bytes((FIXTURES / "plugin_fixture.py").read_bytes())
    fixture.chmod(0o755)


def main():
    with tempfile.TemporaryDirectory(prefix="omapixel plugin run ") as directory:
        root = Path(directory)
        plugin_root = root / "plugins"
        plugin_root.mkdir()
        install_fixture(plugin_root)
        source = root / "source art \u03c0.json"
        source.write_bytes(DOCUMENT.read_bytes())
        output = root / "output dir" / "r\u00e9sultat \u03c0.bin"
        output.parent.mkdir()
        output.write_bytes(b"keep this on failure")
        baseline_workspaces = plugin_workspaces()
        env = {
            "OMAPIXEL_PLUGIN_PATH": str(plugin_root),
            "OMAPIXEL_PLUGIN_SECRET": "must-not-leak",
            "PATH": "/usr/bin:/bin",
        }

        success = run(
            "plugin", "run", "example-exporter", "png", source,
            "--out", output, "--param", "case=progress", "--param", "label=two words \u03c0",
            "--json", env=env,
        )
        assert success.returncode == 0, (success.stdout, success.stderr)
        assert success.stderr == ""
        assert json.loads(success.stdout) == {
            "ok": True, "plugin": "example-exporter", "action": "png", "out": str(output)
        }
        assert output.read_bytes() == b"fixture artifact"
        assert_workspace_clean(baseline_workspaces)

        diagnostic = run(
            "plugin", "run", "example-exporter", "png", source,
            "--out", output, "--param", "case=diagnostic", env=env,
        )
        assert diagnostic.returncode == 0, (diagnostic.stdout, diagnostic.stderr)
        assert diagnostic.stdout.startswith(str(output))
        assert "fixture diagnostic" in diagnostic.stderr
        assert_workspace_clean(baseline_workspaces)

        inspect = root / "inspect.bin"
        inspected = run(
            "plugin", "run", "example-exporter", "png", source,
            "--out", inspect, "--param", "case=inspect",
            "--param", "label=two words \u03c0", env=env,
        )
        assert inspected.returncode == 0, (inspected.stdout, inspected.stderr)
        details = json.loads(inspect.read_text())
        assert details["document"] == "input/document.json"
        assert details["outputDir"] == "output"
        assert details["snapshot"]["version"] == 2
        workspace = Path(details["cwd"])
        assert details["cwd"].startswith("/tmp/omapixel-plugin-")
        retained = {"PATH", "HOME", "LANG", "LC_ALL", "LC_CTYPE", "TMPDIR"}
        assert set(details["environment"]) == retained & set(os.environ), details["environment"]
        assert details["params"] == [
            {"key": "case", "value": "inspect"},
            {"key": "label", "value": "two words \u03c0"},
        ]
        assert not workspace.exists()
        assert_workspace_clean(baseline_workspaces)

        for case in ("malformed", "unexpected-stdout", "duplicate-result", "missing-result",
                     "mismatched-id", "traversal", "absolute", "symlink", "non-regular",
                     "artifact-oversized", "missing-artifact", "nonzero", "crash",
                     "oversized-output", "stderr-oversized"):
            output.write_bytes(b"unchanged")
            failed = run(
                "plugin", "run", "example-exporter", "png", source,
                "--out", output, "--param", f"case={case}", env=env,
            )
            assert failed.returncode == 1, (case, failed.stdout, failed.stderr)
            assert output.read_bytes() == b"unchanged"
            assert_workspace_clean(baseline_workspaces)

        timeout = run(
            "plugin", "run", "example-exporter", "png", source,
            "--out", output, "--param", "case=timeout", env=env,
        )
        assert timeout.returncode == 1, (timeout.stdout, timeout.stderr)
        assert "timed out" in timeout.stderr
        assert output.read_bytes() == b"unchanged"
        assert_workspace_clean(baseline_workspaces)

        for raw in ("", "missing-equals", "=value", "key=", "key=" + "x" * 129):
            sentinel = root / "sentinel"
            output.write_bytes(b"unchanged")
            failed = run(
                "plugin", "run", "example-exporter", "png", source,
                "--out", output, "--param", raw,
                env={**env, "OMAPIXEL_SENTINEL": str(sentinel)},
            )
            assert failed.returncode == 2, (raw, failed.stdout, failed.stderr)
            assert output.read_bytes() == b"unchanged"
            assert not sentinel.exists()
            assert_workspace_clean(baseline_workspaces)

        duplicate = run(
            "plugin", "run", "example-exporter", "png", source,
            "--out", output, "--param", "x=1", "--param", "x=2", env=env,
        )
        assert duplicate.returncode == 2
        assert "duplicate parameter" in duplicate.stderr
        assert output.read_bytes() == b"unchanged"
        assert_workspace_clean(baseline_workspaces)

        omitted = run(
            "plugin", "run", "example-exporter", "png", source,
            "--out", output, "--param", env=env,
        )
        assert omitted.returncode == 2, (omitted.stdout, omitted.stderr)
        assert "must be KEY=VALUE with a value" in omitted.stderr
        assert output.read_bytes() == b"unchanged"
        assert_workspace_clean(baseline_workspaces)

        publication_dir = root / "read-only destination"
        publication_dir.mkdir()
        publication = publication_dir / "result.bin"
        publication.write_bytes(b"pre-existing destination")
        source_before = source.read_bytes()
        publication_dir.chmod(0o555)
        try:
            failed_publication = run(
                "plugin", "run", "example-exporter", "png", source,
                "--out", publication, env=env,
            )
        finally:
            publication_dir.chmod(0o755)
        assert failed_publication.returncode == 1, (
            failed_publication.stdout, failed_publication.stderr
        )
        assert "result.bin" in failed_publication.stderr
        assert publication.read_bytes() == b"pre-existing destination"
        assert source.read_bytes() == source_before
        assert_workspace_clean(baseline_workspaces)

        document_before = DOCUMENT.read_bytes()
        same_path = run(
            "plugin", "run", "example-exporter", "png", DOCUMENT,
            "--out", DOCUMENT, env=env,
        )
        assert same_path.returncode == 1
        assert DOCUMENT.read_bytes() == document_before
        assert_workspace_clean(baseline_workspaces)

        missing_out = run(
            "plugin", "run", "example-exporter", "png", DOCUMENT, env=env,
        )
        assert missing_out.returncode == 2
        assert_workspace_clean(baseline_workspaces)

        startup = root / "startup-failure"
        startup.mkdir()
        (startup / "omapixel-plugin.json").write_text(
            json.dumps({
                "schemaVersion": 1,
                "id": "startup-failure",
                "name": "Startup failure",
                "version": "1.0.0",
                "pluginApi": 1,
                "executable": "run.sh",
                "actions": [{"name": "png", "kind": "export"}],
            }), encoding="utf-8"
        )
        (startup / "run.sh").write_text("#!/no-such-interpreter\n", encoding="utf-8")
        (startup / "run.sh").chmod(0o755)
        failed_start = run(
            "plugin", "run", "startup-failure", "png", source,
            "--out", output, "--json", env={**env, "OMAPIXEL_PLUGIN_PATH": str(root)},
        )
        assert failed_start.returncode == 1
        assert json.loads(failed_start.stdout)["ok"] is False
        assert "failed to start" in failed_start.stderr
        assert_workspace_clean(baseline_workspaces)


if __name__ == "__main__":
    main()
