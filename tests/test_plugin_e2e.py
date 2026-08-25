#!/usr/bin/env python3
"""The complete real-binary plugin acceptance matrix."""

from test_plugin_discovery import main as discovery_main
from test_plugin_run import main as run_main


def main():
    discovery_main()
    run_main()


if __name__ == "__main__":
    main()
