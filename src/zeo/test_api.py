#!/usr/bin/env python3
"""
KiCad API Test Script

This script can be run standalone or from within the KiCad IPC shell.

Usage:
    Standalone:
        python test_api.py [test_name ...]

    From IPC shell:
        import kipy
        kipy.test_ipc()

Available tests:
    connection, pcb_read, pcb_selection,
    schematic_read, schematic_selection,
    schematic_transactions, schematic_crud
"""

import sys


def main():
    try:
        import kipy
    except ImportError:
        print("Error: kipy module not found.")
        print("Make sure kipy is installed or run from the IPC shell.")
        return 1

    # Parse arguments
    if len(sys.argv) > 1:
        if sys.argv[1] == "list":
            print("Available tests:")
            print("  connection")
            print("  pcb_read")
            print("  pcb_selection")
            print("  schematic_read")
            print("  schematic_selection")
            print("  schematic_transactions")
            print("  schematic_crud")
            return 0

        tests = sys.argv[1:]
        success = kipy.test_ipc(tests=tests)
    else:
        success = kipy.test_ipc()

    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
