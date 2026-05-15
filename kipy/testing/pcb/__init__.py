# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
PCB IPC API tests.

This package contains tests for PCB-related functionality.
"""

from kipy.testing.pcb.test_read import test_pcb_read
from kipy.testing.pcb.test_selection import test_pcb_selection
from kipy.testing.pcb.test_routing import test_pcb_routing, test_pcb_coordinates
from kipy.testing.pcb.test_zones import test_pcb_zones

__all__ = [
    "test_pcb_read",
    "test_pcb_selection",
    "test_pcb_routing",
    "test_pcb_coordinates",
    "test_pcb_zones",
]
