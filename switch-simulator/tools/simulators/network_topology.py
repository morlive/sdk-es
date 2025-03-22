#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Network Topology Simulator

This module provides functionality to create and manipulate virtual network
topologies for testing the switch simulator.
"""

import json
import logging
import os
import subprocess
import sys
import tempfile
import time
from typing import Dict, List, Optional, Set, Tuple, Union
import random
import uuid

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S'
)
logger = logging.getLogger('network_topology')


class NetworkDevice:
    """Base class for all network devices in the topology."""
    
    def __init__(self, device_id: str, device_type: str, name: Optional[str] = None):
        """Initialize a network device.
        
        Args:
            device_id: Unique identifier for the device
            device_type: Type of the device (e.g., 'switch', 'host')
            name: Optional human-readable name for the device
        """
        self.device_id = device_id
        self.device_type = device_type
        self.name = name or device_id
        self.interfaces = {}  # Dict mapping interface name to connected device and its interface
        self.properties = {}  # Additional device-specific properties
    
    def add_interface(self, interface_name: str) -> None:
        """Add a new interface to the device.
        
        Args:
            interface_name: Name of the interface to add
        """
        if interface_name in self.interfaces:
            logger.warning(f"Interface {interface_name} already exists on device {self.name}")
            return
        
        self.interfaces[interface_name] = None
        logger.debug(f"Added interface {interface_name} to device {self.name}")
    
    def get_available_interface(self) -> Optional[str]:
        """Get an available (unconnected) interface name.
        
        Returns:
            Name of an available interface or None if all are connected
        """
        for interface, connection in self.interfaces.items():
            if connection is None:
                return interface
        return None
    
    def set_property(self, key: str, value) -> None:
        """Set a device property.
        
        Args:
            key: Property name
            value: Property value
        """
        self.properties[key] = value
    
    def get_property(self, key: str, default=None):
        """Get a device property.
        
        Args:
            key: Property name
            default: Default value if property doesn't exist
            
        Returns:
            The property value or the default value
        """
        return self.properties.get(key, default)
    
    def to_dict(self) -> Dict:
        """Convert the device to a dictionary representation.
        
        Returns:
            Dictionary representation of the device
        """
        connections = {}
        for intf, conn in self.interfaces.items():
            if conn is not None:
                remote_device, remote_intf = conn
                connections[intf] = {
                    'device': remote_device.device_id,
                    'interface': remote_intf
                }
            else:
                connections[intf] = None
        
        return {
            'id': self.device_id,
            'type': self.device_type,
            'name': self.name,
            'interfaces': list(self.interfaces.keys()),
            'connections': connections,
            'properties': self.properties
        }


class Switch(NetworkDevice):
    """Switch device in the network topology."""
    
    def __init__(self, device_id: str, name: Optional[str] = None, 
                 num_ports: int = 24, switch_type: str = 'l2'):
        """Initialize a switch device.
        
        Args:
            device_id: Unique identifier for the switch
            name: Optional human-readable name for the switch
            num_ports: Number of ports on the switch
            switch_type: Type of switch ('l2' or 'l3')
        """
        super().__init__(device_id, 'switch', name)
        
        # Add the specified number of ports
        for i in range(1, num_ports + 1):
            self.add_interface(f"port{i}")
        
        # Set switch-specific properties
        self.set_property('switch_type', switch_type)
        self.set_property('vlans', {'1': {'name': 'default', 'ports': list(self.interfaces.keys())}})
        
        if switch_type == 'l3':
            self.set_property('routing_table', {})
            self.set_property('ip_interfaces', {})


class Host(NetworkDevice):
    """Host device in the network topology."""
    
    def __init__(self, device_id: str, name: Optional[str] = None, ip_address: Optional[str] = None):
        """Initialize a host device.
        
        Args:
            device_id: Unique identifier for the host
            name: Optional human-readable name for the host
            ip_address: Optional IP address for the host
        """
        super().__init__(device_id, 'host', name)
        
        # Add a single network interface
        self.add_interface("eth0")
        
        # Set host-specific properties
        if ip_address:
            self.set_property('ip_address', ip_address)
            self.set_property('mac_address', self._generate_mac_address())
    
    def _generate_mac_address(self) -> str:
        """Generate a random MAC address.
        
        Returns:
            A MAC address as a string
        """
        # Generate a random MAC address with a specific prefix
        mac = "02:00:00"  # Locally administered MAC address prefix
        for _ in range(3):
            mac += f":{random.randint(0, 255):02x}"
        return mac


class NetworkTopology:
    """Represents a network topology of connected devices."""
    
    def __init__(self, name: str = "Default Topology"):
        """Initialize a network topology.
        
        Args:
            name: Name of the topology
        """
        self.name = name
        self.devices = {}  # Dict mapping device ID to device object
        self.connections = set()  # Set of (device1_id, intf1, device2_id, intf2) tuples
    
    def add_device(self, device: NetworkDevice) -> None:
        """Add a device to the topology.
        
        Args:
            device: Device to add
        
        Raises:
            ValueError: If a device with the same ID already exists
        """
        if device.device_id in self.devices:
            raise ValueError(f"Device with ID {device.device_id} already exists in the topology")
        
        self.devices[device.device_id] = device
        logger.debug(f"Added device {device.name} to topology")
    
    def remove_device(self, device_id: str) -> None:
        """Remove a device from the topology.
        
        Args:
            device_id: ID of the device to remove
            
        Raises:
            ValueError: If the device doesn't exist
        """
        if device_id not in self.devices:
            raise ValueError(f"Device with ID {device_id} doesn't exist in the topology")
        
        # Remove all connections involving this device
        device = self.devices[device_id]
        for intf, conn in list(device.interfaces.items()):
            if conn is not None:
                remote_device, remote_intf = conn
                self.disconnect_devices(device_id, intf, remote_device.device_id, remote_intf)
        
        # Remove the device
        del self.devices[device_id]
        logger.debug(f"Removed device {device_id} from topology")
    
    def get_device(self, device_id: str) -> NetworkDevice:
        """Get a device by its ID.
        
        Args:
            device_id: ID of the device to get
            
        Returns:
            The device object
            
        Raises:
            ValueError: If the device doesn't exist
        """
        if device_id not in self.devices:
            raise ValueError(f"Device with ID {device_id} doesn't exist in the topology")
        
        return self.devices[device_id]
    
    def connect_devices(self, 
                      device1_id: str, interface1: str, 
                      device2_id: str, interface2: str) -> None:
        """Connect two devices by their interfaces.
        
        Args:
            device1_id: ID of the first device
            interface1: Interface name on the first device
            device2_id: ID of the second device
            interface2: Interface name on the second device
            
        Raises:
            ValueError: If either device doesn't exist or if interfaces are already connected
        """
        # Get the devices
        if device1_id not in self.devices:
            raise ValueError(f"Device with ID {device1_id} doesn't exist in the topology")
        if device2_id not in self.devices:
            raise ValueError(f"Device with ID {device2_id} doesn't exist in the topology")
        
        device1 = self.devices[device1_id]
        device2 = self.devices[device2_id]
        
        # Check if the interfaces exist and are not already connected
        if interface1 not in device1.interfaces:
            raise ValueError(f"Interface {interface1} doesn't exist on device {device1.name}")
        if interface2 not in device2.interfaces:
            raise ValueError(f"Interface {interface2} doesn't exist on device {device2.name}")
        
        if device1.interfaces[interface1] is not None:
            raise ValueError(f"Interface {interface1} on device {device1.name} is already connected")
        if device2.interfaces[interface2] is not None:
            raise ValueError(f"Interface {interface2} on device {device2.name} is already connected")
        
        # Connect the interfaces
        device1.interfaces[interface1] = (device2, interface2)
        device2.interfaces[interface2] = (device1, interface1)
        
        # Add to the connections set
        connection = (device1_id, interface1, device2_id, interface2)
        self.connections.add(connection)
        
        logger.debug(f"Connected {device1.name}:{interface1} to {device2.name}:{interface2}")
    
    def disconnect_devices(self, 
                         device1_id: str, interface1: str, 
                         device2_id: str, interface2: str) -> None:
        """Disconnect two devices.
        
        Args:
            device1_id: ID of the first device
            interface1: Interface name on the first device
            device2_id: ID of the second device
            interface2: Interface name on the second device
            
        Raises:
            ValueError: If either device doesn't exist or if the interfaces are not connected
        """
        # Get the devices
        if device1_id not in self.devices:
            raise ValueError(f"Device with ID {device1_id} doesn't exist in the topology")
        if device2_id not in self.devices:
            raise ValueError(f"Device with ID {device2_id} doesn't exist in the topology")
        
        device1 = self.devices[device1_id]
        device2 = self.devices[device2_id]
        
        # Check if the interfaces exist and are connected to each other
        if interface1 not in device1.interfaces:
            raise ValueError(f"Interface {interface1} doesn't exist on device {device1.name}")
        if interface2 not in device2.interfaces:
            raise ValueError(f"Interface {interface2} doesn't exist on device {device2.name}")
        
        conn1 = device1.interfaces[interface1]
        conn2 = device2.interfaces[interface2]
        
        if conn1 is None or conn1[0] != device2 or conn1[1] != interface2:
            raise ValueError(f"Devices {device1.name}:{interface1} and {device2.name}:{interface2} are not connected")
        
        # Disconnect the interfaces
        device1.interfaces[interface1] = None
        device2.interfaces[interface2] = None
        
        # Remove from the connections set
        connection = (device1_id, interface1, device2_id, interface2)
        if connection in self.connections:
            self.connections.remove(connection)
        
        logger.debug(f"Disconnected {device1.name}:{interface1} from {device2.name}:{interface2}")
    
    def assign_ip_to_host(self, host_id: str, ip_address: str, subnet_mask: str = '255.255.255.0') -> None:
        """Assign an IP address to a host.
        
        Args:
            host_id: ID of the host device
            ip_address: IP address to assign
            subnet_mask: Subnet mask for the IP address
            
        Raises:
            ValueError: If the device doesn't exist or is not a host
        """
        if host_id not in self.devices:
            raise ValueError(f"Device with ID {host_id} doesn't exist in the topology")
        
        device = self.devices[host_id]
        if device.device_type != 'host':
            raise ValueError(f"Device {device.name} is not a host")
        
        device.set_property('ip_address', ip_address)
        device.set_property('subnet_mask', subnet_mask)
        logger.debug(f"Assigned IP {ip_address}/{subnet_mask} to host {device.name}")
    
    def configure_vlan(self, switch_id: str, vlan_id: str, vlan_name: str, ports: List[str]) -> None:
        """Configure a VLAN on a switch.
        
        Args:
            switch_id: ID of the switch device
            vlan_id: VLAN ID
            vlan_name: VLAN name
            ports: List of port names to add to the VLAN
            
        Raises:
            ValueError: If the device doesn't exist or is not a switch
        """
        if switch_id not in self.devices:
            raise ValueError(f"Device with ID {switch_id} doesn't exist in the topology")
        
        device = self.devices[switch_id]
        if device.device_type != 'switch':
            raise ValueError(f"Device {device.name} is not a switch")
        
        # Get the current VLANs
        vlans = device.get_property('vlans', {})
        
        # Update or add the VLAN
        vlans[vlan_id] = {
            'name': vlan_name,
            'ports': ports
        }
        
        device.set_property('vlans', vlans)
        logger.debug(f"Configured VLAN {vlan_id} ({vlan_name}) on switch {device.name}")
    
    def configure_stp(self, switch_id: str, priority: int = 32768) -> None:
        """Configure Spanning Tree Protocol on a switch.
        
        Args:
            switch_id: ID of the switch device
            priority: STP priority value
            
        Raises:
            ValueError: If the device doesn't exist or is not a switch
        """
        if switch_id not in self.devices:
            raise ValueError(f"Device with ID {switch_id} doesn't exist in the topology")
        
        device = self.devices[switch_id]
        if device.device_type != 'switch':
            raise ValueError(f"Device {device.name} is not a switch")
        
        device.set_property('stp_priority', priority)
        logger.debug(f"Configured STP priority {priority} on switch {device.name}")
    
    def configure_routing(self, switch_id: str, dest_network: str, next_hop: str, metric: int = 1) -> None:
        """Configure a static route on a Layer 3 switch.
        
        Args:
            switch_id: ID of the L3 switch device
            dest_network: Destination network (CIDR notation)
            next_hop: Next hop IP address
            metric: Route metric
            
        Raises:
            ValueError: If the device doesn't exist or is not an L3 switch
        """
        if switch_id not in self.devices:
            raise ValueError(f"Device with ID {switch_id} doesn't exist in the topology")
        
        device = self.devices[switch_id]
        if device.device_type != 'switch' or device.get_property('switch_type') != 'l3':
            raise ValueError(f"Device {device.name} is not an L3 switch")
        
        # Get the current routing table
        routing_table = device.get_property('routing_table', {})
        
        # Add the route
        routing_table[dest_network] = {
            'next_hop': next_hop,
            'metric': metric
        }
        
        device.set_property('routing_table', routing_table)
        logger.debug(f"Configured route to {dest_network} via {next_hop} on switch {device.name}")
    
    def to_dict(self) -> Dict:
        """Convert the topology to a dictionary representation.
        
        Returns:
            Dictionary representation of the topology
        """
        return {
            'name': self.name,
            'devices': {device_id: device.to_dict() for device_id, device in self.devices.items()},
            'connections': [
                {
                    'device1': device1_id,
                    'interface1': intf1,
                    'device2': device2_id,
                    'interface2': intf2
                }
                for device1_id, intf1, device2_id, intf2 in self.connections
            ]
        }
    
    def save_to_file(self, filename: str) -> None:
        """Save the topology to a JSON file.
        
        Args:
            filename: Path to the file to save to
        """
        with open(filename, 'w') as f:
            json.dump(self.to_dict(), f, indent=2)
        logger.info(f"Topology saved to {filename}")
    
    @classmethod
    def load_from_file(cls, filename: str) -> 'NetworkTopology':
        """Load a topology from a JSON file.
        
        Args:
            filename: Path to the file to load from
            
        Returns:
            Loaded NetworkTopology object
            
        Raises:
            FileNotFoundError: If the file doesn't exist
        """
        with open(filename, 'r') as f:
            data = json.load(f)
        
        topology = cls(data.get('name', 'Loaded Topology'))
        
        # Create devices first
        for device_id, device_data in data['devices'].items():
            device_type = device_data['type']
            
            if device_type == 'switch':
                switch_type = device_data['properties'].get('switch_type', 'l2')
                num_ports = len(device_data['interfaces'])
                device = Switch(device_id, device_data['name'], num_ports, switch_type)
                
                # Set all properties
                for key, value in device_data['properties'].items():
                    device.set_property(key, value)
                
                topology.add_device(device)
            
            elif device_type == 'host':
                ip_address = device_data['properties'].get('ip_address')
                device = Host(device_id, device_data['name'], ip_address)
                
                # Set all properties
                for key, value in device_data['properties'].items():
                    device.set_property(key, value)
                
                topology.add_device(device)
        
        # Connect devices
        for connection in data['connections']:
            topology.connect_devices(
                connection['device1'], connection['interface1'],
                connection['device2'], connection['interface2']
            )
        
        logger.info(f"Topology loaded from {filename}")
        return topology
    
    @staticmethod
    def create_simple_network(num_switches: int = 2, num_hosts: int = 4) -> 'NetworkTopology':
        """Create a simple network topology with switches and hosts.
        
        Args:
            num_switches: Number of switches to create
            num_hosts: Number of hosts to create
            
        Returns:
            A NetworkTopology object with the created devices
        """
        topology = NetworkTopology("Simple Network")
        
        # Create switches
        switches = []
        for i in range(1, num_switches + 1):
            switch_id = f"switch{i}"
            switch = Switch(switch_id, f"Switch {i}")
            topology.add_device(switch)
            switches.append(switch)
        
        # Create hosts
        hosts = []
        for i in range(1, num_hosts + 1):
            host_id = f"host{i}"
            # Assign an IP address in the 192.168.1.x subnet
            ip_address = f"192.168.1.{100 + i}"
            host = Host(host_id, f"Host {i}", ip_address)
            topology.add_device(host)
            hosts.append(host)
        
        # Connect hosts to switches
        # Distribute hosts evenly among switches
        hosts_per_switch = max(1, num_hosts // num_switches)
        for i, host in enumerate(hosts):
            switch_index = min(i // hosts_per_switch, num_switches - 1)
            switch = switches[switch_index]
            
            # Get available interfaces
            host_interface = host.get_available_interface()
            switch_interface = switch.get_available_interface()
            
            if host_interface and switch_interface:
                topology.connect_devices(
                    host.device_id, host_interface,
                    switch.device_id, switch_interface
                )
        
        # Connect switches to each other in a line
        for i in range(len(switches) - 1):
            # Get available interfaces
            switch1 = switches[i]
            switch2 = switches[i + 1]
            
            switch1_interface = switch1.get_available_interface()
            switch2_interface = switch2.get_available_interface()
            
            if switch1_interface and switch2_interface:
                topology.connect_devices(
                    switch1.device_id, switch1_interface,
                    switch2.device_id, switch2_interface
                )
        
        logger.info(f"Created simple network with {num_switches} switches and {num_hosts} hosts")
        return topology
    
    @staticmethod
    def create_ring_network(num_switches: int = 4, num_hosts: int = 4) -> 'NetworkTopology':
        """Create a ring network topology with switches in a ring and hosts connected to them.
        
        Args:
            num_switches: Number of switches to create
            num_hosts: Number of hosts to create
            
        Returns:
            A NetworkTopology object with the created devices
        """
        topology = NetworkTopology("Ring Network")
        
        # Create switches
        switches = []
        for i in range(1, num_switches + 1):
            switch_id = f"switch{i}"
            switch = Switch(switch_id, f"Switch {i}")
            topology.add_device(switch)
            switches.append(switch)
        
        # Create hosts
        hosts = []
        for i in range(1, num_hosts + 1):
            host_id = f"host{i}"
            # Assign an IP address in the 192.168.1.x subnet
            ip_address = f"192.168.1.{100 + i}"
            host = Host(host_id, f"Host {i}", ip_address)
            topology.add_device(host)
            hosts.append(host)
        
        # Connect hosts to switches
        # Distribute hosts evenly among switches
        hosts_per_switch = max(1, num_hosts // num_switches)
        for i, host in enumerate(hosts):
            switch_index = min(i // hosts_per_switch, num_switches - 1)
            switch = switches[switch_index]
            
            # Get available interfaces
            host_interface = host.get_available_interface()
            switch_interface = switch.get_available_interface()
            
            if host_interface and switch_interface:
                topology.connect_devices(
                    host.device_id, host_interface,
                    switch.device_id, switch_interface
                )
        
        # Connect switches in a ring
        for i in range(num_switches):
            # Connect each switch to the next one in the ring
            switch1 = switches[i]
            switch2 = switches[(i + 1) % num_switches]
            
            switch1_interface = switch1.get_available_interface()
            switch2_interface = switch2.get_available_interface()
            
            if switch1_interface and switch2_interface:
                topology.connect_devices(
                    switch1.device_id, switch1_interface,
                    switch2.device_id, switch2_interface
                )
        
        logger.info(f"Created ring network with {num_switches} switches and {num_hosts} hosts")
        return topology
    
    @staticmethod
    def create_mesh_network(num_switches: int = 4, num_hosts: int = 4) -> 'NetworkTopology':
        """Create a full mesh network topology with switches fully connected and hosts connected to them.
        
        Args:
            num_switches: Number of switches to create
            num_hosts: Number of hosts to create
            
        Returns:
            A NetworkTopology object with the created devices
        """
        topology = NetworkTopology("Mesh Network")
        
        # Create switches
        switches = []
        for i in range(1, num_switches + 1):
            switch_id = f"switch{i}"
            switch = Switch(switch_id, f"Switch {i}")
            topology.add_device(switch)
            switches.append(switch)
        
        # Create hosts
        hosts = []
        for i in range(1, num_hosts + 1):
            host_id = f"host{i}"
            # Assign an IP address in the 192.168.1.x subnet
            ip_address = f"192.168.1.{100 + i}"
            host = Host(host_id, f"Host {i}", ip_address)
            topology.add_device(host)
            hosts.append(host)
        
        # Connect hosts to switches
        # Distribute hosts evenly among switches
        hosts_per_switch = max(1, num_hosts // num_switches)
        for i, host in enumerate(hosts):
            switch_index = min(i // hosts_per_switch, num_switches - 1)
            switch = switches[switch_index]
            
            # Get available interfaces
            host_interface = host.get_available_interface()
            switch_interface = switch.get_available_interface()
            
            if host_interface and switch_interface:
                topology.connect_devices(
                    host.device_id, host_interface,
                    switch.device_id, switch_interface
                )
        
        # Connect switches in a full mesh
        for i in range(num_switches):
            for j in range(i + 1, num_switches):
                switch1 = switches[i]
                switch2 = switches[j]
                
                switch1_interface = switch1.get_available_interface()
                switch2_interface = switch2.get_available_interface()
                
                if switch1_interface and switch2_interface:
                    topology.connect_devices(
                        switch1.device_id, switch1_interface,
                        switch2.device_id, switch2_interface
                    )
        
        logger.info(f"Created mesh network with {num_switches} switches and {num_hosts} hosts")
        return topology


def main():
    """Main entry point for the network topology creator script."""
    import argparse
    
    parser = argparse.ArgumentParser(description='Create and manage network topologies for switch simulator testing')
    
    subparsers = parser.add_subparsers(dest='command', help='Command to execute')
    
    # Create topology command
    create_parser = subparsers.add_parser('create', help='Create a new network topology')
    create_parser.add_argument('--type', choices=['simple', 'ring', 'mesh'], default='simple',
                            help='Type of topology to create')
    create_parser.add_argument('--switches', type=int, default=2,
                            help='Number of switches in the topology')
    create_parser.add_argument('--hosts', type=int, default=4,
                            help='Number of hosts in the topology')
    create_parser.add_argument('--output', required=True,
                            help='Output JSON file to save the topology')
    
    # Load topology command
    load_parser = subparsers.add_parser('load', help='Load a network topology from a file')
    load_parser.add_argument('--input', required=True,
                          help='Input JSON file to load the topology from')
    load_parser.add_argument('--output', required=True,
                          help='Output JSON file to save the modified topology')
    
    # Visualize topology command
    viz_parser = subparsers.add_parser('visualize', help='Visualize a network topology')
    viz_parser.add_argument('--input', required=True,
                         help='Input JSON file to load the topology from')
    viz_parser.add_argument('--output', required=True,
                         help='Output image file to save the visualization')
    
    args = parser.parse_args()
    
    if args.command == 'create':
        if args.type == 'simple':
            topology = NetworkTopology.create_simple_network(args.switches, args.hosts)
        elif args.type == 'ring':
            topology = NetworkTopology.create_ring_network(args.switches, args.hosts)
        elif args.type == 'mesh':
            topology = NetworkTopology.create_mesh_network(args.switches, args.hosts)
        
        topology.save_to_file(args.output)
        logger.info(f"Topology saved to {args.output}")
    
    elif args.command == 'load':
        topology = NetworkTopology.load_from_file(args.input)
        
        # Here you could add code to modify the topology
        
        topology.save_to_file(args.output)
        logger.info(f"Modified topology saved to {args.output}")
    
    elif args.command == 'visualize':
        try:
            import matplotlib.pyplot as plt
            import networkx as nx
        except ImportError:
            logger.error("Visualization requires matplotlib and networkx. Install with: pip install matplotlib networkx")
            return 1
        
        topology = NetworkTopology.load_from_file(args.input)
        
        # Create a graph from the topology
        G = nx.Graph()
        
        # Add nodes
        for device_id, device in topology.devices.items():
            node_type = device.device_type
            G.add_node(device_id, label=device.name, type=node_type)
        
        # Add edges
        for device1_id, intf1, device2_id, intf2 in topology.connections:
            G.add_edge(device1_id, device2_id, label=f"{intf1} - {intf2}")
        
        # Create the visualization
        plt.figure(figsize=(12, 8))
        
        # Set positions
        pos = nx.spring_layout(G, seed=42)
        
        # Draw nodes
        switch_nodes = [n for n, attr in G.nodes(data=True) if attr.get('type') == 'switch']
        host_nodes = [n for n, attr in G.nodes(data=True) if attr.get('type') == 'host']
        
        nx.draw_networkx_nodes(G, pos, nodelist=switch_nodes, node_color='lightblue', node_size=700)
        nx.draw_networkx_nodes(G, pos, nodelist=host_nodes, node_color='lightgreen', node_size=500)

        # Draw edges
        nx.draw_networkx_edges(G, pos, width=1.5, alpha=0.7)

        # Draw labels
        node_labels = {n: G.nodes[n]['label'] for n in G.nodes()}
        nx.draw_networkx_labels(G, pos, labels=node_labels, font_size=10, font_weight='bold')

        # Add edge labels if not too crowded
        if len(G.edges()) < 20:  # Only show edge labels for smaller graphs
            edge_labels = {(u, v): G.edges[u, v]['label'] for u, v in G.edges()}
            nx.draw_networkx_edge_labels(G, pos, edge_labels=edge_labels, font_size=8)

        plt.title(f"Network Topology: {topology.name}")
        plt.axis('off')  # Turn off axis

        # Save the figure
        plt.tight_layout()
        plt.savefig(args.output, dpi=300, bbox_inches='tight')
        logger.info(f"Visualization saved to {args.output}")
        plt.close()

    else:
        parser.print_help()
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())






