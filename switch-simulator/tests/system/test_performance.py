#!/usr/bin/env python3
"""
System tests for performance evaluation of the switch simulator
"""

import sys
import os
import time
import unittest
import subprocess
import signal
import json
import matplotlib.pyplot as plt
import numpy as np
from threading import Thread

# Add the parent directory to the path so we can import the Python API
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '../../python')))

from api.switch_controller import SwitchController
from api.stats_viewer import StatsViewer

class TestPerformance(unittest.TestCase):
    """Test cases for performance evaluation"""
    
    @classmethod
    def setUpClass(cls):
        """Start the switch simulator process"""
        # Path to the simulator executable
        cls.simulator_path = os.path.abspath(os.path.join(os.path.dirname(__file__), 
                                             '../../build/switch-simulator'))
        
        # Start the simulator in a separate process
        cls.simulator_process = subprocess.Popen([cls.simulator_path, '--config=perf_config.json'],
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
        
        # Create results directory
        cls.results_dir = os.path.join(os.path.dirname(__file__), 'performance_results')
        os.makedirs(cls.results_dir, exist_ok=True)
    
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
        """Setup the test topology for performance evaluation"""
        # Configure ports (all 24 ports)
        for port_id in range(24):
            cls.controller.configure_port(port_id, 'up', '10G', 'full')
        
        # Configure VLANs
        for vlan_id in range(10, 110):  # 100 VLANs
            cls.controller.create_vlan(vlan_id, f'vlan_{vlan_id}')
        
        # Distribute ports across VLANs
        for port_id in range(24):
            vlan_id = 10 + (port_id % 10)  # Spread ports across 10 VLANs
            cls.controller.add_port_to_vlan(vlan_id, port_id, 'untagged')
        
        # Configure trunk ports
        for port_id in range(20, 24):
            for vlan_id in range(10, 110):
                cls.controller.add_port_to_vlan(vlan_id, port_id, 'tagged')
        
        # Configure IP interfaces for each VLAN
        for vlan_id in range(10, 110):
            third_octet = vlan_id
            cls.controller.create_interface(f'vlan{vlan_id}', f'192.168.{third_octet}.1', '255.255.255.0', vlan_id)
        
        # Add static routes (100 routes)
        for i in range(100):
            cls.controller.add_static_route(f'10.{i}.0.0', '255.255.255.0', '192.168.10.254')
        
        # Pre-populate MAC table with 10,000 entries
        print("Pre-populating MAC table - this may take a while...")
        for i in range(10000):
            # Generate MAC address
            mac = f'00:{i//65536:02x}:{(i//256)%256:02x}:{i%256:02x}:00:01'
            vlan_id = 10 + (i % 100)
            port_id = i % 24
            
            if i % 1000 == 0:
                print(f"Added {i} MAC entries...")
            
            cls.controller.add_static_mac(mac, vlan_id, port_id)
    
    def _generate_test_packet(self, packet_type, src_port):
        """Generate a test packet based on type"""
        if packet_type == 'l2_unicast':
            # Random destination MAC from our pre-populated table
            dst_mac_id = np.random.randint(0, 10000)
            dst_mac = f'00:{dst_mac_id//65536:02x}:{(dst_mac_id//256)%256:02x}:{dst_mac_id%256:02x}:00:01'
            
            vlan_id = 10 + (src_port % 10)
            
            return {
                'src_mac': f'00:00:00:00:00:{src_port:02x}',
                'dst_mac': dst_mac,
                'vlan_id': vlan_id,
                'type': 'ethernet',
                'payload': 'Test L2 unicast performance'
            }
            
        elif packet_type == 'l2_broadcast':
            vlan_id = 10 + (src_port % 10)
            
            return {
                'src_mac': f'00:00:00:00:00:{src_port:02x}',
                'dst_mac': 'FF:FF:FF:FF:FF:FF',
                'vlan_id': vlan_id,
                'type': 'ethernet',
                'payload': 'Test L2 broadcast performance'
            }
            
        elif packet_type == 'l3_routing':
            src_vlan = 10 + (src_port % 10)
            dst_vlan = 10 + ((src_port + 5) % 10)  # Pick a different VLAN
            
            return {
                'src_mac': f'00:00:00:00:00:{src_port:02x}',
                'dst_mac': '00:00:00:00:00:01',  # Router MAC
                'src_ip': f'192.168.{src_vlan}.100',
                'dst_ip': f'192.168.{dst_vlan}.100',
                'vlan_id': src_vlan,
                'type': 'ipv4',
                'ttl': 64,
                'protocol': 6,
                'payload': 'Test L3 routing performance'
            }
            
        else:  # l3_static_route
            src_vlan = 10 + (src_port % 10)
            route_id = np.random.randint(0, 100)
            
            return {
                'src_mac': f'00:00:00:00:00:{src_port:02x}',
                'dst_mac': '00:00:00:00:00:01',  # Router MAC
                'src_ip': f'192.168.{src_vlan}.100',
                'dst_ip': f'10.{route_id}.0.100',
                'vlan_id': src_vlan,
                'type': 'ipv4',
                'ttl': 64,
                'protocol': 6,
                'payload': 'Test L3 static route performance'
            }
    
    def _measure_packet_processing_time(self, packet_type, num_packets):
        """Measure the time to process packets of a specific type"""
        processing_times = []
        
        for i in range(num_packets):
            src_port = i % 24
            packet = self._generate_test_packet(packet_type, src_port)
            
            start_time = time.time()
            result = self.controller.send_test_packet(src_port, packet)
            end_time = time.time()
            
            processing_time = (end_time - start_time) * 1000  # Convert to ms
            processing_times.append(processing_time)
        
        return processing_times
    
    def _plot_results(self, results, test_name):
        """Plot and save the performance test results"""
        plt.figure(figsize=(12, 8))
        
        # Plot histogram
        plt.subplot(2, 1, 1)
        for packet_type, times in results.items():
            plt.hist(times, alpha=0.7, label=packet_type, bins=20)
        
        plt.xlabel('Processing Time (ms)')
        plt.ylabel('Frequency')
        plt.title(f'{test_name} - Processing Time Distribution')
        plt.legend()
        plt.grid(True)
        
        # Plot box plot
        plt.subplot(2, 1, 2)
        data = [times for packet_type, times in results.items()]
        labels = list(results.keys())
        
        plt.boxplot(data, labels=labels)
        plt.ylabel('Processing Time (ms)')
        plt.title(f'{test_name} - Processing Time Comparison')
        plt.grid(True)
        
        plt.tight_layout()
        plt.savefig(os.path.join(self.results_dir, f'{test_name}.png'))
        
        # Save raw data
        with open(os.path.join(self.results_dir, f'{test_name}_data.json'), 'w') as f:
            json.dump(results, f)
        
        # Calculate and save statistics
        stats = {}
        for packet_type, times in results.items():
            stats[packet_type] = {
                'min': min(times),
                'max': max(times),
                'mean': np.mean(times),
                'median': np.median(times),
                'std_dev': np.std(times),
                'p95': np.percentile(times, 95),
                'p99': np.percentile(times, 99)
            }
        
        with open(os.path.join(self.results_dir, f'{test_name}_stats.json'), 'w') as f:
            json.dump(stats, f, indent=2)
    
    def test_packet_processing_performance(self):
        """Test packet processing performance for different packet types"""
        num_packets = 1000  # Number of packets per type
        
        results = {}
        
        # Measure L2 unicast packet processing
        print("Measuring L2 unicast performance...")
        results['l2_unicast'] = self._measure_packet_processing_time('l2_unicast', num_packets)
        
        # Measure L2 broadcast packet processing
        print("Measuring L2 broadcast performance...")
        results['l2_broadcast'] = self._measure_packet_processing_time('l2_broadcast', num_packets)
        
        # Measure L3 inter-VLAN routing performance
        print("Measuring L3 routing performance...")
        results['l3_routing'] = self._measure_packet_processing_time('l3_routing', num_packets)
        
        # Measure L3 static route performance
        print("Measuring L3 static route performance...")
        results['l3_static_route'] = self._measure_packet_processing_time('l3_static_route', num_packets)
        
        # Plot and save results
        self._plot_results(results, 'packet_processing_performance')
        
        # Print summary statistics
        for packet_type, times in results.items():
            print(f"\n{packet_type} Performance:")
            print(f"  Minimum: {min(times):.2f} ms")
            print(f"  Maximum: {max(times):.2f} ms")
            print(f"  Average: {np.mean(times):.2f} ms")
            print(f"  95th percentile: {np.percentile(times, 95):.2f} ms")
    
    def test_mac_table_scaling(self):
        """Test MAC table lookup performance with increasing table size"""
        print("Testing MAC table scaling performance...")
        max_entries = 50000
        step_size = 5000
        lookup_iterations = 100
        
        entry_counts = list(range(0, max_entries + 1, step_size))
        if entry_counts[0] == 0:
            entry_counts[0] = 10000  # We already added 10,000 in setup
        
        lookup_times = []
        
        # Get current MAC table size
        mac_table_info = self.stats_viewer.get_mac_table_stats()
        initial_size = mac_table_info['total_entries']
        
        # Test performance at increasing MAC table sizes
        for target_size in entry_counts:
            current_size = initial_size
            
            # Add entries if needed
            if target_size > current_size:
                print(f"Adding entries to reach {target_size}...")
                for i in range(current_size, target_size):
                    mac = f'01:{i//65536:02x}:{(i//256)%256:02x}:{i%256:02x}:00:01'
                    vlan_id = 10 + (i % 100)
                    port_id = i % 24
                    
                    if i % 1000 == 0:
                        print(f"Added {i-current_size} of {target_size-current_size} entries...")
                    
                    self.controller.add_static_mac(mac, vlan_id, port_id)
            
            # Measure lookup time
            print(f"Measuring lookup performance at {target_size} entries...")
            times = []
            
            for i in range(lookup_iterations):
                # Generate random MAC address lookup
                lookup_id = np.random.randint(0, target_size)
                
                if lookup_id < 10000:
                    # Format for first 10,000 entries
                    lookup_mac = f'00:{lookup_id//65536:02x}:{(lookup_id//256)%256:02x}:{lookup_id%256:02x}:00:01'
                else:
                    # Format for additional entries
                    lookup_mac = f'01:{lookup_id//65536:02x}:{(lookup_id//256)%256:02x}:{lookup_id%256:02x}:00:01'
                
                lookup_vlan = 10 + (lookup_id % 100)
                
                start_time = time.time()
                self.controller.lookup_mac(lookup_mac, lookup_vlan)
                end_time = time.time()
                
                lookup_time = (end_time - start_time) * 1000  # Convert to ms
                times.append(lookup_time)
            
            avg_time = np.mean(times)
            lookup_times.append(avg_time)
            
            print(f"Average lookup time at {target_size} entries: {avg_time:.2f} ms")
        
        # Plot results
        plt.figure(figsize=(10, 6))
        plt.plot(entry_counts, lookup_times, marker='o')
        plt.xlabel('MAC Table Size (entries)')
        plt.ylabel('Average Lookup Time (ms)')
        plt.title('MAC Table Lookup Performance Scaling')
        plt.grid(True)
        plt.savefig(os.path.join(self.results_dir, 'mac_table_scaling.png'))
        
        # Save raw data
        scaling_data = {
            'entry_counts': entry_counts,
            'lookup_times': lookup_times
        }
        
        with open(os.path.join(self.results_dir, 'mac_table_scaling_data.json'), 'w') as f:
            json.dump(scaling_data, f)

if __name__ == '__main__':
    unittest.main()
