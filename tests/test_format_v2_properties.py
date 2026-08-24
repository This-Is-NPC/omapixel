#!/usr/bin/env python3
"""Deterministic property checks for every bounded v2 schema domain."""

import copy
import random
import unittest

from test_format_v2 import ContractError, validate_document


def base_document():
    return {
        "version": 2,
        "canvas": {"width": 2, "height": 2},
        "palette": [{"slot": "A", "colour": "#112233FF"}],
        "clips": [{"id": "idle", "name": "Idle", "fps": 8, "frameCount": 2}],
        "layers": [{
            "id": "layer",
            "name": "Layer",
            "visible": True,
            "locked": False,
            "opacity": 255,
            "mode": "normal",
            "storage": "animated",
            "cels": [
                {"clip": "idle", "frame": 0, "rows": ["A.", ".A"]},
                {"clip": "idle", "frame": 1, "rows": [".A", "A."]},
            ],
        }],
    }


def assert_valid(test, document):
    test.assertIsNone(validate_document(document))


class FormatV2PropertyTest(unittest.TestCase):
    def test_dimensions_accept_boundaries_and_reject_neighbours(self):
        for dimension in ("width", "height"):
            for value in (1, 2, 17, 2047, 2048):
                document = base_document()
                document["canvas"][dimension] = value
                other = document["canvas"]["height" if dimension == "width" else "width"]
                rows = ["A" * value for _ in range(other)] if dimension == "width" else ["A" * other for _ in range(value)]
                for cel in document["layers"][0]["cels"]:
                    cel["rows"] = rows
                assert_valid(self, document)
            for value in (0, -1, 2049, 1.5, True, "2"):
                document = base_document()
                document["canvas"][dimension] = value
                with self.assertRaises(ContractError):
                    validate_document(document)

    def test_layer_counts_are_stable_under_generated_stack_sizes(self):
        for count in range(1, 25):
            document = base_document()
            document["layers"] = []
            for index in range(count):
                layer = copy.deepcopy(base_document()["layers"][0])
                layer["id"] = f"layer-{index}"
                layer["name"] = f"Layer {index}"
                document["layers"].append(layer)
            assert_valid(self, document)
        empty = base_document()
        empty["layers"] = []
        with self.assertRaises(ContractError):
            validate_document(empty)

    def test_opacity_domain_accepts_all_values_and_rejects_outside(self):
        for opacity in range(256):
            document = base_document()
            document["layers"][0]["opacity"] = opacity
            assert_valid(self, document)
        for opacity in (-1, 256, 999, 1.25, False, "128"):
            document = base_document()
            document["layers"][0]["opacity"] = opacity
            with self.assertRaises(ContractError):
                validate_document(document)

    def test_ids_and_names_cover_length_and_character_boundaries(self):
        for length in (1, 2, 16, 63, 64):
            document = base_document()
            document["layers"][0]["id"] = "a" + "x" * (length - 1)
            assert_valid(self, document)
        for length in (1, 2, 16, 127, 128):
            document = base_document()
            document["layers"][0]["name"] = "N" * length
            assert_valid(self, document)
        for value in ("", "A", "a_", "a/", "a" + "x" * 64, "a" * 65):
            document = base_document()
            document["layers"][0]["id"] = value
            with self.assertRaises(ContractError):
                validate_document(document)
        for value in ("", "N" * 129, 7, None):
            document = base_document()
            document["layers"][0]["name"] = value
            with self.assertRaises(ContractError):
                validate_document(document)

    def test_cel_counts_and_pairs_are_invariant_under_fuzzed_mutations(self):
        randomizer = random.Random(2669)
        for _ in range(250):
            document = base_document()
            layer = document["layers"][0]
            if randomizer.randrange(2):
                layer["storage"] = "shared"
                layer["cels"] = [{"scope": "all", "rows": ["A.", ".A"]}]
            assert_valid(self, document)

            broken = copy.deepcopy(document)
            cels = broken["layers"][0]["cels"]
            if broken["layers"][0]["storage"] == "shared":
                cels.append(copy.deepcopy(cels[0]))
            else:
                mutation = randomizer.randrange(3)
                if mutation == 0:
                    cels.pop()
                elif mutation == 1:
                    cels[0]["frame"] = 99
                else:
                    cels[1]["frame"] = cels[0]["frame"]
            with self.assertRaises(ContractError):
                validate_document(broken)


if __name__ == "__main__":
    unittest.main(verbosity=2)
