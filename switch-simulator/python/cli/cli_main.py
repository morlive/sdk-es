"""
CLI Main Application for Switch Simulator

This module provides the main CLI application that users interact with.
It sets up the command parser, handles input/output, and manages the
command loop.
"""

import os
import sys
import signal
import logging
import argparse
import readline
from typing import List, Optional

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# Add parent directory to sys.path
sys.path.append(os.path.join(os.path.dirname(__file__), '../..'))

# Import CLI modules
from .cli_parser import CommandParser, CommandContext, CommandResult

# Import API modules
try:
    from api.switch_controller import SwitchController, SwitchConfig
    from api.stats_viewer import StatsViewer
except ImportError as e:
    logger.error(f"Failed to import API modules: {e}")
    logger.error("Make sure the api/ directory is properly implemented")
    api_available = False
else:
    api_available = True


class CLI:
    """Main CLI application class"""
    
    def __init__(self, controller: Optional['SwitchController'] = None, 
                 history_file: str = None):
        """
        Initialize CLI application
        
        Args:
            controller: Optional switch controller instance
            history_file: Path to CLI history file
        """
        self.controller = controller
        self.context = CommandContext(controller)
        self.parser = CommandParser(self.context)
        self.history_file = history_file or os.path.expanduser("~/.switch_cli_history")
        self.running = False
        
        # Set up command history
        self._setup_history()
        
        # Set up signal handlers
        signal.signal(signal.SIGINT, self._handle_interrupt)
    
    def _setup_history(self):
        """Set up command history handling"""
        # Ensure history file exists
        os.makedirs(os.path.dirname(self.history_file), exist_ok=True)
        
        try:
            if not os.path.exists(self.history_file):
                with open(self.history_file, 'w'):
                    pass
            
            readline.read_history_file(self.history_file)
            readline.set_history_length(1000)
        except (IOError, OSError) as e:
            logger.warning(f"Could not read history file: {e}")
    
    def _save_history(self):
        """Save command history to file"""
        try:
            readline.write_history_file(self.history_file)
        except (IOError, OSError) as e:
            logger.warning(f"Could not write history file: {e}")
    
    def _handle_interrupt(self, signum, frame):
        """Handle keyboard interrupt"""
        print("\nUse 'exit' or 'quit' to exit the CLI")
    
    def _get_input(self) -> str:
        """Get input from the user with proper prompt"""
        try:
            return input(self.context.get_prompt())
        except EOFError:
            print()  # Add newline after EOF
            return "exit"
    
    def _display_result(self, result: CommandResult):
        """Display command result to the user"""
        if result.message:
            print(result.message)
    
    def _setup_autocompletion(self):
        """Set up tab autocompletion"""
        
        def completer(text, state):
            """Tab completion function for readline"""
            # Get completion options from the parser
            options = self.parser.autocomplete(text)
            if state < len(options):
                return options[state]
            return None
        
        readline.set_completer(completer)
        readline.parse_and_bind("tab: complete")
    
    def run(self):
        """Run the main CLI loop"""
        if not api_available:
            print("Error: Could not initialize CLI because API modules are missing")
            return 1
        
        print("Switch Simulator CLI")
        print("Type 'help' for a list of commands, or 'exit' to quit")
        
        self.running = True
        self._setup_autocompletion()
        
        while self.running:
            try:
                command = self._get_input()
                
                # Skip empty commands
                if not command.strip():
                    continue
                
                # Handle help command separately
                if command.strip().lower().startswith("help"):
                    args = command.strip().split(maxsplit=1)
                    if len(args) > 1:
                        result = self.parser.get_command_help(args[1])
                    else:
                        result = self.parser.get_command_help()
                else:
                    # Parse and execute the command
                    result = self.parser.parse_and_execute(command)
                
                # Display the result
                self._display_result(result)
                
                # Check for exit command
                if not result.success and result.message == "exit":
                    break
                
            except Exception as e:
                logger.error(f"Unhandled exception: {e}")
                print(f"Error: {e}")
        
        print("Goodbye!")
        self._save_history()
        return 0


def parse_arguments():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(description="Switch Simulator CLI")
    
    parser.add_argument("--config", "-c", type=str,
                        help="Path to switch configuration file")
    parser.add_argument("--debug", "-d", action="store_true",
                        help="Enable debug logging")
    parser.add_argument("--batch", "-b", type=str,
                        help="Path to batch command file to execute")
    
    return parser.parse_args()


def main():
    """Main entry point for the CLI application"""
    args = parse_arguments()
    
    # Set up logging level
    if args.debug:
        logging.getLogger().setLevel(logging.DEBUG)
    
    # Initialize switch controller
    if api_available:
        try:
            # Load configuration if specified
            config = SwitchConfig()
            if args.config:
                # TODO: Implement configuration loading
                logger.info(f"Loading configuration from {args.config}")
            
            controller = SwitchController(config)
            controller.initialize()
            logger.info("Initialized switch controller")
        except Exception as e:
            logger.error(f"Failed to initialize switch controller: {e}")
            print(f"Error initializing switch controller: {e}")
            return 1
    else:
        controller = None
    
    # Create and run CLI
    cli = CLI(controller)
    
    if args.batch:
        # Execute batch commands from file
        try:
            with open(args.batch, 'r') as batch_file:
                for line in batch_file:
                    line = line.strip()
                    if line and not line.startswith('#'):
                        print(f"{cli.context.get_prompt()}{line}")
                        result = cli.parser.parse_and_execute(line)
                        cli._display_result(result)
                        
                        # Check for exit command
                        if not result.success and result.message == "exit":
                            break
            return 0
        except (IOError, OSError) as e:
            logger.error(f"Failed to read batch file: {e}")
            print(f"Error reading batch file: {e}")
            return 1
    else:
        # Run interactive CLI
        return cli.run()


if __name__ == "__main__":
    sys.exit(main())
