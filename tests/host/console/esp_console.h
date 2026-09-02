// Host esp_console shim for the console command suite.
//
// This is a faithful double of the small part of the ESP-IDF console the
// firmware uses: a registration table, the built in help command, and a
// tokenizing dispatcher. The real command handlers in src/FurbleConsole.cpp
// run unmodified against it, so the suite exercises production parsing and
// dispatch rather than a reimplementation of it.
#ifndef FURBLE_HOST_CONSOLE_ESP_CONSOLE_H
#define FURBLE_HOST_CONSOLE_ESP_CONSOLE_H

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

typedef int (*esp_console_cmd_func_t)(int argc, char **argv);
typedef int (*esp_console_cmd_func_with_context_t)(void *context, int argc, char **argv);

// Field order mirrors the ESP-IDF struct, because FurbleConsole builds every
// entry with designated initializers in exactly this order.
typedef struct {
  const char *command;
  const char *help;
  const char *hint;
  esp_console_cmd_func_t func;
  void *argtable;
  esp_console_cmd_func_with_context_t func_w_context;
  void *context;
} esp_console_cmd_t;

typedef struct {
  size_t max_cmdline_length;
  size_t max_cmdline_args;
  int hint_color;
  int hint_bold;
} esp_console_config_t;

#define ESP_CONSOLE_CONFIG_DEFAULT() \
  {.max_cmdline_length = 256, .max_cmdline_args = 32, .hint_color = 39, .hint_bold = 0}

esp_err_t esp_console_init(const esp_console_config_t *config);
esp_err_t esp_console_deinit(void);
esp_err_t esp_console_register_help_command(void);
esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd);

// Returns ESP_ERR_NOT_FOUND for an unknown command and ESP_ERR_INVALID_ARG for
// an empty line, matching ESP-IDF. The handler return value lands in cmd_ret.
esp_err_t esp_console_run(const char *cmdline, int *cmd_ret);

#endif
