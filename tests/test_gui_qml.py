#!/usr/bin/env python3
"""Focused regression checks for Studio QML contracts without a display."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
QML = ROOT / "src/gui/qml"


class GuiQmlContractTest(unittest.TestCase):
    def read(self, name):
        return (QML / name).read_text(encoding="utf-8")

    def test_save_filter_and_layer_geometry_use_runtime_values(self):
        main = self.read("Main.qml")
        layer_tool = self.read("LayerToolWindow.qml")
        self.assertIn('selectedNameFilter.extensions.indexOf("omapixel")', main)
        for tool in ("pencil", "eraser", "bucket", "picker", "hand"):
            self.assertIn(f"cfg.keys.tool_{tool}", main)
        self.assertIn("function shortcutGlyph(binding)", main)
        self.assertIn('binding.endsWith("++")', main)
        self.assertIn("Screen.desktopAvailableWidth", layer_tool)
        self.assertIn("Screen.desktopAvailableHeight", layer_tool)

    def test_reusable_controls_expose_focus_and_accessibility_state(self):
        for name in ("Chip.qml", "ToolButton.qml", "Section.qml"):
            source = self.read(name)
            self.assertIn("Accessible.role", source)
            self.assertIn("Accessible.name", source)
            self.assertIn("Accessible.checked", source)
            self.assertIn("forceActiveFocus()", source)
        self.assertIn("enabled: usable", self.read("Chip.qml"))
        self.assertIn("property bool checkable: false", self.read("Chip.qml"))
        self.assertIn("Accessible.checkable: checkable", self.read("Chip.qml"))
        self.assertIn("enabled: usable", self.read("ToolButton.qml"))
        self.assertIn("enabled: usable", self.read("Section.qml"))

    def test_collections_have_one_keyboard_entry_point(self):
        main = self.read("Main.qml")
        timeline = self.read("Timeline.qml")
        self.assertIn("Accessible.role: Accessible.List", timeline)
        self.assertIn("activeFocusOnTab: true", timeline)
        self.assertIn('id: frameList', timeline)
        self.assertIn("Accessible.role: Accessible.List", main)
        self.assertIn("id: swatches", main)
        self.assertIn("paletteColourError", main)
        self.assertIn('T.t("panel.palette.invalidColour")', main)
        self.assertIn("doc.palette[currentIndex - 1].slot", main)
        self.assertIn("1 + doc.palette.map", main)
        self.assertIn("activeFocusOnTab: enabled", main)

    def test_responsive_and_safety_feedback_contracts(self):
        main = self.read("Main.qml")
        status = self.read("StatusBar.qml")
        surface = self.read("Surface.qml")
        self.assertIn("id: clipScroll", self.read("Timeline.qml"))
        self.assertIn("readonly property bool compact", status)
        self.assertIn("Qt.ShiftModifier", surface)
        self.assertIn("usable: layerSheet.report.ok !== false", main)
        self.assertIn("if (layerSheet.report.ok === false)", main)


if __name__ == "__main__":
    unittest.main(verbosity=2)
