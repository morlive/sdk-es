/**
 * @file cli.h
 * @brief Command Line Interface (CLI) for switch management
 *
 * This header defines the interface for the switch simulator's command-line
 * interface, allowing configuration and monitoring of the switch.
 */

#ifndef CLI_H
#define CLI_H

#include <stddef.h>
#include <stdbool.h>
#include "../common/error_codes.h"
#include "../common/types.h"

/**
 * @brief CLI command callback function type
 * 
 * Function signature for CLI command handlers
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @param output Buffer to store command output
 * @param output_len Maximum length of the output buffer
 * @return error_code_t Error code
 */
typedef error_code_t (*cli_cmd_handler_t)(int argc, char **argv, char *output, size_t output_len);

/**
 * @brief CLI command structure
 * 
 * Defines a CLI command with its name, handler function and help information
 */
typedef struct {
    const char *name;           /**< Command name */
    const char *help;           /**< Help text for the command */
    const char *usage;          /**< Usage example */
    cli_cmd_handler_t handler;  /**< Command handler function */
} cli_command_t;

/**
 * @brief CLI module context
 */
typedef struct cli_context_s {
    void *private_data;         /**< Private data for CLI implementation */
} cli_context_t;

/**
 * @brief Initialize the CLI subsystem
 * 
 * @param ctx Pointer to CLI context to initialize
 * @return error_code_t Error code
 */
error_code_t cli_init(cli_context_t *ctx);

/**
 * @brief Register a command with the CLI
 * 
 * @param ctx CLI context
 * @param cmd Command structure to register
 * @return error_code_t Error code
 */
error_code_t cli_register_command(cli_context_t *ctx, const cli_command_t *cmd);

/**
 * @brief Register a set of commands with the CLI
 * 
 * @param ctx CLI context
 * @param cmds Array of command structures to register
 * @param count Number of commands in the array
 * @return error_code_t Error code
 */
error_code_t cli_register_commands(cli_context_t *ctx, const cli_command_t *cmds, size_t count);

/**
 * @brief Execute a CLI command string
 * 
 * @param ctx CLI context
 * @param command_str Command string to execute
 * @param output Buffer to store command output
 * @param output_len Maximum length of the output buffer
 * @return error_code_t Error code
 */
error_code_t cli_execute(cli_context_t *ctx, const char *command_str, 
                         char *output, size_t output_len);

/**
 * @brief Start the CLI interactive mode
 * 
 * Starts an interactive CLI session that accepts user input.
 * This function blocks until the CLI session is terminated.
 * 
 * @param ctx CLI context
 * @return error_code_t Error code
 */
error_code_t cli_interactive_mode(cli_context_t *ctx);

/**
 * @brief Enable command history
 * 
 * @param ctx CLI context
 * @param history_size Number of commands to store in history
 * @return error_code_t Error code
 */
error_code_t cli_enable_history(cli_context_t *ctx, size_t history_size);

/**
 * @brief Enable command auto-completion
 * 
 * @param ctx CLI context
 * @param enable True to enable, false to disable
 * @return error_code_t Error code
 */
error_code_t cli_enable_auto_complete(cli_context_t *ctx, bool enable);

/**
 * @brief Set the CLI prompt string
 * 
 * @param ctx CLI context
 * @param prompt Prompt string to display
 * @return error_code_t Error code
 */
error_code_t cli_set_prompt(cli_context_t *ctx, const char *prompt);

/**
 * @brief Clean up CLI resources
 * 
 * @param ctx CLI context to clean up
 * @return error_code_t Error code
 */
error_code_t cli_cleanup(cli_context_t *ctx);

#endif /* CLI_H */
