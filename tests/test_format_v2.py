#!/usr/bin/env python3
"""Executable, dependency-free checks for the format v2 contract."""

import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests" / "fixtures" / "format-v2"
ID = re.compile(r"^[a-z][a-z0-9-]{0,63}$")
RGBA = re.compile(r"^#[0-9A-Fa-f]{8}$")


class ContractError(ValueError):
    def __init__(self, path, message):
        super().__init__(f"{path}: {message}")
        self.path = path


def fail(path, message):
    raise ContractError(path, message)


def integer(value):
    return isinstance(value, int) and not isinstance(value, bool)


def field(obj, name, path):
    if name not in obj:
        fail(f"{path}.{name}", "is required")
    return obj[name]


def object_with_fields(value, allowed, path):
    if not isinstance(value, dict):
        fail(path, "must be an object")
    unknown = sorted(set(value) - set(allowed))
    if unknown:
        fail(f"{path}.{unknown[0]}", "unknown field")


def check_id(value, path):
    if not isinstance(value, str) or not ID.fullmatch(value):
        fail(path, "must match [a-z][a-z0-9-]{0,63}")


def check_name(value, path):
    if not isinstance(value, str) or not 1 <= len(value) <= 128:
        fail(path, "must be a non-empty string of at most 128 characters")


def check_rows(rows, width, height, palette, path):
    if not isinstance(rows, list) or len(rows) != height:
        fail(path, f"must contain exactly {height} rows")
    for y, row in enumerate(rows):
        row_path = f"{path}[{y}]"
        if not isinstance(row, str) or len(row) != width:
            fail(row_path, f"must contain exactly {width} characters")
        for x, slot in enumerate(row):
            if slot != "." and slot not in palette:
                fail(f"{row_path}[{x}]", f"uses undefined palette slot `{slot}`")


def validate_document(document):
    object_with_fields(document, {"version", "canvas", "palette", "clips", "layers"}, "$")
    if document.get("version") != 2:
        fail("$.version", "must be exactly 2")

    canvas = field(document, "canvas", "$")
    object_with_fields(canvas, {"width", "height"}, "$.canvas")
    for dimension in ("width", "height"):
        value = field(canvas, dimension, "$.canvas")
        if not integer(value) or not 1 <= value <= 2048:
            fail(f"$.canvas.{dimension}", "must be an integer from 1 through 2048")
    width, height = canvas["width"], canvas["height"]

    palette_value = field(document, "palette", "$")
    if not isinstance(palette_value, list):
        fail("$.palette", "must be an array")
    palette = set()
    if len(palette_value) > 256:
        fail("$.palette", "has more than 256 slots")
    for index, entry in enumerate(palette_value):
        path = f"$.palette[{index}]"
        object_with_fields(entry, {"slot", "colour"}, path)
        slot = field(entry, "slot", path)
        if (not isinstance(slot, str) or len(slot) != 1 or slot in '."\\'
                or ord(slot) < 0x20 or 0x7f <= ord(slot) <= 0x9f):
            fail(f"{path}.slot", "must be one character and not `.`, `\"`, or `\\`")
        if slot in palette:
            fail(f"{path}.slot", f"duplicates palette slot `{slot}`")
        colour = field(entry, "colour", path)
        if not isinstance(colour, str) or not RGBA.fullmatch(colour):
            fail(f"{path}.colour", "must be #RRGGBBAA")
        palette.add(slot)

    clips_value = field(document, "clips", "$")
    if not isinstance(clips_value, list) or not clips_value:
        fail("$.clips", "must not be empty")
    clip_ids, clip_names, clips = set(), set(), {}
    for index, clip in enumerate(clips_value):
        path = f"$.clips[{index}]"
        object_with_fields(clip, {"id", "name", "fps", "frameCount"}, path)
        clip_id = field(clip, "id", path)
        check_id(clip_id, f"{path}.id")
        if clip_id in clip_ids:
            fail(f"{path}.id", f"duplicates clip id `{clip_id}`")
        name = field(clip, "name", path)
        check_name(name, f"{path}.name")
        if name in clip_names:
            fail(f"{path}.name", f"duplicates clip name `{name}`")
        fps = field(clip, "fps", path)
        if not integer(fps) or not 1 <= fps <= 60:
            fail(f"{path}.fps", "must be an integer from 1 through 60")
        frame_count = field(clip, "frameCount", path)
        if not integer(frame_count) or frame_count < 1:
            fail(f"{path}.frameCount", "must be a positive integer")
        clip_ids.add(clip_id)
        clip_names.add(name)
        clips[clip_id] = frame_count

    layers_value = field(document, "layers", "$")
    if not isinstance(layers_value, list) or not layers_value:
        fail("$.layers", "must not be empty")
    layer_ids, layer_names = set(), set()
    for index, layer in enumerate(layers_value):
        path = f"$.layers[{index}]"
        object_with_fields(layer, {"id", "name", "visible", "locked", "opacity", "mode", "storage", "cels"}, path)
        layer_id = field(layer, "id", path)
        check_id(layer_id, f"{path}.id")
        if layer_id in layer_ids:
            fail(f"{path}.id", f"duplicates layer id `{layer_id}`")
        name = field(layer, "name", path)
        check_name(name, f"{path}.name")
        if name in layer_names:
            fail(f"{path}.name", f"duplicates layer name `{name}`")
        for boolean in ("visible", "locked"):
            if not isinstance(field(layer, boolean, path), bool):
                fail(f"{path}.{boolean}", "must be boolean")
        opacity = field(layer, "opacity", path)
        if not integer(opacity) or not 0 <= opacity <= 255:
            fail(f"{path}.opacity", "must be an integer from 0 through 255")
        if field(layer, "mode", path) not in {"normal", "multiply", "screen"}:
            fail(f"{path}.mode", "must be normal, multiply, or screen")
        storage = field(layer, "storage", path)
        if storage not in {"shared", "animated"}:
            fail(f"{path}.storage", "must be shared or animated")
        cels = field(layer, "cels", path)
        if not isinstance(cels, list):
            fail(f"{path}.cels", "must be an array")
        expected = sum(clips.values())
        if storage == "shared" and len(cels) != 1:
            fail(f"{path}.cels", "shared storage requires exactly one cel")
        if storage == "animated" and len(cels) != expected:
            fail(f"{path}.cels", f"animated storage requires exactly {expected} cels")

        seen = set()
        for cel_index, cel in enumerate(cels):
            cel_path = f"{path}.cels[{cel_index}]"
            if storage == "shared":
                object_with_fields(cel, {"scope", "rows"}, cel_path)
                if cel.get("scope") != "all":
                    fail(f"{cel_path}.scope", "shared cel scope must be `all`")
            else:
                object_with_fields(cel, {"clip", "frame", "rows"}, cel_path)
                clip_id = field(cel, "clip", cel_path)
                check_id(clip_id, f"{cel_path}.clip")
                frame = field(cel, "frame", cel_path)
                if clip_id not in clips:
                    fail(f"{cel_path}.clip", f"unknown clip `{clip_id}`")
                if not integer(frame) or not 0 <= frame < clips[clip_id]:
                    fail(f"{cel_path}.frame", "is outside the clip frame range")
                key = (clip_id, frame)
                if key in seen:
                    fail(f"{cel_path}.frame", "duplicates an animated cel")
                seen.add(key)
            check_rows(field(cel, "rows", cel_path), width, height, palette, f"{cel_path}.rows")
        if storage == "animated":
            expected_keys = {(clip_id, frame) for clip_id, count in clips.items() for frame in range(count)}
            if seen != expected_keys:
                fail(f"{path}.cels", "must cover every clip/frame pair exactly once")
        layer_ids.add(layer_id)
        layer_names.add(name)


def validate_session(session):
    object_with_fields(session, {"version", "pid", "started", "path", "dirty", "view", "selection"}, "$session")
    if session.get("version") != 2:
        fail("$session.version", "must be exactly 2")
    for key in ("pid", "started"):
        value = field(session, key, "$session")
        if not integer(value) or value <= 0:
            fail(f"$session.{key}", "must be a positive integer")
    if not isinstance(field(session, "path", "$session"), str):
        fail("$session.path", "must be a string")
    if not isinstance(field(session, "dirty", "$session"), bool):
        fail("$session.dirty", "must be boolean")
    view = field(session, "view", "$session")
    object_with_fields(view, {"clip", "frame", "layerId", "layerName", "scope"}, "$session.view")
    for key in ("clip", "layerId", "layerName"):
        if not isinstance(field(view, key, "$session.view"), str) or not view[key]:
            fail(f"$session.view.{key}", "must be a non-empty string")
    if not integer(field(view, "frame", "$session.view")) or view["frame"] < 0:
        fail("$session.view.frame", "must be a non-negative integer")
    if view["scope"] not in ("frame", "all-frames"):
        fail("$session.view.scope", "must be `frame` or `all-frames`")
    selection = field(session, "selection", "$session")
    if selection is None:
        return
    object_with_fields(selection, {"clip", "frame", "layerId", "layerName", "x", "y", "width", "height", "count"}, "$session.selection")
    for key in ("clip", "layerId", "layerName"):
        if not isinstance(field(selection, key, "$session.selection"), str):
            fail(f"$session.selection.{key}", "must be a stable ID")
    for key in ("frame", "x", "y", "width", "height", "count"):
        value = field(selection, key, "$session.selection")
        if not integer(value) or value < 0:
            fail(f"$session.selection.{key}", "must be a non-negative integer")
    if selection["width"] == 0 or selection["height"] == 0:
        fail("$session.selection", "selection dimensions must be positive")


def round255(value):
    return (value + 127) // 255


def source_over(destination, source, layer_opacity=255):
    source_alpha = round255(source[3] * layer_opacity)
    destination_alpha = destination[3]
    source_premultiplied = [round255(channel * source_alpha) for channel in source[:3]]
    destination_premultiplied = [round255(channel * destination_alpha) for channel in destination[:3]]
    output_alpha = source_alpha + round255(destination_alpha * (255 - source_alpha))
    output_premultiplied = [
        source_premultiplied[index]
        + round255(destination_premultiplied[index] * (255 - source_alpha))
        for index in range(3)
    ]
    if output_alpha == 0:
        return (0, 0, 0, 0)
    output = tuple(
        min(255, (value * 255 + output_alpha // 2) // output_alpha)
        for value in output_premultiplied
    )
    return output + (output_alpha,)


def quantize(pixel, palette):
    if pixel[3] == 0:
        return "."
    candidates = []
    for index, (slot, colour) in enumerate(palette):
        distance = sum((pixel[channel] - colour[channel]) ** 2 for channel in range(4))
        candidates.append((distance, index, slot))
    return min(candidates)[2]


class FormatV2Test(unittest.TestCase):
    def load(self, relative):
        with (FIXTURES / relative).open(encoding="utf-8") as stream:
            return json.load(stream)

    def test_schema_declares_v2_root_and_semantic_fields(self):
        with (ROOT / "docs" / "format-v2.schema.json").open(encoding="utf-8") as stream:
            schema = json.load(stream)
        self.assertEqual(schema["$id"], "https://omapixel.org/schemas/format-v2.schema.json")
        self.assertEqual(schema["properties"]["version"]["const"], 2)
        self.assertEqual(schema["properties"]["layers"]["items"]["$ref"], "#/$defs/layer")

    def test_canonical_valid_documents(self):
        for relative in ("valid/minimal.json", "valid/animated-shared.json"):
            with self.subTest(relative=relative):
                validate_document(self.load(relative))

    def test_canonical_invalid_documents_report_exact_paths(self):
        expected = {
            "unsupported-version.json": "$.version",
            "malformed-layer-id.json": "$.layers[0].id",
            "duplicate-layer-id.json": "$.layers[1].id",
            "duplicate-layer-name.json": "$.layers[1].name",
            "invalid-opacity.json": "$.layers[0].opacity",
            "wrong-shared-cel-count.json": "$.layers[0].cels",
            "wrong-animated-cel-count.json": "$.layers[0].cels",
        }
        for filename, path in expected.items():
            with self.subTest(filename=filename):
                with self.assertRaises(ContractError) as raised:
                    validate_document(self.load(f"invalid/{filename}"))
                self.assertEqual(raised.exception.path, path)

    def test_session_fixture_has_stable_layer_selection(self):
        validate_session(self.load("session-valid.json"))

    def test_integer_source_over_is_exact(self):
        self.assertEqual(
            source_over((0, 0, 255, 255), (255, 0, 0, 128), 128),
            (64, 0, 191, 255),
        )

    def test_palette_distance_uses_array_order_for_ties(self):
        palette = [("black", (0, 0, 0, 255)), ("white", (254, 254, 254, 255))]
        self.assertEqual(quantize((127, 127, 127, 255), palette), "black")
        self.assertEqual(quantize((0, 0, 0, 0), palette), ".")

    def test_migration_inventory_names_every_tracked_document(self):
        text = (ROOT / "docs" / "format-v2.md").read_text(encoding="utf-8")
        for path in (
            "heart.json",
            "examples/heart.json",
            "examples/last-horizon.json",
            "examples/last-horizon-omarchy.json",
            "packaging/icon/omapixel.json",
            "examples/last-horizon-omarchy.batch",
            "packaging/icon/omapixel.batch",
        ):
            self.assertIn(f"`{path}`", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
