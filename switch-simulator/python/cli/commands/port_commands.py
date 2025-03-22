"""
Port-related CLI commands.

This module provides commands for configuring and managing switch ports,
including enabling/disabling ports, setting port speed, and viewing port status.
"""

import argparse
from ...api.switch_controller import SwitchController


def show_port(args, switch_ctrl):
    """Display port information"""
    if args.port_id:
        # Show specific port
        port = switch_ctrl.get_port(args.port_id)
        if not port:
            print(f"Error: Port {args.port_id} not found")
            return
        
        print(f"Port {port['id']}:")
        print(f"  Status: {'Up' if port['up'] else 'Down'}")
        print(f"  Speed: {port['speed']} Mbps")
        print(f"  Duplex: {port['duplex']}")
        print(f"  VLAN: {port['vlan']}")
        print(f"  MAC Address: {port['mac']}")
    else:
        # Show all ports
        ports = switch_ctrl.get_all_ports()
        print("Port   Status   Speed    VLAN   MAC Address")
        print("-----------------------------------------")
        for port in ports:
            print(f"{port['id']:<6} {'Up' if port['up'] else 'Down':<8} "
                  f"{port['speed']:<8} {port['vlan']:<6} {port['mac']}")


def set_port_status(args, switch_ctrl):
    """Enable or disable a port"""
    try:
        if args.status.lower() == "up":
            result = switch_ctrl.set_port_status(args.port_id, True)
        elif args.status.lower() == "down":
            result = switch_ctrl.set_port_status(args.port_id, False)
        else:
            print("Error: Status must be 'up' or 'down'")
            return
        
        if result:
            print(f"Port {args.port_id} set to {args.status}")
        else:
            print(f"Failed to set port {args.port_id} status")
    except Exception as e:
        print(f"Error: {str(e)}")


def set_port_speed(args, switch_ctrl):
    """Set port speed"""
    valid_speeds = [10, 100, 1000, 10000, 25000, 40000, 100000]
    
    if args.speed not in valid_speeds:
        print(f"Error: Speed must be one of {valid_speeds}")
        return
    
    try:
        result = switch_ctrl.set_port_speed(args.port_id, args.speed)
        if result:
            print(f"Port {args.port_id} speed set to {args.speed} Mbps")
        else:
            print(f"Failed to set port {args.port_id} speed")
    except Exception as e:
        print(f"Error: {str(e)}")


def register_port_commands(subparsers):
    """
    Register all port-related commands with the CLI parser
    
    Args:
        subparsers: The subparsers object from argparse to register commands with
    """
    # Create port command parser
    port_parser = subparsers.add_parser('port', help='Port configuration and management')
    port_subparsers = port_parser.add_subparsers(dest='port_command', help='Port commands')
    port_subparsers.required = True
    
    # Show port command
    show_parser = port_subparsers.add_parser('show', help='Show port information')
    show_parser.add_argument('port_id', nargs='?', type=int, help='Port ID (optional, shows all ports if omitted)')
    show_parser.set_defaults(func=show_port)
    
    # Set port status command
    status_parser = port_subparsers.add_parser('status', help='Set port status (up/down)')
    status_parser.add_argument('port_id', type=int, help='Port ID')
    status_parser.add_argument('status', choices=['up', 'down'], help='Port status')
    status_parser.set_defaults(func=set_port_status)
    
    # Set port speed command
    speed_parser = port_subparsers.add_parser('speed', help='Set port speed')
    speed_parser.add_argument('port_id', type=int, help='Port ID')
    speed_parser.add_argument('speed', type=int, help='Port speed in Mbps (10, 100, 1000, etc.)')
    speed_parser.set_defaults(func=set_port_speed)
