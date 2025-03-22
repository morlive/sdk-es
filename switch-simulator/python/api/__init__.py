"""
Switch Simulator Python API

This package provides a Python interface to interact with the switch simulator.
It allows controlling the switch, configuring interfaces, VLANs, routing,
and viewing real-time statistics.
"""

from .switch_controller import SwitchController, SwitchInterface, SwitchConfig
from .stats_viewer import StatsViewer

__all__ = [
    'SwitchController',
    'SwitchInterface',
    'SwitchConfig',
    'StatsViewer',
]
