#!/usr/bin/env python3
"""Tiny executable fixture for Plugin API 1."""

import json
import os
from pathlib import Path
import sys
import time


def emit(record):
    print(json.dumps(record, separators=(",", ":")), flush=True)


def main():
    case_from_argument = sys.argv[1] if len(sys.argv) > 1 else None
    request_line = sys.stdin.readline()
    request = json.loads(request_line)
    request_id = request.get("requestId", "req-1")
    params = request.get("params", [])
    case_from_params = next((item["value"] for item in params if item.get("key") == "case"), "success")
    case = case_from_argument or case_from_params
    output = Path(os.environ.get("OMAPIXEL_PLUGIN_OUTPUT", request.get("outputDir", "output")))

    if case == "malformed":
        print("{malformed", flush=True)
        return 0
    if case == "unexpected-stdout":
        print("plugin diagnostic on stdout", flush=True)
        emit({"type": "result", "requestId": request_id, "ok": True,
              "artifact": "result.bin"})
        return 0
    if case == "duplicate-result":
        emit({"type": "result", "requestId": request_id, "ok": True,
              "artifact": "result.bin"})
        emit({"type": "result", "requestId": request_id, "ok": True,
              "artifact": "result.bin"})
        return 0
    if case == "mismatched-id":
        emit({"type": "result", "requestId": "other-request", "ok": True,
              "artifact": "result.bin"})
        return 0
    if case == "missing-result":
        emit({"type": "progress", "requestId": request_id,
              "message": "waiting"})
        return 0
    if case == "nonzero":
        print("bounded failure diagnostic", file=sys.stderr, flush=True)
        return 7
    if case == "crash":
        os.abort()
    if case == "timeout":
        time.sleep(61)
        return 0
    if case == "oversized-output":
        sys.stdout.write("x" * (1024 * 1024 + 1))
        sys.stdout.flush()
        return 0
    if case == "stderr-oversized":
        sys.stderr.write("x" * (1024 * 1024 + 1))
        sys.stderr.flush()
        return 0

    output.mkdir(parents=True, exist_ok=True)
    if case == "inspect":
        document = Path(request["document"])
        details = {
            "cwd": os.getcwd(),
            "document": request["document"],
            "outputDir": request["outputDir"],
            "params": params,
            "environment": sorted(os.environ),
            "snapshot": json.loads(document.read_text()),
        }
        (output / "result.bin").write_bytes(
            json.dumps(details, separators=(",", ":")).encode()
        )
        emit({"type": "result", "requestId": request_id, "ok": True,
              "artifact": "result.bin"})
        return 0
    if case == "symlink":
        outside = output.parent / "outside.bin"
        outside.write_bytes(b"outside")
        (output / "link.bin").symlink_to(outside)
        artifact = "link.bin"
    elif case == "traversal":
        artifact = "../escape.bin"
    elif case == "absolute":
        artifact = str((output.parent / "escape.bin").resolve())
    elif case == "non-regular":
        (output / "directory").mkdir()
        artifact = "directory"
    elif case == "artifact-oversized":
        artifact_path = output / "large.bin"
        with artifact_path.open("wb") as stream:
            stream.truncate(64 * 1024 * 1024 + 1)
        artifact = "large.bin"
    elif case == "missing-artifact":
        artifact = "missing.bin"
    else:
        (output / "result.bin").write_bytes(b"fixture artifact")
        artifact = "result.bin"

    if case == "diagnostic":
        print("fixture diagnostic", file=sys.stderr, flush=True)
    if case == "progress":
        emit({"type": "progress", "requestId": request_id,
              "message": "encoding", "percent": 50})
    emit({"type": "result", "requestId": request_id, "ok": True,
          "artifact": artifact})
    return 0


if __name__ == "__main__":
    sys.exit(main())
