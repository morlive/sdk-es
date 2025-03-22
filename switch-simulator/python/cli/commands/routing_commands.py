"""
Routing-related CLI commands.

This module provides commands for configuring and managing routing functionality,
including static routes, routing protocols, and viewing routing tables.
"""

import argparse
import ipaddress
from ...api.switch_controller import SwitchController


def show_routes(args, switch_ctrl):
    """Display routing table information"""
    if args.destination:
        try:
            # Validate IP address/subnet format
            dest = ipaddress.ip_network(args.destination, strict=False)
            # Show specific route
            route = switch_ctrl.get_route(str(dest))
            if not route:
                print(f"Error: Route for {args.destination} not found")
                return
            
            print(f"Destination: {route['destination']}")
            print(f"  Next Hop: {route['next_hop']}")
            print(f"  Interface: {route['interface']}")
            print(f"  Protocol: {route['protocol']}")
            print(f"  Metric: {route['metric']}")
            print(f"  Admin Distance: {route['admin_distance']}")
        except ValueError:
            print(f"Error: Invalid IP address or subnet format: {args.destination}")
    else:
        # Show all routes
        routes = switch_ctrl.get_all_routes()
        print("Destination       Next Hop         Interface  Protocol  Metric")
        print("--------------------------------------------------------------")
        for route in routes:
            print(f"{route['destination']:<18} {route['next_hop']:<16} "
                  f"{route['interface']:<10} {route['protocol']:<9} {route['metric']}")


def add_route(args, switch_ctrl):
    """Add a static route"""
    try:
        # Validate IP address/subnet formats
        dest = ipaddress.ip_network(args.destination, strict=False)
        next_hop = ipaddress.ip_address(args.next_hop)
        
        result = switch_ctrl.add_static_route(
            destination=str(dest),
            next_hop=str(next_hop),
            interface=args.interface,
            metric=args.metric
        )
        
        if result:
            print(f"Static route to {args.destination} via {args.next_hop} added successfully")
        else:
            print(f"Failed to add static route")
    except ValueError as e:
        print(f"Error: Invalid IP format - {str(e)}")
    except Exception as e:
        print(f"Error: {str(e)}")


def delete_route(args, switch_ctrl):
    """Delete a route"""
    try:
        # Validate IP address/subnet format
        dest = ipaddress.ip_network(args.destination, strict=False)
        
        result = switch_ctrl.delete_route(str(dest))
        if result:
            print(f"Route to {args.destination} deleted successfully")
        else:
            print(f"Failed to delete route to {args.destination}")
    except ValueError:
        print(f"Error: Invalid IP address or subnet format: {args.destination}")
    except Exception as e:
        print(f"Error: {str(e)}")


def config_ospf(args, switch_ctrl):
    """Configure OSPF protocol"""
    if args.action == "enable":
        result = switch_ctrl.enable_ospf(area_id=args.area)
        if result:
            print(f"OSPF enabled for area {args.area}")
        else:
            print("Failed to enable OSPF")
    elif args.action == "disable":
        result = switch_ctrl.disable_ospf()
        if result:
            print("OSPF disabled")
        else:
            print("Failed to disable OSPF")
    elif args.action == "status":
        status = switch_ctrl.get_ospf_status()
        print(f"OSPF Status: {'Enabled' if status['enabled'] else 'Disabled'}")
        if status['enabled']:
            print(f"Router ID: {status['router_id']}")
            print("Areas:")
            for area in status['areas']:
                print(f"  Area {area['id']}: {area['interfaces']} interfaces")


def register_routing_commands(subparsers):
    """
    Register all routing-related commands with the CLI parser
    
    Args:
        subparsers: The subparsers object from argparse to register commands with
    """
    # Create routing command parser
    routing_parser = subparsers.add_parser('routing', help='Routing configuration and management')
    routing_subparsers = routing_parser.add_subparsers(dest='routing_command', help='Routing commands')
    routing_subparsers.required = True
    
    # Show routes command
    show_parser = routing_subparsers.add_parser('show', help='Show routing table')
    show_parser.add_argument('destination', nargs='?', help='Destination network (e.g., 192.168.1.0/24)')
    show_parser.set_defaults(func=show_routes)
    
    # Add route command
    add_parser = routing_subparsers.add_parser('add', help='Add a static route')
    add_parser.add_argument('destination', help='Destination network (e.g., 192.168.1.0/24)')
    add_parser.add_argument('next_hop', help='Next hop IP address')
    add_parser.add_argument('--interface', '-i', required=True, help='Outgoing interface')
    add_parser.add_argument('--metric', '-m', type=int, default=1, help='Route metric (default: 1)')
    add_parser.set_defaults(func=add_route)
    
    # Delete route command
    delete_parser = routing_subparsers.add_parser('delete', help='Delete a route')
    delete_parser.add_argument('destination', help='Destination network to delete (e.g., 192.168.1.0/24)')
    delete_parser.set_defaults(func=delete_route)
    
    # OSPF configuration command
    ospf_parser = routing_subparsers.add_parser('ospf', help='OSPF configuration')
    ospf_parser.add_argument('action', choices=['enable', 'disable', 'status'], help='OSPF action')
    ospf_parser.add_argument('--area', '-a', type=int, default=0, help='OSPF area ID (default: 0)')
    ospf_parser.set_defaults(func=config_ospf)
