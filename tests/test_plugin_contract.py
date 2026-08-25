#!/usr/bin/env python3
"""Executable checks for the Plugin API 1 contract."""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from test_format_v2 import ContractError, validate_document


ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests" / "fixtures" / "plugin"
MANIFEST = FIXTURES / "valid" / "omapixel-plugin.json"
SCHEMA = ROOT / "docs" / "plugin-manifest.schema.json"
PLUGIN = FIXTURES / "plugin_fixture.py"
DOCUMENT = ROOT / "tests" / "fixtures" / "format-v2" / "valid" / "minimal.json"
ID = re.compile(r"^[a-z][a-z0-9-]{0,63}$")
SAFE_PATH = re.compile(r"^[^/\\\x00-\x1f\x7f-\x9f]+(?:/[^/\\\x00-\x1f\x7f-\x9f]+)*$")
MAX_STDOUT = 1024 * 1024
MAX_STDERR = 1024 * 1024
MAX_ARTIFACT = 64 * 1024 * 1024
EXECUTION_TIMEOUT = 60


def integer(value):
    return isinstance(value, int) and not isinstance(value, bool)


def fail(path, message):
    raise ContractError(path, message)


def object_with_fields(value, allowed, path):
    if not isinstance(value, dict):
        fail(path, "must be an object")
    unknown = sorted(set(value) - set(allowed))
    if unknown:
        fail(f"{path}.{unknown[0]}", "unknown field")


def required(obj, key, path):
    if key not in obj:
        fail(f"{path}.{key}", "is required")
    return obj[key]


def check_id(value, path):
    if not isinstance(value, str) or not ID.fullmatch(value):
        fail(path, "must match [a-z][a-z0-9-]{0,63}")


def check_name(value, path):
    if not isinstance(value, str) or not 1 <= len(value) <= 128:
        fail(path, "must be a non-empty string of at most 128 characters")


def check_relative_path(value, path):
    if not isinstance(value, str) or not 1 <= len(value) <= 255:
        fail(path, "must be a relative path of at most 255 characters")
    if not SAFE_PATH.fullmatch(value) or any(part in {".", ".."} for part in value.split("/")):
        fail(path, "must be a safe slash-separated relative path")


def validate_manifest(manifest, plugin_root):
    fields = {"schemaVersion", "id", "name", "version", "pluginApi", "executable", "actions"}
    object_with_fields(manifest, fields, "$")
    schema_version = required(manifest, "schemaVersion", "$")
    if schema_version != 1 or not integer(schema_version):
        fail("$.schemaVersion", "must be exactly integer 1")
    check_id(required(manifest, "id", "$"), "$.id")
    check_name(required(manifest, "name", "$"), "$.name")
    version = required(manifest, "version", "$")
    if not isinstance(version, str) or not 1 <= len(version) <= 128:
        fail("$.version", "must be a non-empty string of at most 128 characters")
    plugin_api = required(manifest, "pluginApi", "$")
    if plugin_api != 1 or not integer(plugin_api):
        fail("$.pluginApi", "must be exactly integer 1")
    executable = required(manifest, "executable", "$")
    check_relative_path(executable, "$.executable")
    executable_path = plugin_root / executable
    root = plugin_root.resolve()
    try:
        executable_path.resolve().relative_to(root)
    except ValueError:
        fail("$.executable", "must remain inside the plugin root")
    path_part = plugin_root
    for part in Path(executable).parts:
        path_part /= part
        if path_part.is_symlink():
            fail("$.executable", "must not traverse a symlink")
    if executable_path.is_symlink() or not executable_path.is_file() or not os.access(executable_path, os.X_OK):
        fail("$.executable", "must name a regular executable file, not a symlink")

    actions = required(manifest, "actions", "$")
    if not isinstance(actions, list) or not actions:
        fail("$.actions", "must contain at least one action")
    names = set()
    for index, action in enumerate(actions):
        path = f"$.actions[{index}]"
        object_with_fields(action, {"name", "kind"}, path)
        name = required(action, "name", path)
        check_name(name, f"{path}.name")
        if name in names:
            fail(f"{path}.name", f"duplicates action name `{name}`")
        names.add(name)
        if required(action, "kind", path) != "export":
            fail(f"{path}.kind", "must be exactly `export`")


def validate_request(request):
    object_with_fields(request, {"type", "requestId", "action", "document", "outputDir", "params"}, "$request")
    if required(request, "type", "$request") != "request":
        fail("$request.type", "must be exactly `request`")
    request_id = required(request, "requestId", "$request")
    if not isinstance(request_id, str) or not 1 <= len(request_id) <= 64 or not request_id.isascii():
        fail("$request.requestId", "must be 1 through 64 ASCII characters")
    check_name(required(request, "action", "$request"), "$request.action")
    if required(request, "document", "$request") != "input/document.json":
        fail("$request.document", "must be input/document.json")
    if required(request, "outputDir", "$request") != "output":
        fail("$request.outputDir", "must be output")
    params = required(request, "params", "$request")
    if not isinstance(params, list):
        fail("$request.params", "must be an array")
    for index, param in enumerate(params):
        path = f"$request.params[{index}]"
        object_with_fields(param, {"key", "value"}, path)
        for key in ("key", "value"):
            value = required(param, key, path)
            if not isinstance(value, str) or not 1 <= len(value) <= 128:
                fail(f"{path}.{key}", "must be a non-empty string of at most 128 characters")


def validate_artifact(artifact, output_dir):
    check_relative_path(artifact, "$result.artifact")
    output = output_dir.resolve()
    candidate = output_dir / artifact
    try:
        candidate.resolve().relative_to(output)
    except ValueError:
        fail("$result.artifact", "must remain inside output")
    if candidate.is_symlink() or not candidate.is_file():
        fail("$result.artifact", "must name one regular non-symlink file")
    if candidate.stat().st_size > MAX_ARTIFACT:
        fail("$result.artifact", "exceeds the 64 MiB limit")


def validate_records(stdout, request_id, output_dir):
    if len(stdout) > MAX_STDOUT:
        fail("stdout", "exceeds the 1 MiB protocol budget")
    records = []
    terminal_seen = False
    for line_number, line in enumerate(stdout.splitlines(), 1):
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            fail(f"stdout[{line_number}]", f"is not JSON: {error.msg}")
        if not isinstance(record, dict):
            fail(f"stdout[{line_number}]", "must be a JSON object")
        if record.get("requestId") != request_id:
            fail(f"stdout[{line_number}].requestId", "does not match the request")
        if terminal_seen:
            fail(f"stdout[{line_number}]", "appears after the terminal result")
        record_type = record.get("type")
        if record_type == "progress":
            object_with_fields(record, {"type", "requestId", "message", "percent"}, f"stdout[{line_number}]")
            if not isinstance(record.get("message"), str) or not 1 <= len(record["message"]) <= 256:
                fail(f"stdout[{line_number}].message", "must be a non-empty string of at most 256 characters")
            if "percent" in record and (not integer(record["percent"]) or not 0 <= record["percent"] <= 100):
                fail(f"stdout[{line_number}].percent", "must be an integer from 0 through 100")
        elif record_type == "result":
            if record.get("ok") is True:
                object_with_fields(record, {"type", "requestId", "ok", "artifact"}, f"stdout[{line_number}]")
                if not isinstance(record.get("artifact"), str):
                    fail(f"stdout[{line_number}].artifact", "must be a string")
                validate_artifact(record["artifact"], output_dir)
            elif record.get("ok") is False:
                object_with_fields(record, {"type", "requestId", "ok", "error"}, f"stdout[{line_number}]")
                if not isinstance(record.get("error"), str) or not 1 <= len(record["error"]) <= 1024:
                    fail(f"stdout[{line_number}].error", "must be a non-empty string of at most 1024 characters")
            else:
                fail(f"stdout[{line_number}].ok", "must be boolean")
            records.append(record)
            terminal_seen = True
        else:
            fail(f"stdout[{line_number}].type", "must be progress or result")
    if len(records) != 1:
        fail("stdout", "must contain exactly one terminal result")
    return records[0]


def validate_diagnostics(stderr):
    if len(stderr) > MAX_STDERR:
        fail("stderr", "exceeds the 1 MiB diagnostics budget")


def request():
    return {
        "type": "request",
        "requestId": "req-1",
        "action": "png",
        "document": "input/document.json",
        "outputDir": "output",
        "params": [{"key": "scale", "value": "2"}, {"key": "scale", "value": "4"}],
    }


def run_fixture(case, workspace, timeout=EXECUTION_TIMEOUT):
    input_dir = workspace / "input"
    output_dir = workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()
    shutil.copyfile(DOCUMENT, input_dir / "document.json")
    encoded = json.dumps(request(), separators=(",", ":")) + "\n"
    environment = os.environ.copy()
    environment["OMAPIXEL_PLUGIN_OUTPUT"] = str(output_dir)
    process = subprocess.Popen([str(PLUGIN), case], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, env=environment)
    try:
        stdout, stderr = process.communicate(encoded.encode(), timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        stdout, stderr = process.communicate()
        return None, stdout, stderr, "timeout"
    return process.returncode, stdout, stderr, None


class ManifestContractTest(unittest.TestCase):
    def test_published_schema_requires_exact_integer_versions(self):
        with SCHEMA.open() as stream:
            schema = json.load(stream)
        properties = schema["properties"]
        for field in ("schemaVersion", "pluginApi"):
            with self.subTest(field=field):
                self.assertEqual(properties[field], {"type": "integer", "const": 1})

    def test_schema_documents_numeric_limit_without_claiming_lexical_validation(self):
        with SCHEMA.open() as stream:
            schema = json.load(stream)
        self.assertIn("1.0", schema["$comment"])
        self.assertEqual(json.loads("1.0"), json.loads("1"))

    def test_published_schema_rejects_duplicate_action_names_like_runtime(self):
        with SCHEMA.open() as stream:
            schema = json.load(stream)
        actions_schema = schema["properties"]["actions"]
        self.assertTrue(actions_schema["uniqueItems"])

        with MANIFEST.open() as stream:
            manifest = json.load(stream)
        duplicate = dict(manifest)
        duplicate["actions"] = [dict(manifest["actions"][0]), dict(manifest["actions"][0])]
        with self.assertRaises(ContractError):
            validate_manifest(duplicate, FIXTURES)

    def test_valid_manifest_and_invalid_fixture_files(self):
        with MANIFEST.open() as stream:
            manifest = json.load(stream)
        validate_manifest(manifest, FIXTURES)
        for path in (FIXTURES / "invalid").glob("*.json"):
            with self.subTest(path=path.name), path.open() as stream:
                with self.assertRaises(ContractError):
                    validate_manifest(json.load(stream), path.parent.parent)

    def test_every_required_manifest_field_is_required_and_typed(self):
        with MANIFEST.open() as stream:
            valid = json.load(stream)
        required_fields = ("schemaVersion", "id", "name", "version", "pluginApi", "executable", "actions")
        for field in required_fields:
            missing = dict(valid)
            del missing[field]
            with self.subTest(missing=field), self.assertRaises(ContractError):
                validate_manifest(missing, FIXTURES)
        for field, value in {
            "schemaVersion": "1", "id": 1, "name": 1, "version": 1,
            "pluginApi": "1", "executable": 1, "actions": {},
        }.items():
            typed = dict(valid)
            typed[field] = value
            with self.subTest(field=field), self.assertRaises(ContractError):
                validate_manifest(typed, FIXTURES)

    def test_manifest_rules_reject_unsupported_values_and_paths(self):
        with MANIFEST.open() as stream:
            valid = json.load(stream)
        for value in (0, 2, True):
            candidate = dict(valid)
            candidate["schemaVersion"] = value
            with self.assertRaises(ContractError):
                validate_manifest(candidate, FIXTURES)
        for value in ("", "A", "a_", "a/", "a" * 65):
            candidate = dict(valid)
            candidate["id"] = value
            with self.assertRaises(ContractError):
                validate_manifest(candidate, FIXTURES)
        for value in ("", "v" * 129):
            candidate = dict(valid)
            candidate["version"] = value
            with self.assertRaises(ContractError):
                validate_manifest(candidate, FIXTURES)
        for value in ("/bin/export", "../export", "a/../export", "./export", "a//export", "a\\export", "a\x00export"):
            candidate = dict(valid)
            candidate["executable"] = value
            with self.assertRaises(ContractError):
                validate_manifest(candidate, FIXTURES)
        for kind in ("import", "transform", ""):
            candidate = dict(valid)
            candidate["actions"] = [{"name": "png", "kind": kind}]
            with self.assertRaises(ContractError):
                validate_manifest(candidate, FIXTURES)
        candidate = dict(valid)
        candidate["actions"] = [{"name": "png", "kind": "export"}, {"name": "png", "kind": "export"}]
        with self.assertRaises(ContractError):
            validate_manifest(candidate, FIXTURES)

    def test_executable_must_be_regular_and_executable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = {"schemaVersion": 1, "id": "plugin", "name": "Plugin", "version": "v",
                        "pluginApi": 1, "executable": "run", "actions": [{"name": "x", "kind": "export"}]}
            target = root / "run"
            target.write_text("#!/bin/sh\n")
            target.chmod(0o644)
            with self.assertRaises(ContractError):
                validate_manifest(manifest, root)
            target.chmod(0o755)
            link = root / "link"
            link.symlink_to(target)
            manifest["executable"] = "link"
            with self.assertRaises(ContractError):
                validate_manifest(manifest, root)


class ProtocolContractTest(unittest.TestCase):
    def test_request_is_compact_v2_snapshot_and_preserves_string_repeats(self):
        current = request()
        validate_request(current)
        self.assertNotIn("out", current)
        with DOCUMENT.open() as stream:
            validate_document(json.load(stream))
        self.assertEqual(current["params"][0]["key"], current["params"][1]["key"])
        encoded = json.dumps(current, separators=(",", ":"))
        self.assertIn('"type":"request"', encoded)
        self.assertNotIn('"type": "request"', encoded)

    def test_success_and_progress_have_one_result_and_echo_request_id(self):
        for case in ("success", "progress"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as directory:
                workspace = Path(directory)
                code, stdout, stderr, timed_out = run_fixture(case, workspace)
                self.assertEqual(code, 0)
                self.assertEqual(timed_out, None)
                self.assertLessEqual(len(stdout), MAX_STDOUT)
                validate_diagnostics(stderr)
                result = validate_records(stdout, "req-1", workspace / "output")
                self.assertTrue(result["ok"])

    def test_protocol_rejects_malformed_unexpected_duplicate_and_missing_records(self):
        for case in ("malformed", "unexpected-stdout", "duplicate-result", "missing-result", "mismatched-id"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as directory:
                workspace = Path(directory)
                code, stdout, _, _ = run_fixture(case, workspace)
                self.assertEqual(code, 0)
                with self.assertRaises(ContractError):
                    validate_records(stdout, "req-1", workspace / "output")

    def test_process_failures_and_protocol_budgets_are_bounded(self):
        self.assertEqual(EXECUTION_TIMEOUT, 60)
        for case in ("nonzero", "crash"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as directory:
                code, _, stderr, _ = run_fixture(case, Path(directory))
                self.assertNotEqual(code, 0)
                self.assertLessEqual(len(stderr), MAX_STDERR)
        with tempfile.TemporaryDirectory() as directory:
            code, _, _, timed_out = run_fixture("timeout", Path(directory), timeout=0.1)
            self.assertIsNone(code)
            self.assertEqual(timed_out, "timeout")
        for case, budget in (("oversized-output", MAX_STDOUT), ("stderr-oversized", MAX_STDERR)):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as directory:
                _, stdout, stderr, _ = run_fixture(case, Path(directory))
                if case == "oversized-output":
                    self.assertGreater(len(stdout), budget)
                    with self.assertRaises(ContractError):
                        validate_records(stdout, "req-1", Path(directory) / "output")
                else:
                    self.assertGreater(len(stderr), budget)
                    with self.assertRaises(ContractError):
                        validate_diagnostics(stderr)

    def test_artifact_must_be_one_safe_regular_file_within_size_limit(self):
        for case in ("traversal", "symlink", "artifact-oversized"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as directory:
                workspace = Path(directory)
                code, stdout, _, _ = run_fixture(case, workspace)
                self.assertEqual(code, 0)
                with self.assertRaises(ContractError):
                    validate_records(stdout, "req-1", workspace / "output")


if __name__ == "__main__":
    unittest.main(verbosity=2)
