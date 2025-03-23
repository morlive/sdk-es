#!/usr/bin/env python3
"""
System test for network scenarios in the switch simulator
"""

import sys
import os
import time
import unittest
import subprocess
import signal
from threading import Thread

# Add the parent directory to the path so we can import the Python API
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '../../python')))

from api.switch_controller import SwitchController
from api.stats_viewer import StatsViewer

class TestNetworkScenarios(unittest.TestCase):
    """Test cases for various network scenarios"""
    
    @classmethod
    def setUpClass(cls):
        """Start the switch simulator process"""
        # Path to the simulator executable
        cls.simulator_path = os.path.abspath(os.path.join(os.path.dirname(__file__), 
                                             '../../build/switch-simulator'))
        
        # Start the simulator in a separate process
        cls.simulator_process = subprocess.Popen([cls.simulator_path, '--config=test_config.json'],
                                                stdout=subprocess.PIPE,
                                                stderr=subprocess.PIPE,
                                                universal_newlines=True)
        
        # Wait for simulator to initialize
        time.sleep(2)
        
        # Create API controller instance
        cls.controller = SwitchController('localhost', 8000)
        cls.stats_viewer = StatsViewer('localhost', 8000)
        
        # Setup test topology
        cls._setup_test_topology()
    
    @classmethod
    def tearDownClass(cls):
        """Stop the switch simulator process"""
        # Send SIGTERM to the simulator process
        if cls.simulator_process:
            cls.simulator_process.terminate()
            try:
                cls.simulator_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                cls.simulator_process.kill()
            
            # Get output for debugging
            stdout, stderr = cls.simulator_process.communicate()
            if stderr:
                print(f"Simulator stderr: {stderr}")
    
    @classmethod
    def _setup_test_topology(cls):
        """Setup the test topology for the network scenarios"""
        # Configure ports
        for port_id in range(8):
            cls.controller.configure_port(port_id, 'up', '1G', 'full')
        
        # Configure VLANs
        cls.controller.create_vlan(10, 'data_vlan')
        cls.controller.create_vlan(20, 'voice_vlan')
        
        # Configure port VLAN membership
        # Ports 0, 1 in VLAN 10 as untagged
        cls.controller.add_port_to_vlan(10, 0, 'untagged')
        cls.controller.add_port_to_vlan(10, 1, 'untagged')
        
        # Ports 2, 3 in VLAN 20 as untagged
        cls.controller.add_port_to_vlan(20, 2, 'untagged')
        cls.controller.add_port_to_vlan(20, 3, 'untagged')
        
        # Ports 4, 5 as trunk ports for both VLANs
        cls.controller.add_port_to_vlan(10, 4, 'tagged')
        cls.controller.add_port_to_vlan(20, 4, 'tagged')
        cls.controller.add_port_to_vlan(10, 5, 'tagged')
        cls.controller.add_port_to_vlan(20, 5, 'tagged')
        
        # Configure IP interfaces
        cls.controller.create_interface('vlan10', '192.168.10.1', '255.255.255.0', 10)
        cls.controller.create_interface('vlan20', '192.168.20.1', '255.255.255.0', 20)
        
        # Configure static MAC entries
        cls.controller.add_static_mac('00:11:22:33:44:55', 10, 0)
        cls.controller.add_static_mac('00:11:22:33:44:66', 10, 1)
        cls.controller.add_static_mac('00:11:22:33:44:77', 20, 2)
        cls.controller.add_static_mac('00:11:22:33:44:88', 20, 3)
        
        # Configure static routes
        cls.controller.add_static_route('10.0.0.0', '255.255.255.0', '192.168.10.254')
        cls.controller.add_static_route('172.16.0.0', '255.255.0.0', '192.168.20.254')
    
    def test_l2_switching_same_vlan(self):
        """Test L2 switching between ports in the same VLAN"""
        # Create a test packet from port 0 to port 1 (VLAN 10)
        packet = {
            'src_mac': '00:11:22:33:44:55',
            'dst_mac': '00:11:22:33:44:66',
            'vlan_id': 10,
            'type': 'ethernet',
            'payload': 'Test L2 same VLAN'
        }
        
        # Send the packet and get the result
        result = self.controller.send_test_packet(0, packet)
        
        # Verify packet was forwarded correctly
        self.assertEqual(result['status'], 'success')
        self.assertEqual(result['egress_port'], 1)
        self.assertEqual(result['forwarding_reason'], 'unicast_dst')
        
        # Check the stats were updated
        port_stats = self.stats_viewer.get_port_stats(0)
        self.assertGreater(port_stats['tx_packets'], 0)
        
        port_stats = self.stats_viewer.get_port_stats(1)
        self.assertGreater(port_stats['rx_packets'], 0)
    
    def test_l2_switching_different_vlans(self):
        """Test L2 switching between ports in different VLANs (shouldn't work)"""
        # Create a test packet from port 0 (VLAN 10) to port 2 (VLAN 20)
        packet = {
            'src_mac': '00:11:22:33:44:55',
            'dst_mac': '00:11:22:33:44:77',
            'vlan_id': 10,
            'type': 'ethernet',
            'payload': 'Test L2 different VLANs'
        }
        
        # Send the packet and get the result
        result = self.controller.send_test_packet(0, packet)
        
        # Verify packet was not forwarded to port 2
        self.assertEqual(result['status'], 'dropped')
        self.assertEqual(result['drop_reason'], 'dst_not_found_in_vlan')
        
        # Packet might be flooded within VLAN 10, but not to port 2
        port_stats = self.stats_viewer.get_port_stats(2)
        rx_before = port_stats['rx_packets']
        
        # Send again
        self.controller.send_test_packet(0, packet)
        
        # Check stats - should be unchanged for port 2
        port_stats = self.stats_viewer.get_port_stats(2)
        self.assertEqual(port_stats['rx_packets'], rx_before)
    
    def test_l2_trunk_port(self):
        """Test L2 switching through a trunk port"""
        # Create a test packet from port 0 to trunk port 4 (both VLAN 10)
        packet = {
            'src_mac': '00:11:22:33:44:55', 
            'dst_mac': '00:11:22:33:44:99',  # MAC learned on trunk port
            'vlan_id': 10,
            'type': 'ethernet',
            'payload': 'Test L2 trunk port'
        }
        
        # First, add the MAC to simulate it being learned on the trunk port
        self.controller.add_static_mac('00:11:22:33:44:99', 10, 4)
        
        # Send the packet and get the result
        result = self.controller.send_test_packet(0, packet)
        
        # Verify packet was forwarded correctly and tagged
        self.assertEqual(result['status'], 'success')
        self.assertEqual(result['egress_port'], 4)
        self.assertTrue(result['has_vlan_tag'])
        self.assertEqual(result['vlan_id'], 10)
    
    def test_l2_broadcast(self):
        """Test L2 broadcast forwarding"""
        # Create a broadcast packet from port 0
        packet = {
            'src_mac': '00:11:22:33:44:55',
            'dst_mac': 'FF:FF:FF:FF:FF:FF',
            'vlan_id': 10,
            'type': 'ethernet',
            'payload': 'Test L2 broadcast'
        }
        
        # Get port stats before
        port0_stats_before = self.stats_viewer.get_port_stats(0)
        port1_stats_before = self.stats_viewer.get_port_stats(1)
        port2_stats_before = self.stats_viewer.get_port_stats(2)
        port4_stats_before = self.stats_viewer.get_port_stats(4)
        
        # Send the packet and get the result
        result = self.controller.send_test_packet(0, packet)
        
        # Verify packet was broadcast within VLAN
        self.assertEqual(result['status'], 'broadcast')
        
        # Check stats after
        port0_stats_after = self.stats_viewer.get_port_stats(0)
        port1_stats_after = self.stats_viewer.get_port_stats(1)
        port2_stats_after = self.stats_viewer.get_port_stats(2)
        port4_stats_after = self.stats_viewer.get_port_stats(4)
        
        # Port 0 (source) - TX should increase
        self.assertGreater(port0_stats_after['tx_packets'], port0_stats_before['tx_packets'])
        
        # Port 1 (same VLAN) - RX should increase
        self.assertGreater(port1_stats_after['rx_packets'], port1_stats_before['rx_packets'])
        
        # Port 2 (different VLAN) - RX should not change
        self.assertEqual(port2_stats_after['rx_packets'], port2_stats_before['rx_packets'])
        
        # Port 4 (trunk in VLAN 10) - RX should increase
        self.assertGreater(port4_stats_after['rx_packets'], port4_stats_before['rx_packets'])
    
    def test_l3_routing_between_vlans(self):
        """Test L3 routing between VLANs"""
        # Create an IP packet from VLAN 10 to VLAN 20
        packet = {
            'src_mac': '00:11:22:33:44:55',
            'dst_mac': '00:00:00:00:00:01',  # Router MAC in VLAN 10
            'src_ip': '192.168.10.100',
            'dst_ip': '192.168.20.100',
            'vlan_id': 10,
            'type': 'ipv4',
            'ttl': 64,
            'protocol': 6,  # TCP
            'payload': 'Test L3 inter-VLAN routing'
        }
        
        # First, add the destination MAC in VLAN 20
        self.controller.add_static_mac('00:aa:bb:cc:dd:ee', 20, 2)
        
        # Add ARP entry for destination IP
        self.controller.add_arp_entry('192.168.20.100', '00:aa:bb:cc:dd:ee')
        
        # Send the packet and get the result
        result = self.controller.send_test_packet(0, packet)
        
        # Verify packet was routed correctly
        self.assertEqual(result['status'], 'routed')
        self.assertEqual(result['egress_port'], 2)  # Port in VLAN 20
        self.assertEqual(result['vlan_id'], 20)     # Changed VLAN
        self.assertEqual(result['ttl'], 63)         # Decremented TTL
        
        # Source MAC should be router's MAC in VLAN 20
        self.assertEqual(result['src_mac'], '00:00:00:00:00:02')
        # Destination MAC should be the ARP-resolved MAC
        self.assertEqual(result['dst_mac'], '00:aa:bb:cc:dd:ee')
    
    def test_l3_static_route(self):
        """Test L3 routing via static route"""
        # Create an IP packet to a network reachable via static route
        packet = {
            'src_mac': '00:11:22:33:44:55',
            'dst_mac': '00:00:00:00:00:01',  # Router MAC in VLAN 10
            'src_ip': '192.168.10.100',
            'dst_ip': '10.0.0.100',  # In the static route network
            'vlan_id': 10,
            'type': 'ipv4',
            'ttl': 64,
            'protocol': 6,  # TCP
            'payload': 'Test L3 static route'
        }
        
        # Add ARP entry for next hop
        self.controller.add_arp_entry('192.168.10.254', '00:aa:bb:cc:dd:ff')
        
        # Send the packet and get the result
        result = self.controller.send_test_packet(0, packet)
        
        # Verify packet was routed correctly
        self.assertEqual(result['status'], 'routed')
        self.assertEqual(result['ttl'], 63)  # Decremented TTL
        
        # Destination MAC should be the next hop's MAC
        self.assertEqual(result['dst_mac'], '00:aa:bb:cc:dd:ff')
    
    def test_acl_filtering(self):
        """Test ACL packet filtering"""
        # Configure ACL to block TCP traffic from 192.168.10.100 to 192.168.20.100
        self.controller.add_acl_rule({
            'priority': 10,
            'action': 'deny',
            'src_ip': '192.168.10.100',
            'dst_ip': '192.168.20.100',
            'protocol': 6  # TCP
        })
        
        # Create a packet that should be blocked
        packet = {
            'src_mac': '00:11:22:33:44:55',
            'dst_mac': '00:00:00:00:00:01',
            'src_ip': '192.168.10.100',
            'dst_ip': '192.168.20.100',
            'vlan_id': 10,
            'type': 'ipv4',
            'ttl': 64,
            'protocol': 6,  # TCP
            'payload': 'Test ACL filtering - TCP'
        }
        
        # Send the packet and get the result
        result = self.controller.send_test_packet(0, packet)
        
        # Verify packet was blocked by ACL
        self.assertEqual(result['status'], 'dropped')
        self.assertEqual(result['drop_reason'], 'acl_deny')
        
        # Now try with UDP (not blocked)
        packet['protocol'] = 17  # UDP
        packet['payload'] = 'Test ACL filtering - UDP'
        
        result = self.controller.send_test_packet(0, packet)
        
        # Verify packet was not blocked
        self.assertNotEqual(result['status'], 'dropped')
        
        # Clean up ACL rule
        self.controller.delete_acl_rule(10)
    
    def test_qos_priority(self):
        """Test QoS priority handling"""
        # Configure QoS to mark DSCP based on source IP
        self.controller.add_qos_rule({
            'priority': 10,
            'src_ip': '192.168.10.100',
            'action': 'mark',
            'dscp': 46  # Expedited Forwarding
        })
        
        # Create a packet that should be marked
        packet = {
            'src_mac': '00:11:22:33:44:55',
            'dst_mac': '00:00:00:00:00:01',
            'src_ip': '192.168.10.100',
            'dst_ip': '192.168.20.100',
            'vlan_id': 10,
            'type': 'ipv4',
            'ttl': 64,
            'protocol': 6,
            'dscp': 0,  # Default
            'payload': 'Test QoS priority'
        }
        
        # Send the packet and get the result
        result = self.controller.send_test_packet(0, packet)
        
        # Verify DSCP was marked
        self.assertEqual(result['dscp'], 46)
        
        # Clean up QoS rule
        self.controller.delete_qos_rule(10)

if __name__ == '__main__':
    unittest.main()









