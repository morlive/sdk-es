/**
 * @file cli_engine.c
 * @brief Implementation of the command-line interface engine
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "../../include/management/cli.h"
#include "../../include/common/logging.h"
#include "../../include/common/error_codes.h"

#define MAX_COMMANDS 256
#define MAX_COMMAND_LINE_LENGTH 1024
#define DEFAULT_PROMPT "switch> "

/**
 * @brief Private CLI context structure
 */
typedef struct {
    cli_command_t commands[MAX_COMMANDS];
    size_t command_count;
    char prompt[64];
    bool history_enabled;
    bool auto_complete_enabled;
    size_t history_size;
} cli_private_t;

// Forward declarations for helper functions
static char** cli_command_completion(const char* text, int start, int end);
static char* cli_command_generator(const char* text, int state);
static cli_private_t* get_private_ctx(cli_context_t *ctx);
static error_code_t cli_tokenize_command(const char *command_str, int *argc, char ***argv);
static void cli_free_argv(int argc, char **argv);

/**
 * @brief Initialize the CLI subsystem
 */
error_code_t cli_init(cli_context_t *ctx) {
    if (!ctx) {
        return ERROR_INVALID_PARAMETER;
    }
    
    // Allocate private data
    cli_private_t *priv = (cli_private_t *)calloc(1, sizeof(cli_private_t));
    if (!priv) {
        return ERROR_OUT_OF_MEMORY;
    }
    
    // Initialize default settings
    priv->command_count = 0;
    strncpy(priv->prompt, DEFAULT_PROMPT, sizeof(priv->prompt) - 1);
    priv->history_enabled = false;
    priv->auto_complete_enabled = false;
    priv->history_size = 100; // Default history size
    
    // Store private data in context
    ctx->private_data = priv;
    
    LOG_INFO("CLI subsystem initialized");
    return ERROR_NONE;
}

/**
 * @brief Register a command with the CLI
 */
error_code_t cli_register_command(cli_context_t *ctx, const cli_command_t *cmd) {
    cli_private_t *priv = get_private_ctx(ctx);
    if (!priv || !cmd || !cmd->name || !cmd->handler) {
        return ERROR_INVALID_PARAMETER;
    }
    
    // Check if we have space for another command
    if (priv->command_count >= MAX_COMMANDS) {
        LOG_ERROR("Cannot register command: maximum number of commands reached");
        return ERROR_RESOURCE_EXHAUSTED;
    }
    
    // Check if command name already exists
    for (size_t i = 0; i < priv->command_count; i++) {
        if (strcmp(priv->commands[i].name, cmd->name) == 0) {
            LOG_ERROR("Cannot register command: command '%s' already exists", cmd->name);
            return ERROR_ALREADY_EXISTS;
        }
    }
    
    // Copy command to internal array
    memcpy(&priv->commands[priv->command_count], cmd, sizeof(cli_command_t));
    priv->command_count++;
    
    LOG_INFO("Registered command: %s", cmd->name);
    return ERROR_NONE;
}

/**
 * @brief Register a set of commands with the CLI
 */
error_code_t cli_register_commands(cli_context_t *ctx, const cli_command_t *cmds, size_t count) {
    error_code_t result = ERROR_NONE;
    
    for (size_t i = 0; i < count; i++) {
        result = cli_register_command(ctx, &cmds[i]);
        if (result != ERROR_NONE) {
            LOG_ERROR("Failed to register command %zu: %s", i, cmds[i].name);
            return result;
        }
    }
    
    return ERROR_NONE;
}

/**
 * @brief Execute a CLI command string
 */
error_code_t cli_execute(cli_context_t *ctx, const char *command_str, 
                         char *output, size_t output_len) {
    cli_private_t *priv = get_private_ctx(ctx);
    if (!priv || !command_str || !output || output_len == 0) {
        return ERROR_INVALID_PARAMETER;
    }
    
    // Clear the output buffer
    output[0] = '\0';
    
    // Skip leading whitespace
    while (isspace(*command_str)) {
        command_str++;
    }
    
    // Check for empty command
    if (*command_str == '\0') {
        return ERROR_NONE;
    }
    
    // Tokenize the command string
    int argc = 0;
    char **argv = NULL;
    error_code_t result = cli_tokenize_command(command_str, &argc, &argv);
    if (result != ERROR_NONE || argc == 0) {
        if (argv) {
            cli_free_argv(argc, argv);
        }
        return result;
    }
    
    // Find the command handler
    const char *cmd_name = argv[0];
    cli_command_t *cmd = NULL;
    
    for (size_t i = 0; i < priv->command_count; i++) {
        if (strcmp(priv->commands[i].name, cmd_name) == 0) {
            cmd = &priv->commands[i];
            break;
        }
    }
    
    // Execute the command if found
    if (cmd) {
        result = cmd->handler(argc, argv, output, output_len);
    } else {
        snprintf(output, output_len, "Unknown command: %s", cmd_name);
        result = ERROR_NOT_FOUND;
    }
    
    // Clean up
    cli_free_argv(argc, argv);
    
    return result;
}

/**
 * @brief Start the CLI interactive mode
 */
error_code_t cli_interactive_mode(cli_context_t *ctx) {
    cli_private_t *priv = get_private_ctx(ctx);
    if (!priv) {
        return ERROR_INVALID_PARAMETER;
    }
    
    // Set up readline if history or auto-complete is enabled
    if (priv->history_enabled) {
        using_history();
        stifle_history(priv->history_size);
    }
    
    if (priv->auto_complete_enabled) {
        rl_attempted_completion_function = cli_command_completion;
    }
    
    char output[4096];
    char *line;
    
    LOG_INFO("Entering CLI interactive mode");
    
    // Main CLI loop
    while (1) {
        // Read a line of input
        line = readline(priv->prompt);
        
        // Check for EOF
        if (!line) {
            printf("\n");
            break;
        }
        
        // Skip empty lines
        if (line[0] != '\0') {
            // Add to history if enabled
            if (priv->history_enabled) {
                add_history(line);
            }
            
            // Check for exit command
            if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
                free(line);
                break;
            }
            
            // Execute the command
            error_code_t result = cli_execute(ctx, line, output, sizeof(output));
            
            // Display the output
            if (output[0] != '\0') {
                printf("%s\n", output);
            }
            
            // Display error if command failed
            if (result != ERROR_NONE && output[0] == '\0') {
                printf("Error: %d\n", result);
            }
        }
        
        // Free the line buffer
        free(line);
    }
    
    LOG_INFO("Exiting CLI interactive mode");
    return ERROR_NONE;
}

/**
 * @brief Enable command history
 */
error_code_t cli_enable_history(cli_context_t *ctx, size_t history_size) {
    cli_private_t *priv = get_private_ctx(ctx);
    if (!priv || history_size == 0) {
        return ERROR_INVALID_PARAMETER;
    }
    
    priv->history_enabled = true;
    priv->history_size = history_size;
    
    LOG_INFO("CLI history enabled with size %zu", history_size);
    return ERROR_NONE;
}

/**
 * @brief Enable command auto-completion
 */
error_code_t cli_enable_auto_complete(cli_context_t *ctx, bool enable) {
    cli_private_t *priv = get_private_ctx(ctx);
    if (!priv) {
        return ERROR_INVALID_PARAMETER;
    }
    
    priv->auto_complete_enabled = enable;
    
    LOG_INFO("CLI auto-complete %s", enable ? "enabled" : "disabled");
    return ERROR_NONE;
}

/**
 * @brief Set the CLI prompt string
 */
error_code_t cli_set_prompt(cli_context_t *ctx, const char *prompt) {
    cli_private_t *priv = get_private_ctx(ctx);
    if (!priv || !prompt) {
        return ERROR_INVALID_PARAMETER;
    }
    
    strncpy(priv->prompt, prompt, sizeof(priv->prompt) - 1);
    priv->prompt[sizeof(priv->prompt) - 1] = '\0';
    
    LOG_INFO("CLI prompt set to '%s'", priv->prompt);
    return ERROR_NONE;
}

/**
 * @brief Clean up CLI resources
 */
error_code_t cli_cleanup(cli_context_t *ctx) {
    cli_private_t *priv = get_private_ctx(ctx);
    if (!priv) {
        return ERROR_INVALID_PARAMETER;
    }
    
    // Clean up readline history if it was used
    if (priv->history_enabled) {
        clear_history();
    }
    
    // Free the private data
    free(priv);
    ctx->private_data = NULL;
    
    LOG_INFO("CLI subsystem cleaned up");
    return ERROR_NONE;
}

/**
 * @brief Get the private context data
 */
static cli_private_t* get_private_ctx(cli_context_t *ctx) {
    if (!ctx || !ctx->private_data) {
        return NULL;
    }
    return (cli_private_t*)ctx->private_data;
}

/**
 * @brief Tokenize a command string into arguments
 */
static error_code_t cli_tokenize_command(const char *command_str, int *argc, char ***argv) {
    if (!command_str || !argc || !argv) {
        return ERROR_INVALID_PARAMETER;
    }
    
    // Count the maximum number of arguments
    int max_args = 1;
    for (const char *p = command_str; *p; p++) {
        if (isspace(*p)) {
            max_args++;
        }
    }
    
    // Allocate argv array
    *argv = (char**)calloc(max_args, sizeof(char*));
    if (!*argv) {
        return ERROR_OUT_OF_MEMORY;
    }
    
    // Copy the command string
    char *cmd_copy = strdup(command_str);
    if (!cmd_copy) {
        free(*argv);
        *argv = NULL;
        return ERROR_OUT_OF_MEMORY;
    }
    
    // Tokenize the string
    *argc = 0;
    char *token = strtok(cmd_copy, " \t\n\r");
    while (token && *argc < max_args) {
        (*argv)[*argc] = strdup(token);
        if (!(*argv)[*argc]) {
            // Clean up on failure
            for (int i = 0; i < *argc; i++) {
                free((*argv)[i]);
            }
            free(*argv);
            *argv = NULL;
            free(cmd_copy);
            return ERROR_OUT_OF_MEMORY;
        }
        (*argc)++;
        token = strtok(NULL, " \t\n\r");
    }
    
    free(cmd_copy);
    return ERROR_NONE;
}

/**
 * @brief Free argv array
 */
static void cli_free_argv(int argc, char **argv) {
    if (!argv) {
        return;
    }
    
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);
}

/**
 * @brief Command completion function for readline
 */
static char** cli_command_completion(const char* text, int start, int end) {
    // If this is the start of the line, complete with command names
    if (start == 0) {
        return rl_completion_matches(text, cli_command_generator);
    }
    
    // Otherwise, let readline use default filename completion
    return NULL;
}

/**
 * @brief Command generator function for readline completion
 */
static char* cli_command_generator(const char* text, int state) {
    static size_t list_index;
    static size_t len;
    cli_private_t *priv;
    
    // Get the CLI context from readline's app-specific data
    cli_context_t *ctx = (cli_context_t*)rl_get_app_data();
    if (!ctx) {
        return NULL;
    }
    
    priv = get_private_ctx(ctx);
    if (!priv) {
        return NULL;
    }
    
    // If this is the first call, initialize list_index and text length
    if (!state) {
        list_index = 0;
        len = strlen(text);
    }
    
    // Return the next name which matches
    while (list_index < priv->command_count) {
        const char *name = priv->commands[list_index++].name;
        
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }
    
    // No more matches
    return NULL;
}
