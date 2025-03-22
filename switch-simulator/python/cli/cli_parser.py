"""
Command Parser for Switch Simulator CLI

This module handles parsing of CLI commands and dispatching them
to the appropriate handlers.
"""

import os
import sys
import re
import logging
import inspect
import importlib
from typing import Dict, List, Callable, Any, Optional, Tuple
from enum import Enum

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# Add parent directory to sys.path
sys.path.append(os.path.join(os.path.dirname(__file__), '../..'))


class CommandContext:
    """Context object passed to command handlers"""
    
    def __init__(self, switch_controller=None):
        """
        Initialize command context
        
        Args:
            switch_controller: The switch controller instance
        """
        self.switch_controller = switch_controller
        self.last_result = None
        self.mode = "global"  # Could be "global", "interface", "vlan", etc.
        self.current_interface = None
        self.current_vlan = None
        self.variables = {}  # For user-defined variables
    
    def set_mode(self, mode: str, context_obj=None):
        """Set the current CLI mode"""
        self.mode = mode
        
        if mode == "interface" and context_obj is not None:
            self.current_interface = context_obj
            self.current_vlan = None
        elif mode == "vlan" and context_obj is not None:
            self.current_vlan = context_obj
            self.current_interface = None
        elif mode == "global":
            self.current_interface = None
            self.current_vlan = None
    
    def get_prompt(self) -> str:
        """Get the appropriate prompt based on current mode"""
        hostname = "switch"
        if self.switch_controller:
            hostname = getattr(self.switch_controller.config, "hostname", "switch")
        
        if self.mode == "global":
            return f"{hostname}# "
        elif self.mode == "interface" and self.current_interface:
            return f"{hostname}(config-if-{self.current_interface.name})# "
        elif self.mode == "vlan" and self.current_vlan:
            return f"{hostname}(config-vlan-{self.current_vlan})# "
        else:
            return f"{hostname}({self.mode})# "


class CommandResult:
    """Result of a command execution"""
    
    def __init__(self, success: bool, message: str = "", data: Any = None):
        """
        Initialize command result
        
        Args:
            success: Whether the command succeeded
            message: Message to display to the user
            data: Optional data returned by the command
        """
        self.success = success
        self.message = message
        self.data = data
    
    def __str__(self) -> str:
        return self.message if self.message else ("Success" if self.success else "Failed")


class CommandHandler:
    """Decorator and registry for command handlers"""
    
    _commands = {}
    _help_text = {}
    _mode_restrictions = {}
    
    @classmethod
    def register(cls, command_pattern: str, help_text: str = "", modes: List[str] = None):
        """
        Decorator to register a command handler
        
        Args:
            command_pattern: Regex pattern to match the command
            help_text: Help text for the command
            modes: List of modes in which this command is valid
        """
        if modes is None:
            modes = ["global"]  # Default to global mode
        
        def decorator(func):
            cls._commands[command_pattern] = func
            cls._help_text[command_pattern] = help_text
            cls._mode_restrictions[command_pattern] = modes
            return func
        
        return decorator
    
    @classmethod
    def get_commands(cls) -> Dict[str, Callable]:
        """Get all registered commands"""
        return cls._commands
    
    @classmethod
    def get_help(cls, command_pattern: str = None) -> Dict[str, str]:
        """
        Get help text for commands
        
        Args:
            command_pattern: Optional pattern to get help for a specific command
            
        Returns:
            Dictionary of command patterns to help text
        """
        if command_pattern:
            if command_pattern in cls._help_text:
                return {command_pattern: cls._help_text[command_pattern]}
            return {}
        return cls._help_text
    
    @classmethod
    def is_valid_in_mode(cls, command_pattern: str, mode: str) -> bool:
        """
        Check if a command is valid in the current mode
        
        Args:
            command_pattern: The command pattern
            mode: The current mode
            
        Returns:
            True if the command is valid in the given mode
        """
        if command_pattern not in cls._mode_restrictions:
            return False
        
        valid_modes = cls._mode_restrictions[command_pattern]
        return mode in valid_modes or "all" in valid_modes


class CommandParser:
    """Parses command strings and executes appropriate handlers"""
    
    def __init__(self, context: CommandContext = None):
        """
        Initialize command parser
        
        Args:
            context: Optional command context
        """
        self.context = context or CommandContext()
        self._load_command_modules()
    
    def _load_command_modules(self):
        """Load all command modules from the commands directory"""
        commands_dir = os.path.join(os.path.dirname(__file__), "commands")
        
        # Ensure __init__.py exists
        init_path = os.path.join(commands_dir, "__init__.py")
        if not os.path.exists(init_path):
            logger.warning(f"Missing __init__.py in {commands_dir}")
            return
        
        # Import all python files in the commands directory
        for filename in os.listdir(commands_dir):
            if filename.endswith(".py") and filename != "__init__.py":
                module_name = filename[:-3]  # Remove .py extension
                try:
                    importlib.import_module(f".commands.{module_name}", package="cli")
                    logger.debug(f"Loaded command module: {module_name}")
                except ImportError as e:
                    logger.error(f"Failed to load command module {module_name}: {e}")
    
    def parse_and_execute(self, command_str: str) -> CommandResult:
        """
        Parse and execute a command string
        
        Args:
            command_str: The command string to parse
            
        Returns:
            CommandResult object with the result of execution
        """
        command_str = command_str.strip()
        
        if not command_str:
            return CommandResult(True, "")
        
        # Handle built-in exit command
        if command_str.lower() in ("exit", "quit"):
            if self.context.mode != "global":
                self.context.set_mode("global")
                return CommandResult(True, "Returned to global mode")
            else:
                # Signal to the main loop that we want to exit
                return CommandResult(False, "exit")
        
        # Handle mode-specific exit
        if command_str.lower() == "end":
            if self.context.mode != "global":
                self.context.set_mode("global")
                return CommandResult(True, "Returned to global mode")
            return CommandResult(False, "Cannot use 'end' in global mode")
        
        # Find matching command
        for pattern, handler in CommandHandler.get_commands().items():
            match = re.match(pattern, command_str, re.IGNORECASE)
            if match:
                # Check if command is valid in current mode
                if not CommandHandler.is_valid_in_mode(pattern, self.context.mode):
                    return CommandResult(False, f"Command not valid in {self.context.mode} mode")
                
                try:
                    # Call the handler with the match object and context
                    result = handler(match, self.context)
                    if isinstance(result, CommandResult):
                        self.context.last_result = result
                        return result
                    else:
                        # If handler didn't return a CommandResult, create one
                        result_obj = CommandResult(True, str(result) if result else "")
                        self.context.last_result = result_obj
                        return result_obj
                except Exception as e:
                    logger.error(f"Error executing command {command_str!r}: {e}")
                    return CommandResult(False, f"Error: {e}")
                
        return CommandResult(False, f"Unknown command: {command_str}")
    
    def get_command_help(self, command_str: str = None) -> CommandResult:
        """
        Get help for commands
        
        Args:
            command_str: Optional specific command to get help for
            
        Returns:
            CommandResult with help text
        """
        if command_str:
            # Try to match against available commands
            help_matches = {}
            for pattern in CommandHandler.get_help():
                if re.search(command_str, pattern, re.IGNORECASE):
                    # Check if command is valid in current mode
                    if CommandHandler.is_valid_in_mode(pattern, self.context.mode):
                        help_matches[pattern] = CommandHandler.get_help()[pattern]
            
            if help_matches:
                help_text = "Available commands matching '{}' in {} mode:\n".format(
                    command_str, self.context.mode
                )
                for pattern, text in help_matches.items():
                    help_text += f"  {pattern} - {text}\n"
                return CommandResult(True, help_text)
            else:
                return CommandResult(False, f"No help available for '{command_str}' in {self.context.mode} mode")
        else:
            # Show all commands valid in the current mode
            help_text = "Available commands in {} mode:\n".format(self.context.mode)
            for pattern, text in CommandHandler.get_help().items():
                if CommandHandler.is_valid_in_mode(pattern, self.context.mode):
                    help_text += f"  {pattern} - {text}\n"
            return CommandResult(True, help_text)
    
    def autocomplete(self, partial_command: str) -> List[str]:
        """
        Provide autocompletion suggestions for a partial command
        
        Args:
            partial_command: The partial command to complete
            
        Returns:
            List of possible completions
        """
        suggestions = []
        
        for pattern in CommandHandler.get_commands():
            # Convert regex pattern to a prefix if possible
            if pattern.startswith('^'):
                # Remove regex components to get a base command
                base_cmd = re.sub(r'\([^)]*\)', '', pattern[1:])
                base_cmd = re.sub(r'[\$\^\*\+\?\[\]\{\}]', '', base_cmd)
                
                if base_cmd.lower().startswith(partial_command.lower()):
                    # Only suggest commands valid in current mode
                    if CommandHandler.is_valid_in_mode(pattern, self.context.mode):
                        suggestions.append(base_cmd)
        
        return suggestions
