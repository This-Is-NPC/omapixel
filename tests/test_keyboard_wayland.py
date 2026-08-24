#!/usr/bin/env python3
"""Keyboard-only Studio smoke test through a real headless Hyprland session."""

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
    hyprland = require("Hyprland")
    hyprctl = require("hyprctl")
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
        config = root / "hyprland.lua"
        config.write_text(
            'hl.monitor({ output = "WL-1", mode = "1280x720@60", '
            'position = "0x0", scale = 1 })\n',
            encoding="utf-8",
        )
        log = (root / "hyprland.log").open("w+", encoding="utf-8")
        parent_compositor = None
        parent_log = None
        parent_display = os.environ.get("WAYLAND_DISPLAY", "")
        parent_runtime = Path(os.environ.get("XDG_RUNTIME_DIR", "/nonexistent"))
        parent_socket = Path(parent_display) if parent_display.startswith("/") else parent_runtime / parent_display
        if not parent_display or not parent_socket.is_socket():
            weston = require("weston")
            parent_display = "parent-wayland"
            parent_log = (root / "weston.log").open("w+", encoding="utf-8")
            parent_env = {**os.environ, "HOME": str(home), "XDG_RUNTIME_DIR": str(runtime)}
            parent_compositor = subprocess.Popen(
                [weston, "--backend=headless-backend.so", "--renderer=gl",
                 f"--socket={parent_display}", "--idle-time=0"],
                cwd=ROOT,
                env=parent_env,
                stdout=parent_log,
                stderr=subprocess.STDOUT,
                text=True,
            )

            def parent_ready():
                if parent_compositor.poll() is not None:
                    parent_log.seek(0)
                    raise RuntimeError(parent_log.read())
                return (runtime / parent_display).is_socket()

            try:
                wait_for("headless Weston parent", parent_ready)
            except Exception:
                if parent_compositor.poll() is None:
                    parent_compositor.terminate()
                    try:
                        parent_compositor.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        parent_compositor.kill()
                        parent_compositor.wait()
                parent_log.close()
                raise
        else:
            parent_display = str(parent_socket)

        compositor_env = {
            **os.environ,
            "HOME": str(home),
            "XDG_RUNTIME_DIR": str(runtime),
            "WAYLAND_DISPLAY": parent_display,
            "AQ_NO_MODIFIERS": "1",
            "LIBGL_ALWAYS_SOFTWARE": "1",
        }
        compositor_command = [hyprland, "--config", str(config)]
        if os.geteuid() == 0:
            compositor_command.append("--i-am-really-stupid")
        compositor = subprocess.Popen(
            compositor_command,
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
                instances = list((runtime / "hypr").glob("*/.socket.sock"))
                displays = [path for path in runtime.glob("wayland-*")
                            if not path.name.endswith(".lock")]
                if not instances or not displays:
                    return None
                return {
                    **compositor_env,
                    "HYPRLAND_INSTANCE_SIGNATURE": instances[0].parent.name,
                    "WAYLAND_DISPLAY": displays[0].name,
                    "QT_QPA_PLATFORM": "wayland",
                    "QT_QPA_PLATFORMTHEME": "",
                    "OMAPIXEL_CONFIG_PATH": str(root / "missing-config.toml"),
                }

            env = wait_for("headless Hyprland sockets", desktop_environment)

            def monitor_ready():
                result = run(hyprctl, "-j", "monitors", "all", env=env, check=False)
                return json.loads(result.stdout) if result.returncode == 0 else None

            wait_for("nested Hyprland monitor", monitor_ready, timeout=15)
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

            def clients():
                result = run(hyprctl, "-j", "clients", env=env)
                return json.loads(result.stdout)

            def studio_client():
                if studio.poll() is not None:
                    studio_log.seek(0)
                    raise RuntimeError(studio_log.read())
                return next(
                    (client for client in clients()
                     if client.get("class") == "omapixel-studio"),
                    None,
                )

            try:
                studio_window = wait_for("Studio client", studio_client)
            except AssertionError as error:
                studio_log.seek(0)
                monitors = run(hyprctl, "-j", "monitors", "all", env=env, check=False)
                raise AssertionError(
                    f"{error}\nmonitors: {monitors.stdout}\n"
                    f"hyprctl errors: {monitors.stderr}\nstudio: {studio_log.read()}"
                ) from error
            checks += 1

            run(
                hyprctl,
                "dispatch",
                'hl.dsp.focus({ window = hl.get_window("class:omapixel-studio") })',
                env=env,
            )

            def initial_studio_focus():
                active = run(hyprctl, "-j", "activewindow", env=env)
                return json.loads(active.stdout).get("address") == studio_window.get("address")

            wait_for("initial Studio focus", initial_studio_focus)
            checks += 1

            run(
                wtype,
                "-M", "ctrl", "-k", "k", "-m", "ctrl", "-s", "300",
                "navigate", "-s", "200", "-k", "Return", "-s", "300",
                "2", "-s", "200", "-k", "Down", "-s", "100", "-k", "Return",
                env=env,
            )
            try:
                def two_studio_clients():
                    found = [client for client in clients()
                             if client.get("class") == "omapixel-studio"]
                    return found if len(found) == 2 else None

                opened_clients = wait_for(
                    "Layer tool after command-palette keyboard journey",
                    two_studio_clients,
                )
            except AssertionError as error:
                active = run(hyprctl, "-j", "activewindow", env=env, check=False)
                raise AssertionError(
                    f"{error}\nactive: {active.stdout}\nclients: {json.dumps(clients())}"
                ) from error
            checks += 1

            tool_window = next(client for client in opened_clients
                               if client.get("address") != studio_window.get("address"))

            def layer_tool_focus():
                active = run(hyprctl, "-j", "activewindow", env=env)
                return json.loads(active.stdout).get("address") == tool_window.get("address")

            wait_for("Layer tool focus", layer_tool_focus)
            checks += 1

            run(wtype, "-s", "300", "-k", "Escape", env=env)
            def one_studio_client():
                found = [client for client in clients()
                         if client.get("class") == "omapixel-studio"]
                return found if len(found) == 1 else None

            studio_clients = wait_for(
                "Layer tool to close with Escape",
                one_studio_client,
            )
            checks += 1

            def studio_reactivated():
                active = run(hyprctl, "-j", "activewindow", env=env)
                return json.loads(active.stdout).get("address") == studio_clients[0].get("address")

            wait_for("Studio focus after closing Layer tool", studio_reactivated)
            checks += 1

            run(wtype, "-M", "ctrl", "-M", "shift", "-k", "l",
                "-m", "shift", "-m", "ctrl", env=env)
            wait_for(
                "Layer tool after direct shortcut",
                lambda: len([client for client in clients()
                             if client.get("class") == "omapixel-studio"]) == 2,
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
            if parent_compositor and parent_compositor.poll() is None:
                parent_compositor.send_signal(signal.SIGTERM)
                try:
                    parent_compositor.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    parent_compositor.kill()
                    parent_compositor.wait()
            if parent_log:
                parent_log.close()
            log.close()

    print(f"keyboard-wayland: {checks} checks passed")


if __name__ == "__main__":
    main()
