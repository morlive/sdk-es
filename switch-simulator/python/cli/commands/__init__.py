"""
Command modules package initialization.

This package contains modules for different command categories:
- port_commands: Commands for port configuration and management
- routing_commands: Commands for routing table and protocol management
- vlan_commands: Commands for VLAN configuration and management
"""

from .port_commands import register_port_commands
from .routing_commands import register_routing_commands
from .vlan_commands import register_vlan_commands

__all__ = [
    'register_port_commands',
    'register_routing_commands',
    'register_vlan_commands'
]

def register_all_commands(command_registry):
    """
    Register all CLI commands with the provided command registry.
    
    Args:
        command_registry: The command registry to register commands with
    """
    register_port_commands(command_registry)
    register_routing_commands(command_registry)
    register_vlan_commands(command_registry)
