#!/usr/bin/env python3
"""End-to-end contracts for raster import and animated GIF export."""

import json
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build/bin/omapixel"
ANIMATED = ROOT / "tests/fixtures/format-v2/valid/animated-shared.json"


def run(*arguments: object, expected: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(CLI), *(str(argument) for argument in arguments)],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    assert result.returncode == expected, (
        result.args,
        result.returncode,
        result.stdout,
        result.stderr,
    )
    return result


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))


def write_source(path: Path) -> None:
    pixels = bytes(
        [
            255, 0, 0, 255,
            0, 0, 0, 0,
            0, 255, 0, 255,
            0, 0, 255, 128,
        ]
    )
    scanlines = b"\x00" + pixels[:8] + b"\x00" + pixels[8:]
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 6, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(scanlines))
        + png_chunk(b"IEND", b"")
    )


def main() -> None:
    assert CLI.exists(), "build the project before running this contract"
    with tempfile.TemporaryDirectory(prefix="omapixel-image-io-") as temporary:
        directory = Path(temporary)
        source = directory / "source image.png"
        document = directory / "imported.json"
        layered = directory / "layered.json"
        gif = directory / "animation.gif"
        gif_again = directory / "animation-again.gif"
        once = directory / "animation-once.gif"
        write_source(source)

        run("import-image", source, "-o", document, "--scale", 1)
        imported = json.loads(document.read_text())
        assert imported["canvas"] == {"width": 2, "height": 2}
        assert len(imported["palette"]) == 3
        assert imported["layers"][0]["cels"][0]["rows"][0][1] == "."
        run("check", document)

        run(
            "import-image",
            source,
            "--into",
            document,
            "-o",
            layered,
            "--resolution",
            "1x1",
            "--fit",
            "cover",
            "--layer-name",
            "Overlay",
        )
        assert len(json.loads(layered.read_text())["layers"]) == 2
        run(
            "import-image",
            source,
            "-o",
            directory / "invalid.json",
            "--scale",
            2,
            "--resolution",
            "1x1",
            expected=2,
        )
        run("import-image", source, directory / "extra.png", "-o",
            directory / "extra.json", expected=2)
        run("import-image", source, "-o", directory / "no-op-fit.json",
            "--scale", 2, "--fit", "contain", expected=2)

        run("render", ANIMATED, "-o", gif, "--format", "gif", "--clip", "Idle",
            "--scale", 2, "--fps", 12, "--loop")
        run("render", ANIMATED, "-o", gif_again, "--format", "gif", "--clip", "Idle",
            "--scale", 2, "--fps", 12, "--loop")
        encoded = gif.read_bytes()
        assert encoded == gif_again.read_bytes()
        assert encoded.startswith(b"GIF89a") and encoded.endswith(b"\x3b")
        assert encoded.count(b"\x21\xf9\x04") == 2
        assert encoded.count(b"\x21\xf9\x04\x09") == 2
        assert b"NETSCAPE2.0" in encoded

        run("render", ANIMATED, "-o", once, "--format", "gif", "--clip", "Idle",
            "--no-loop")
        assert b"NETSCAPE2.0" not in once.read_bytes()

    print("image import and GIF export contracts passed")


if __name__ == "__main__":
    main()
