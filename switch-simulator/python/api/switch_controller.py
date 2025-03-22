"""
Switch Controller API for Switch Simulator

This module provides the main interface for controlling the switch simulator,
including port configuration, VLAN setup, and routing table management.
"""

import os
import sys
import ctypes
import logging
from enum import Enum, auto
from typing import Dict, List, Optional, Tuple, Union, Any

# Add the parent directory to sys.path to access C library
sys.path.append(os.path.join(os.path.dirname(__file__), '../..'))

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# Load the C library
try:
    _lib_path = os.path.join(os.path.dirname(__file__), '../../build/libswitch_simulator.so')
    _switch_lib = ctypes.CDLL(_lib_path)
    logger.info(f"Successfully loaded switch simulator library from {_lib_path}")
except OSError as e:
    logger.error(f"Failed to load switch simulator library: {e}")
    raise RuntimeError(f"Could not load simulator library. Have you built the project? Error: {e}")


class SwitchStatus(Enum):
    """Status codes for switch operations"""
    OK = 0
    ERROR = auto()
    PORT_NOT_FOUND = auto()
    VLAN_NOT_FOUND = auto()
    INVALID_CONFIG = auto()
    HARDWARE_ERROR = auto()
    NOT_IMPLEMENTED = auto()


class PortSpeed(Enum):
    """Available port speeds in Mbps"""
    SPEED_10 = 10
    SPEED_100 = 100
    SPEED_1000 = 1000
    SPEED_10000 = 10000
    SPEED_25000 = 25000
    SPEED_40000 = 40000
    SPEED_100000 = 100000


class PortDuplex(Enum):
    """Port duplex settings"""
    HALF = 0
    FULL = 1


class SwitchInterface:
    """Represents a single interface on the switch"""
    
    def __init__(self, port_id: int, controller: 'SwitchController'):
        self.port_id = port_id
        self._controller = controller
        self.name = f"Port{port_id}"
    
    def enable(self) -> SwitchStatus:
        """Enable this interface"""
        return self._controller.set_port_status(self.port_id, True)
    
    def disable(self) -> SwitchStatus:
        """Disable this interface"""
        return self._controller.set_port_status(self.port_id, False)
    
    def configure(self, speed: PortSpeed = None, duplex: PortDuplex = None, 
                  description: str = None) -> SwitchStatus:
        """Configure interface parameters"""
        return self._controller.configure_port(
            self.port_id, speed=speed, duplex=duplex, description=description
        )
    
    def add_to_vlan(self, vlan_id: int, tagged: bool = False) -> SwitchStatus:
        """Add this interface to a VLAN"""
        return self._controller.add_port_to_vlan(self.port_id, vlan_id, tagged)
    
    def remove_from_vlan(self, vlan_id: int) -> SwitchStatus:
        """Remove this interface from a VLAN"""
        return self._controller.remove_port_from_vlan(self.port_id, vlan_id)
    
    def get_statistics(self) -> Dict[str, Any]:
        """Get interface statistics"""
        return self._controller.get_port_statistics(self.port_id)
    
    def __repr__(self) -> str:
        return f"<SwitchInterface: {self.name} (id={self.port_id})>"


class SwitchConfig:
    """Configuration data structure for the switch"""
    
    def __init__(self):
        self.hostname = "switch-simulator"
        self.mac_table_size = 4096
        self.routing_table_size = 1024
        self.num_ports = 48
        self.default_vlan = 1
        # Additional configuration options can be added here
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert configuration to dictionary"""
        return {
            "hostname": self.hostname,
            "mac_table_size": self.mac_table_size,
            "routing_table_size": self.routing_table_size,
            "num_ports": self.num_ports,
            "default_vlan": self.default_vlan
        }
    
    @classmethod
    def from_dict(cls, config_dict: Dict[str, Any]) -> 'SwitchConfig':
        """Create configuration from dictionary"""
        config = cls()
        for key, value in config_dict.items():
            if hasattr(config, key):
                setattr(config, key, value)
        return config


class SwitchController:
    """Main controller class for interacting with the switch simulator"""
    
    def __init__(self, config: Optional[SwitchConfig] = None):
        """
        Initialize the switch controller
        
        Args:
            config: Optional configuration for the switch
        """
        self.config = config or SwitchConfig()
        self._interfaces = {}
        self._initialized = False
        
        # Define C function signatures
        self._configure_function_signatures()
    
    def _configure_function_signatures(self):
        """Configure the signatures for C library functions"""
        if not hasattr(self, '_switch_lib'):
            self._switch_lib = _switch_lib
        
        # Initialize switch
        self._switch_lib.switch_initialize.argtypes = [ctypes.c_int]
        self._switch_lib.switch_initialize.restype = ctypes.c_int
        
        # Port functions
        self._switch_lib.set_port_status.argtypes = [ctypes.c_int, ctypes.c_bool]
        self._switch_lib.set_port_status.restype = ctypes.c_int
        
        self._switch_lib.configure_port.argtypes = [
            ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_char_p
        ]
        self._switch_lib.configure_port.restype = ctypes.c_int
        
        # VLAN functions
        self._switch_lib.create_vlan.argtypes = [ctypes.c_int, ctypes.c_char_p]
        self._switch_lib.create_vlan.restype = ctypes.c_int
        
        self._switch_lib.add_port_to_vlan.argtypes = [
            ctypes.c_int, ctypes.c_int, ctypes.c_bool
        ]
        self._switch_lib.add_port_to_vlan.restype = ctypes.c_int
        
        # Other functions will be added as needed
    
    def initialize(self) -> SwitchStatus:
        """Initialize the switch simulator"""
        if self._initialized:
            logger.warning("Switch is already initialized")
            return SwitchStatus.OK
        
        result = self._switch_lib.switch_initialize(self.config.num_ports)
        if result == 0:
            self._initialized = True
            
            # Create interface objects
            for port_id in range(1, self.config.num_ports + 1):
                self._interfaces[port_id] = SwitchInterface(port_id, self)
            
            logger.info(f"Switch initialized with {self.config.num_ports} ports")
            return SwitchStatus.OK
        else:
            logger.error(f"Failed to initialize switch: error code {result}")
            return SwitchStatus.ERROR
    
    def shutdown(self) -> SwitchStatus:
        """Shutdown the switch simulator"""
        if not self._initialized:
            logger.warning("Switch is not initialized")
            return SwitchStatus.OK
        
        result = self._switch_lib.switch_shutdown()
        if result == 0:
            self._initialized = False
            logger.info("Switch shut down")
            return SwitchStatus.OK
        else:
            logger.error(f"Failed to shut down switch: error code {result}")
            return SwitchStatus.ERROR
    
    def get_interface(self, port_id: int) -> SwitchInterface:
        """Get interface object by port ID"""
        if not self._initialized:
            self.initialize()
        
        if port_id not in self._interfaces:
            raise ValueError(f"Invalid port ID: {port_id}")
        
        return self._interfaces[port_id]
    
    def get_all_interfaces(self) -> List[SwitchInterface]:
        """Get all interfaces"""
        if not self._initialized:
            self.initialize()
        
        return list(self._interfaces.values())
    
    # Port configuration methods
    def set_port_status(self, port_id: int, enabled: bool) -> SwitchStatus:
        """Set port status (enabled/disabled)"""
        if not self._initialized:
            self.initialize()
        
        result = self._switch_lib.set_port_status(port_id, enabled)
        status = SwitchStatus.OK if result == 0 else SwitchStatus.ERROR
        
        if status == SwitchStatus.OK:
            logger.info(f"Port {port_id} {'enabled' if enabled else 'disabled'}")
        else:
            logger.error(f"Failed to set port {port_id} status: error code {result}")
        
        return status
    
    def configure_port(self, port_id: int, speed: Optional[PortSpeed] = None, 
                      duplex: Optional[PortDuplex] = None, 
                      description: Optional[str] = None) -> SwitchStatus:
        """Configure port parameters"""
        if not self._initialized:
            self.initialize()
        
        speed_val = speed.value if speed else 0
        duplex_val = duplex.value if duplex else 0
        desc_str = description.encode('utf-8') if description else ctypes.c_char_p(0)
        
        result = self._switch_lib.configure_port(port_id, speed_val, duplex_val, desc_str)
        status = SwitchStatus.OK if result == 0 else SwitchStatus.ERROR
        
        if status == SwitchStatus.OK:
            logger.info(f"Configured port {port_id}")
        else:
            logger.error(f"Failed to configure port {port_id}: error code {result}")
        
        return status
    
    def get_port_statistics(self, port_id: int) -> Dict[str, Any]:
        """Get port statistics"""
        if not self._initialized:
            self.initialize()
        
        # This would typically call into the C library to get statistics
        # For now, we'll return a placeholder dictionary
        return {
            "rx_packets": 0,
            "tx_packets": 0,
            "rx_bytes": 0,
            "tx_bytes": 0,
            "rx_errors": 0,
            "tx_errors": 0,
            "rx_dropped": 0,
            "tx_dropped": 0,
            "link_status": "up"
        }
    
    # VLAN methods
    def create_vlan(self, vlan_id: int, name: Optional[str] = None) -> SwitchStatus:
        """Create a new VLAN"""
        if not self._initialized:
            self.initialize()
        
        name_str = name.encode('utf-8') if name else ctypes.c_char_p(0)
        result = self._switch_lib.create_vlan(vlan_id, name_str)
        status = SwitchStatus.OK if result == 0 else SwitchStatus.ERROR
        
        if status == SwitchStatus.OK:
            logger.info(f"Created VLAN {vlan_id}{f' ({name})' if name else ''}")
        else:
            logger.error(f"Failed to create VLAN {vlan_id}: error code {result}")
        
        return status
    
    def delete_vlan(self, vlan_id: int) -> SwitchStatus:
        """Delete a VLAN"""
        if not self._initialized:
            self.initialize()
        
        result = self._switch_lib.delete_vlan(vlan_id)
        status = SwitchStatus.OK if result == 0 else SwitchStatus.ERROR
        
        if status == SwitchStatus.OK:
            logger.info(f"Deleted VLAN {vlan_id}")
        else:
            logger.error(f"Failed to delete VLAN {vlan_id}: error code {result}")
        
        return status
    
    def add_port_to_vlan(self, port_id: int, vlan_id: int, tagged: bool = False) -> SwitchStatus:
        """Add a port to a VLAN"""
        if not self._initialized:
            self.initialize()
        
        result = self._switch_lib.add_port_to_vlan(port_id, vlan_id, tagged)
        status = SwitchStatus.OK if result == 0 else SwitchStatus.ERROR
        
        if status == SwitchStatus.OK:
            logger.info(f"Added port {port_id} to VLAN {vlan_id} ({'tagged' if tagged else 'untagged'})")
        else:
            logger.error(f"Failed to add port {port_id} to VLAN {vlan_id}: error code {result}")
        
        return status
    
    def remove_port_from_vlan(self, port_id: int, vlan_id: int) -> SwitchStatus:
        """Remove a port from a VLAN"""
        if not self._initialized:
            self.initialize()
        
        result = self._switch_lib.remove_port_from_vlan(port_id, vlan_id)
        status = SwitchStatus.OK if result == 0 else SwitchStatus.ERROR
        
        if status == SwitchStatus.OK:
            logger.info(f"Removed port {port_id} from VLAN {vlan_id}")
        else:
            logger.error(f"Failed to remove port {port_id} from VLAN {vlan_id}: error code {result}")
        
        return status
    
    # L3 routing methods
    def add_static_route(self, network: str, netmask: str, next_hop: str, 
                         interface: Optional[int] = None) -> SwitchStatus:
        """Add a static route"""
        if not self._initialized:
            self.initialize()
        
        # Implementation would call into C library
        # This is a placeholder
        logger.info(f"Added static route to {network}/{netmask} via {next_hop}" + 
                    (f" on interface {interface}" if interface else ""))
        return SwitchStatus.OK
    
    def delete_static_route(self, network: str, netmask: str) -> SwitchStatus:
        """Delete a static route"""
        if not self._initialized:
            self.initialize()
        
        # Implementation would call into C library
        # This is a placeholder
        logger.info(f"Deleted static route to {network}/{netmask}")
        return SwitchStatus.OK
    
    def get_routing_table(self) -> List[Dict[str, Any]]:
        """Get the current routing table"""
        if not self._initialized:
            self.initialize()
        
        # This would typically call into the C library to get the routing table
        # For now, we'll return a placeholder list
        return [
            {
                "network": "0.0.0.0",
                "netmask": "0.0.0.0",
                "next_hop": "192.168.1.1",
                "interface": 1,
                "type": "static",
                "metric": 0
            }
        ]
