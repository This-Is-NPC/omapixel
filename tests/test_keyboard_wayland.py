#!/usr/bin/env python3
"""Keyboard-only Studio smoke test through a real headless Sway session."""

import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build/bin/omapixel"
STUDIO = ROOT / "build/bin/omapixel-studio"


def require(program):
    path = shutil.which(program)
    if not path:
        raise RuntimeError(f"keyboard-wayland requires {program}")
    return path


def wait_for(description, probe, timeout=10):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            last = probe()
            if last:
                return last
        except (OSError, subprocess.SubprocessError, json.JSONDecodeError) as error:
            last = error
        time.sleep(0.05)
    raise AssertionError(f"timed out waiting for {description}: {last}")


def run(*arguments, env, check=True):
    return subprocess.run(
        [str(argument) for argument in arguments],
        cwd=ROOT,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=check,
    )


def main():
    sway = require("sway")
    swaymsg = require("swaymsg")
    wtype = require("wtype")
    if not CLI.is_file() or not STUDIO.is_file():
        raise RuntimeError("run `mise run build` before keyboard-wayland")

    checks = 0
    with tempfile.TemporaryDirectory(prefix="ok-", dir="/tmp") as directory:
        root = Path(directory)
        runtime = root / "runtime"
        runtime.mkdir(mode=0o700)
        home = root / "home"
        home.mkdir()
        config = root / "sway.conf"
        config.write_text(
            "output HEADLESS-1 mode 1280x720\n"
            "seat seat0 fallback true\n"
            "default_border none\n",
            encoding="utf-8",
        )
        log = (root / "sway.log").open("w+", encoding="utf-8")
        compositor_env = {
            **os.environ,
            "HOME": str(home),
            "XDG_RUNTIME_DIR": str(runtime),
            "WLR_BACKENDS": "headless",
            "WLR_HEADLESS_OUTPUTS": "1",
            "WLR_LIBINPUT_NO_DEVICES": "1",
            "WLR_RENDERER": "pixman",
        }
        compositor = subprocess.Popen(
            [sway, "--config", str(config)],
            cwd=ROOT,
            env=compositor_env,
            stdout=log,
            stderr=subprocess.STDOUT,
            text=True,
        )
        studio = None
        try:
            def desktop_environment():
                if compositor.poll() is not None:
                    log.seek(0)
                    raise RuntimeError(log.read())
                sockets = list(runtime.glob("sway-ipc.*.sock"))
                displays = [path for path in runtime.glob("wayland-*")
                            if not path.name.endswith(".lock")]
                if not sockets or not displays:
                    return None
                return {
                    **compositor_env,
                    "SWAYSOCK": str(sockets[0]),
                    "WAYLAND_DISPLAY": displays[0].name,
                    "QT_QPA_PLATFORM": "wayland",
                    "QT_QPA_PLATFORMTHEME": "",
                    "OMAPIXEL_CONFIG_PATH": str(root / "missing-config.toml"),
                }

            env = wait_for("headless Sway sockets", desktop_environment)

            def monitor_ready():
                result = run(swaymsg, "-t", "get_outputs", "--raw", env=env, check=False)
                return json.loads(result.stdout) if result.returncode == 0 else None

            wait_for("headless Sway output", monitor_ready, timeout=15)
            document = root / "keyboard.json"
            created = run(CLI, "new", document, "--size", "16x16", env=env)
            assert created.returncode == 0, created.stderr
            layered = run(CLI, "layer", document, "add", "--id", "overlay",
                          "--name", "Overlay", env=env)
            assert layered.returncode == 0, layered.stderr

            studio_log = (root / "studio.log").open("w+", encoding="utf-8")
            studio = subprocess.Popen(
                [str(STUDIO), str(document)],
                cwd=ROOT,
                env=env,
                stdout=studio_log,
                stderr=subprocess.STDOUT,
                text=True,
            )

            def walk_nodes(node):
                yield node
                for child in node.get("nodes", []) + node.get("floating_nodes", []):
                    yield from walk_nodes(child)

            def clients():
                result = run(swaymsg, "-t", "get_tree", "--raw", env=env)
                return [node for node in walk_nodes(json.loads(result.stdout))
                        if node.get("app_id") == "omapixel-studio"]

            def studio_client():
                if studio.poll() is not None:
                    studio_log.seek(0)
                    raise RuntimeError(studio_log.read())
                return next(
                    (client for client in clients()
                     if client.get("app_id") == "omapixel-studio"),
                    None,
                )

            try:
                studio_window = wait_for("Studio client", studio_client)
            except AssertionError as error:
                studio_log.seek(0)
                outputs = run(swaymsg, "-t", "get_outputs", "--raw",
                              env=env, check=False)
                raise AssertionError(
                    f"{error}\noutputs: {outputs.stdout}\n"
                    f"swaymsg errors: {outputs.stderr}\nstudio: {studio_log.read()}"
                ) from error
            checks += 1

            run(
                swaymsg,
                '[app_id="^omapixel-studio$"]',
                "focus",
                env=env,
            )

            def initial_studio_focus():
                return any(client.get("id") == studio_window.get("id")
                           and client.get("focused") for client in clients())

            wait_for("initial Studio focus", initial_studio_focus)
            checks += 1

            run(
                wtype,
                "-s", "500", "-M", "ctrl", "-M", "shift", "-k", "l",
                "-m", "shift", "-m", "ctrl",
                env=env,
            )
            try:
                def two_studio_clients():
                    found = clients()
                    return found if len(found) == 2 else None

                opened_clients = wait_for(
                    "Layer tool after direct shortcut",
                    two_studio_clients,
                )
            except AssertionError as error:
                tree = run(swaymsg, "-t", "get_tree", "--raw", env=env, check=False)
                raise AssertionError(
                    f"{error}\ntree: {tree.stdout}\nclients: {json.dumps(clients())}"
                ) from error
            checks += 1

            tool_window = next(client for client in opened_clients
                               if client.get("id") != studio_window.get("id"))

            def layer_tool_focus():
                return any(client.get("id") == tool_window.get("id")
                           and client.get("focused") for client in clients())

            wait_for("Layer tool focus", layer_tool_focus)
            checks += 1

            run(wtype, "-s", "300", "-k", "Escape", env=env)
            def one_studio_client():
                found = clients()
                return found if len(found) == 1 else None

            studio_clients = wait_for(
                "Layer tool to close with Escape",
                one_studio_client,
            )
            checks += 1

            def studio_reactivated():
                return any(client.get("id") == studio_clients[0].get("id")
                           and client.get("focused") for client in clients())

            wait_for("Studio focus after closing Layer tool", studio_reactivated)
            checks += 1

            run(
                wtype,
                "-s", "500", "-M", "ctrl", "-k", "k", "-m", "ctrl",
                "-s", "300", "navigate", "-s", "200", "-k", "Return",
                "-s", "300", "2", "-s", "200", "-k", "Down",
                "-s", "100", "-k", "Return",
                env=env,
            )
            wait_for(
                "Layer tool after command-palette keyboard journey",
                lambda: len(clients()) == 2,
            )
            checks += 1
        finally:
            if studio and studio.poll() is None:
                studio.send_signal(signal.SIGTERM)
                try:
                    studio.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    studio.kill()
                    studio.wait()
            if compositor.poll() is None:
                compositor.send_signal(signal.SIGTERM)
                try:
                    compositor.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    compositor.kill()
                    compositor.wait()
            log.close()

    print(f"keyboard-wayland: {checks} checks passed")


if __name__ == "__main__":
    main()
