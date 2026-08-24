#!/usr/bin/env python3
"""Cross-surface acceptance fixture for the native-layer workflow."""

import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
CLI = Path(os.environ.get("OMAPIXEL_CLI", ROOT / "build/bin/omapixel"))
STUDIO = Path(os.environ.get("OMAPIXEL_STUDIO", ROOT / "build/bin/omapixel-studio"))


def main():
    checks = 0

    def check(condition, message):
        nonlocal checks
        if not condition:
            raise AssertionError(message)
        checks += 1

    def run(*args, env):
        return subprocess.run(
            [str(CLI), *map(str, args)],
            cwd=ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    with tempfile.TemporaryDirectory(prefix="omapixel-layers-e2e-") as directory:
        root = Path(directory)
        runtime = root / "runtime"
        runtime.mkdir()
        runtime.chmod(0o700)
        env = {
            **os.environ,
            "QT_QPA_PLATFORM": "offscreen",
            "QT_QPA_PLATFORMTHEME": "",
            "XDG_RUNTIME_DIR": str(runtime),
            "OMAPIXEL_CONFIG_PATH": str(root / "missing-config.toml"),
        }
        document = root / "workflow.json"
        result = run("new", document, "--size", "3x2", env=env)
        check(result.returncode == 0, result.stderr)
        result = run("layer", document, "add", "--id", "hero", "--name", "Hero",
                     "--storage", "animated", env=env)
        check(result.returncode == 0, result.stderr)
        result = run("layer", document, "add", "--id", "guide", "--name", "Guide",
                     "--storage", "shared", env=env)
        check(result.returncode == 0, result.stderr)
        result = run("frame", document, "add", "--frame", "0", env=env)
        check(result.returncode == 0, result.stderr)
        result = run("paint", document, "--layer-id", "hero", "--scope", "frame",
                     "--frame", "0", "--at", "0,0", "--slot", "I", env=env)
        check(result.returncode == 0, result.stderr)
        result = run("paint", document, "--layer-id", "hero", "--scope", "all-frames",
                     "--at", "2,1", "--slot", "R", env=env)
        check(result.returncode == 0, result.stderr)
        result = run("paint", document, "--layer-id", "guide", "--scope", "all-frames",
                     "--at", "1,0", "--slot", "I", env=env)
        check(result.returncode == 0, result.stderr)
        result = run("layer", document, "set", "--layer-id", "hero", "--opacity", "192",
                     env=env)
        check(result.returncode == 0, result.stderr)
        listed = run("layer", document, "list", env=env)
        check(listed.returncode == 0, listed.stderr)
        layers = json.loads(listed.stdout)
        check({layer["id"] for layer in layers} == {"layer", "hero", "guide"}, layers)
        check(next(layer for layer in layers if layer["id"] == "hero")["storage"] == "animated", layers)
        check(next(layer for layer in layers if layer["id"] == "guide")["storage"] == "shared", layers)

        studio = subprocess.Popen(
            [str(STUDIO), str(document)], cwd=ROOT, env=env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                where = run("where", document, env=env)
                if where.returncode == 0:
                    break
                time.sleep(0.05)
            check(where.returncode == 0, where.stderr)
            sessions = json.loads(where.stdout)["sessions"]
            check(len(sessions) == 1, sessions)
            check(sessions[0]["view"]["layerId"] in {"layer", "hero", "guide"}, sessions)
            check(sessions[0]["view"]["scope"] in {"frame", "all-frames"}, sessions)

            result = run("paint", document, "--layer-id", "hero", "--scope", "frame",
                         "--frame", "1", "--at", "1,1", "--slot", "I", env=env)
            check(result.returncode == 0, result.stderr)
            time.sleep(0.2)
            check(studio.poll() is None, "Studio exited during external reload")

            shot = root / "studio.png"
            shot_env = {**env, "OMAPIXEL_SHOT": str(shot)}
            shot_result = subprocess.run(
                [str(STUDIO), str(document)], cwd=ROOT, env=shot_env,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                timeout=15,
            )
            check(shot_result.returncode == 0, shot_result.stderr)
            check(shot.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"), "Studio shot is not PNG")

            composite = root / "composite.png"
            isolated = root / "isolated.png"
            check(run("render", document, "-o", composite, env=env).returncode == 0,
                  "composite render failed")
            check(run("render", document, "-o", isolated, "--isolated", "--layer-id", "hero",
                      env=env).returncode == 0, "isolated render failed")
            check(composite.read_bytes() != isolated.read_bytes(), "composite equals isolation")

            flattened = root / "flattened.json"
            refused = run("flatten", document, "-o", flattened, env=env)
            check(refused.returncode == 1, refused.stderr)
            check("E_FLATTEN_PALETTE_LOSS" in refused.stderr, refused.stderr)
            applied = run("flatten", document, "-o", flattened, "--anyway", env=env)
            check(applied.returncode == 0, applied.stderr)
            check(flattened.exists(), "flatten did not create output")
            check(json.loads(flattened.read_text())["layers"][0]["id"] == "flattened",
                  flattened.read_text())
        finally:
            studio.send_signal(signal.SIGTERM)
            try:
                studio.wait(timeout=5)
            except subprocess.TimeoutExpired:
                studio.kill()
                studio.wait()

        nobody = run("where", document, env=env)
        check(nobody.returncode == 1, nobody.stdout)

    print(f"layers-e2e: {checks} assertions passed")


if __name__ == "__main__":
    main()
