"""
Stats Viewer for Switch Simulator

This module provides interfaces for viewing and analyzing switch statistics,
including port statistics, VLAN usage, MAC table entries, and routing information.
"""

import os
import sys
import time
from typing import Dict, List, Optional, Tuple, Union, Any
import logging
import threading
import json
from datetime import datetime

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# Import SwitchController
try:
    from .switch_controller import SwitchController
except ImportError:
    logger.warning("Could not import SwitchController, some functionality may be limited")


class StatsCollector:
    """Collects and stores statistics from the switch"""
    
    def __init__(self, controller: 'SwitchController'):
        """
        Initialize the stats collector
        
        Args:
            controller: The switch controller instance
        """
        self.controller = controller
        self.stats_history = {
            'ports': {},
            'vlans': {},
            'mac_table': [],
            'routing_table': []
        }
        self._collection_thread = None
        self._running = False
        self._collection_interval = 5  # seconds
    
    def start_collection(self, interval: int = 5) -> bool:
        """
        Start collecting statistics in the background
        
        Args:
            interval: Collection interval in seconds
            
        Returns:
            bool: True if collection started successfully
        """
        if self._running:
            logger.warning("Stats collection is already running")
            return False
        
        self._collection_interval = interval
        self._running = True
        self._collection_thread = threading.Thread(target=self._collection_loop)
        self._collection_thread.daemon = True
        self._collection_thread.start()
        logger.info(f"Started stats collection with interval {interval} seconds")
        return True
    
    def stop_collection(self) -> bool:
        """
        Stop collecting statistics
        
        Returns:
            bool: True if collection was stopped
        """
        if not self._running:
            logger.warning("Stats collection is not running")
            return False
        
        self._running = False
        if self._collection_thread:
            self._collection_thread.join(timeout=2.0)
        logger.info("Stopped stats collection")
        return True
    
    def _collection_loop(self):
        """Background loop for collecting statistics"""
        while self._running:
            try:
                self._collect_stats()
                time.sleep(self._collection_interval)
            except Exception as e:
                logger.error(f"Error in stats collection: {e}")
                time.sleep(1)  # Shorter sleep on error
    
    def _collect_stats(self):
        """Collect current statistics from the switch"""
        timestamp = datetime.now().isoformat()
        
        # Collect port statistics
        for interface in self.controller.get_all_interfaces():
            port_id = interface.port_id
            stats = interface.get_statistics()
            
            if port_id not in self.stats_history['ports']:
                self.stats_history['ports'][port_id] = []
            
            # Add timestamp to stats
            stats['timestamp'] = timestamp
            
            # Store statistics
            self.stats_history['ports'][port_id].append(stats)
            
            # Keep only the last 100 entries to limit memory usage
            if len(self.stats_history['ports'][port_id]) > 100:
                self.stats_history['ports'][port_id].pop(0)
        
        # Here we would also collect VLAN, MAC table and routing statistics
        # For now, this is a placeholder
    
    def get_port_stats_history(self, port_id: int, limit: int = 10) -> List[Dict[str, Any]]:
        """
        Get historical statistics for a port
        
        Args:
            port_id: The port ID
            limit: Maximum number of entries to return
            
        Returns:
            List of statistics dictionaries
        """
        if port_id not in self.stats_history['ports']:
            return []
        
        return self.stats_history['ports'][port_id][-limit:]
    
    def export_stats(self, filename: str) -> bool:
        """
        Export statistics to a JSON file
        
        Args:
            filename: The filename to export to
            
        Returns:
            bool: True if export was successful
        """
        try:
            with open(filename, 'w') as f:
                json.dump(self.stats_history, f, indent=2)
            logger.info(f"Exported statistics to {filename}")
            return True
        except Exception as e:
            logger.error(f"Failed to export statistics: {e}")
            return False


class StatsViewer:
    """Provides methods for viewing and analyzing switch statistics"""
    
    def __init__(self, controller: 'SwitchController'):
        """
        Initialize the stats viewer
        
        Args:
            controller: The switch controller instance
        """
        self.controller = controller
        self.collector = StatsCollector(controller)
    
    def start_collection(self, interval: int = 5) -> bool:
        """
        Start collecting statistics
        
        Args:
            interval: Collection interval in seconds
            
        Returns:
            bool: True if collection started successfully
        """
        return self.collector.start_collection(interval)
    
    def stop_collection(self) -> bool:
        """
        Stop collecting statistics
        
        Returns:
            bool: True if collection was stopped
        """
        return self.collector.stop_collection()
    
    def get_port_statistics(self, port_id: Optional[int] = None) -> Dict[str, Any]:
        """
        Get current statistics for ports
        
        Args:
            port_id: Optional port ID. If None, returns stats for all ports
            
        Returns:
            Dictionary of port statistics
        """
        if port_id:
            interface = self.controller.get_interface(port_id)
            return {port_id: interface.get_statistics()}
        else:
            result = {}
            for interface in self.controller.get_all_interfaces():
                result[interface.port_id] = interface.get_statistics()
            return result
    
    def get_port_utilization(self, port_id: int, time_period: int = 60) -> Dict[str, Any]:
        """
        Calculate port utilization over a time period
        
        Args:
            port_id: The port ID
            time_period: Time period in seconds
            
        Returns:
            Dictionary with utilization statistics
        """
        # Get historical data for the port
        stats_history = self.collector.get_port_stats_history(port_id)
        
        if not stats_history or len(stats_history) < 2:
            return {"error": "Not enough historical data available"}
        
        # Calculate utilization based on delta between oldest and newest entries
        oldest = stats_history[0]
        newest = stats_history[-1]
        
        # Calculate time delta in seconds
        try:
            time_start = datetime.fromisoformat(oldest['timestamp'])
            time_end = datetime.fromisoformat(newest['timestamp'])
            time_delta = (time_end - time_start).total_seconds()
        except (KeyError, ValueError):
            time_delta = time_period  # Fallback
        
        if time_delta <= 0:
            return {"error": "Invalid time period"}
        
        # Calculate byte rate
        rx_bytes_delta = newest['rx_bytes'] - oldest['rx_bytes']
        tx_bytes_delta = newest['tx_bytes'] - oldest['tx_bytes']
        
        rx_rate_bps = (rx_bytes_delta * 8) / time_delta
        tx_rate_bps = (tx_bytes_delta * 8) / time_delta
        
        # Assuming 1Gbps port speed for utilization calculation
        # This should be retrieved from the actual port configuration
        port_speed_bps = 1_000_000_000  # 1 Gbps
        
        rx_utilization = (rx_rate_bps / port_speed_bps) * 100
        tx_utilization = (tx_rate_bps / port_speed_bps) * 100
        
        return {
            "port_id": port_id,
            "time_period_seconds": time_delta,
            "rx_rate_bps": rx_rate_bps,
            "tx_rate_bps": tx_rate_bps,
            "rx_rate_mbps": rx_rate_bps / 1_000_000,
            "tx_rate_mbps": tx_rate_bps / 1_000_000,
            "rx_utilization_percent": rx_utilization,
            "tx_utilization_percent": tx_utilization,
            "total_utilization_percent": (rx_utilization + tx_utilization) / 2
        }
    
    def get_mac_table(self) -> List[Dict[str, Any]]:
        """
        Get the current MAC address table
        
        Returns:
            List of MAC table entries
        """
        # This would call into the controller to get MAC table
        # For now, return a placeholder
        return [
            {"mac_address": "00:11:22:33:44:55", "vlan": 1, "port": 1, "type": "dynamic"},
            {"mac_address": "66:77:88:99:AA:BB", "vlan": 1, "port": 2, "type": "dynamic"}
        ]
    
    def get_vlan_statistics(self) -> Dict[int, Dict[str, Any]]:
        """
        Get statistics for VLANs
        
        Returns:
            Dictionary of VLAN statistics
        """
        # This would call into the controller to get VLAN statistics
        # For now, return a placeholder
        return {
            1: {
                "name": "default",
                "port_count": 48,
                "mac_address_count": 10,
                "active": True
            }
        }
    
    def get_routing_statistics(self) -> Dict[str, Any]:
        """
        Get statistics for the routing table
        
        Returns:
            Dictionary of routing statistics
        """
        # This would call into the controller to get routing statistics
        # For now, return a placeholder
        return {
            "total_routes": 1,
            "static_routes": 1,
            "dynamic_routes": 0,
            "default_route_present": True
        }
    
    def export_all_stats(self, filename: str) -> bool:
        """
        Export all statistics to a file
        
        Args:
            filename: The filename to export to
            
        Returns:
            bool: True if export was successful
        """
        return self.collector.export_stats(filename)
    
    def generate_report(self, output_format: str = 'text') -> str:
        """
        Generate a comprehensive statistics report
        
        Args:
            output_format: Format for the report ('text', 'json', 'html')
            
        Returns:
            Report in the requested format
        """
        # Collect all relevant statistics
        port_stats = self.get_port_statistics()
        vlan_stats = self.get_vlan_statistics()
        mac_table = self.get_mac_table()
        routing_stats = self.get_routing_statistics()
        
        # Compile the report data
        report_data = {
            "generated_at": datetime.now().isoformat(),
            "switch_info": {
                "hostname": self.controller.config.hostname,
                "port_count": self.controller.config.num_ports
            },
            "port_statistics": port_stats,
            "vlan_statistics": vlan_stats,
            "mac_table": mac_table,
            "routing_statistics": routing_stats
        }
        
        # Generate the report in the requested format
        if output_format == 'json':
            return json.dumps(report_data, indent=2)
        elif output_format == 'html':
            # A very simple HTML report
            html = "<html><head><title>Switch Simulator Report</title></head><body>"
            html += f"<h1>Switch Report: {report_data['switch_info']['hostname']}</h1>"
            html += f"<p>Generated at: {report_data['generated_at']}</p>"
            
            html += "<h2>Port Statistics</h2>"
            html += "<table border='1'><tr><th>Port</th><th>Status</th><th>RX Packets</th><th>TX Packets</th></tr>"
            for port_id, stats in port_stats.items():
                html += f"<tr><td>{port_id}</td><td>{stats['link_status']}</td>"
                html += f"<td>{stats['rx_packets']}</td><td>{stats['tx_packets']}</td></tr>"
            html += "</table>"
            
            # Add other sections...
            html += "</body></html>"
            return html
        else:
            # Default text format
            lines = [
                f"Switch Report: {report_data['switch_info']['hostname']}",
                f"Generated at: {report_data['generated_at']}",
                f"Port count: {report_data['switch_info']['port_count']}",
                "",
                "Port Statistics:",
            ]
            
            for port_id, stats in port_stats.items():
                lines.append(f"  Port {port_id}: {stats['link_status']}, "
                            f"RX: {stats['rx_packets']} packets, "
                            f"TX: {stats['tx_packets']} packets")
            
            lines.append("")
            lines.append("VLAN Statistics:")
            for vlan_id, stats in vlan_stats.items():
                lines.append(f"  VLAN {vlan_id} ({stats['name']}): "
                            f"{stats['port_count']} ports, "
                            f"{stats['mac_address_count']} MAC addresses")
            
            # Add other sections...
            
            return "\n".join(lines)
