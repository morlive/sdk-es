"""
VLAN-related CLI commands.

This module provides commands for configuring and managing VLANs,
including creating/deleting VLANs and assigning ports to VLANs.
"""

import argparse
from ...api.switch_controller import SwitchController


def show_vlan(args, switch_ctrl):
    """Display VLAN information"""
    if args.vlan_id:
        # Show specific VLAN
        vlan = switch_ctrl.get_vlan(args.vlan_id)
        if not vlan:
            print(f"Error: VLAN {args.vlan_id} not found")
            return
        
        print(f"VLAN {vlan['id']} - {vlan['name']}")
        print(f"  Status: {'Active' if vlan['active'] else 'Inactive'}")
        print("  Ports:")
        if vlan['ports']:
            for port in vlan['ports']:
                print(f"    {port['id']} - {port['mode']}")
        else:
            print("    No ports assigned")
    else:
        # Show all VLANs
        vlans = switch_ctrl.get_all_vlans()
        print("VLAN ID  Name                 Status    Ports")
        print("------------------------------------------------")
        for vlan in vlans:
            port_list = ", ".join([str(p['id']) for p in vlan['ports']])
            print(f"{vlan['id']:<8} {vlan['name']:<20} {'Active' if vlan['active'] else 'Inactive':<9} {port_list}")


def create_vlan(args, switch_ctrl):
    """Create a new VLAN"""
    if args.vlan_id < 1 or args.vlan_id > 4094:
        print("Error: VLAN ID must be between 1 and 4094")
        return
    
    try:
        result = switch_ctrl.create_vlan(args.vlan_id, args.name)
        if result:
            print(f"VLAN {args.vlan_id} created successfully")
        else:
            print(f"Failed to create VLAN {args.vlan_id}")
    except Exception as e:
        print(f"Error: {str(e)}")


def delete_vlan(args, switch_ctrl):
    """Delete a VLAN"""
    try:
        result = switch_ctrl.delete_vlan(args.vlan_id)
        if result:
            print(f"VLAN {args.vlan_id} deleted successfully")
        else:
            print(f"Failed to delete VLAN {args.vlan_id}")
    except Exception as e:
        print(f"Error: {str(e)}")


def add_port_to_vlan(args, switch_ctrl):
    """Add a port to a VLAN"""
    valid_modes = ['access', 'trunk', 'hybrid']
    if args.mode not in valid_modes:
        print(f"Error: Mode must be one of {valid_modes}")
        return
    
    try:
        result = switch_ctrl.add_port_to_vlan(
            port_id=args.port_id,
            vlan_id=args.vlan_id,
            mode=args.mode
        )
        
        if result:
            print(f"Port {args.port_id} added to VLAN {args.vlan_id} in {args.mode} mode")
        else:
            print(f"Failed to add port {args.port_id} to VLAN {args.vlan_id}")
    except Exception as e:
        print(f"Error: {str(e)}")


def remove_port_from_vlan(args, switch_ctrl):
    """Remove a port from a VLAN"""
    try:
        result = switch_ctrl.remove_port_from_vlan(args.port_id, args.vlan_id)
        if result:
            print(f"Port {args.port_id} removed from VLAN {args.vlan_id}")
        else:
            print(f"Failed to remove port {args.port_id} from VLAN {args.vlan_id}")
    except Exception as e:
        print(f"Error: {str(e)}")


def register_vlan_commands(subparsers):
    """
    Register all VLAN-related commands with the CLI parser
    
    Args:
        subparsers: The subparsers object from argparse to register commands with
    """
    # Create VLAN command parser
    vlan_parser = subparsers.add_parser('vlan', help='VLAN configuration and management')
    vlan_subparsers = vlan_parser.add_subparsers(dest='vlan_command', help='VLAN commands')
    vlan_subparsers.required = True
    
    # Show VLAN command
    show_parser = vlan_subparsers.add_parser('show', help='Show VLAN information')
    show_parser.add_argument('vlan_id', nargs='?', type=int, help='VLAN ID (optional, shows all VLANs if omitted)')
    show_parser.set_defaults(func=show_vlan)
    
    # Create VLAN command
    create_parser = vlan_subparsers.add_parser('create', help='Create a new VLAN')
    create_parser.add_argument('vlan_id', type=int, help='VLAN ID (1-4094)')
    create_parser.add_argument('name', help='VLAN name')
    create_parser.set_defaults(func=create_vlan)
    
    # Delete VLAN command
    delete_parser = vlan_subparsers.add_parser('delete', help='Delete a VLAN')
    delete_parser.add_argument('vlan_id', type=int, help='VLAN ID to delete')
    delete_parser.set_defaults(func=delete_vlan)
    
    # Add port to VLAN command
    add_port_parser = vlan_subparsers.add_parser('add-port', help='Add a port to a VLAN')
    add_port_parser.add_argument('vlan_id', type=int, help='VLAN ID')
    add_port_parser.add_argument('port_id', type=int, help='Port ID')
    add_port_parser.add_argument('--mode', '-m', default='access', choices=['access', 'trunk', 'hybrid'],
                              help='Port mode (default: access)')
    add_port_parser.set_defaults(func=add_port_to_vlan)
    
    # Remove port from VLAN command
    remove_port_parser = vlan_subparsers.add_parser('remove-port', help='Remove a port from a VLAN')
    remove_port_parser.add_argument('vlan_id', type=int, help='VLAN ID')
    remove_port_parser.add_argument('port_id', type=int, help='Port ID')
    remove_port_parser.set_defaults(func=remove_port_from_vlan)
