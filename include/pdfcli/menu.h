#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum menu_choice : int8_t {
    MENU_QUIT      = -1,
    MENU_BROWSE    = 0,
    MENU_ENTER_DIR = 1
} menu_choice;

typedef struct menu_result {
    menu_choice choice;
    char dir_path[1024];
} menu_result;

// Runs the interactive main menu TUI.
// Options: "Browse Files", "Enter Directory", "Quit".
// If "Enter Directory" is selected, prompts for a directory path with Tab completion.
[[nodiscard]] menu_result menu_run(void);
