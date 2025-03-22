"""
Command Line Interface (CLI) package for the switch simulator.

This package provides a command-line interface for interacting with
the switch simulator, allowing users to configure and monitor the
simulated switch.

Main modules:
- cli_main: Entry point for the CLI application
- cli_parser: Command line argument parsing
- commands: Subpackage containing command implementations
"""

from .cli_main import main
from .cli_parser import create_parser

__all__ = ['main', 'create_parser']
