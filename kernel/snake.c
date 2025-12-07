/*
 * VibeOS Snake Game
 *
 * Classic snake game for the terminal.
 * Controls: WASD or arrow keys to move, Q to quit
 */

#include "snake.h"
#include "console.h"
#include "keyboard.h"
#include "fb.h"
#include "string.h"
#include "printf.h"
#include <stdint.h>

// Game board dimensions (leave room for border and score)
#define BOARD_WIDTH  40
#define BOARD_HEIGHT 20
#define BOARD_X      10  // X offset on screen
#define BOARD_Y      2   // Y offset on screen

// Maximum snake length
#define MAX_SNAKE_LEN 256

// Directions
typedef enum {
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
} direction_t;

// Point structure
typedef struct {
    int x;
    int y;
} point_t;

// Game state
static point_t snake[MAX_SNAKE_LEN];
static int snake_len;
static direction_t direction;
static direction_t next_direction;
static point_t food;
static int score;
static int game_over;
static int high_score = 0;

// Simple pseudo-random number generator
static uint32_t rand_state = 12345;

static uint32_t rand(void) {
    rand_state = rand_state * 1103515245 + 12345;
    return (rand_state >> 16) & 0x7FFF;
}

static void seed_rand(uint32_t seed) {
    rand_state = seed;
}

// Delay function (busy wait)
static void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++) {
        __asm__ volatile("nop");
    }
}

// Draw the game border
static void draw_border(void) {
    console_set_color(COLOR_WHITE, COLOR_BLACK);

    // Top border
    console_set_cursor(BOARD_Y - 1, BOARD_X - 1);
    console_putc('+');
    for (int x = 0; x < BOARD_WIDTH; x++) {
        console_putc('-');
    }
    console_putc('+');

    // Side borders
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        console_set_cursor(BOARD_Y + y, BOARD_X - 1);
        console_putc('|');
        console_set_cursor(BOARD_Y + y, BOARD_X + BOARD_WIDTH);
        console_putc('|');
    }

    // Bottom border
    console_set_cursor(BOARD_Y + BOARD_HEIGHT, BOARD_X - 1);
    console_putc('+');
    for (int x = 0; x < BOARD_WIDTH; x++) {
        console_putc('-');
    }
    console_putc('+');
}

// Draw the score
static void draw_score(void) {
    console_set_cursor(0, 0);
    console_set_color(COLOR_AMBER, COLOR_BLACK);
    printf("SNAKE");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    printf("  Score: %d  High: %d  [Q]uit", score, high_score);

    // Clear rest of line
    for (int i = 0; i < 20; i++) console_putc(' ');
}

// Place food at a random location (not on snake)
static void place_food(void) {
    int valid;
    do {
        valid = 1;
        food.x = rand() % BOARD_WIDTH;
        food.y = rand() % BOARD_HEIGHT;

        // Check if food is on snake
        for (int i = 0; i < snake_len; i++) {
            if (snake[i].x == food.x && snake[i].y == food.y) {
                valid = 0;
                break;
            }
        }
    } while (!valid);
}

// Draw a cell on the board
static void draw_cell(int x, int y, char c, uint32_t color) {
    console_set_cursor(BOARD_Y + y, BOARD_X + x);
    console_set_color(color, COLOR_BLACK);
    console_putc(c);
}

// Clear a cell
static void clear_cell(int x, int y) {
    console_set_cursor(BOARD_Y + y, BOARD_X + x);
    console_set_color(COLOR_BLACK, COLOR_BLACK);
    console_putc(' ');
}

// Draw the snake
static void draw_snake(void) {
    // Draw head
    draw_cell(snake[0].x, snake[0].y, '@', COLOR_GREEN);

    // Draw body
    for (int i = 1; i < snake_len; i++) {
        draw_cell(snake[i].x, snake[i].y, 'o', COLOR_GREEN);
    }
}

// Draw food
static void draw_food(void) {
    draw_cell(food.x, food.y, '*', COLOR_RED);
}

// Initialize the game
static void init_game(void) {
    // Clear screen
    console_clear();

    // Initialize snake in center
    snake_len = 3;
    snake[0].x = BOARD_WIDTH / 2;
    snake[0].y = BOARD_HEIGHT / 2;
    snake[1].x = snake[0].x - 1;
    snake[1].y = snake[0].y;
    snake[2].x = snake[0].x - 2;
    snake[2].y = snake[0].y;

    direction = DIR_RIGHT;
    next_direction = DIR_RIGHT;
    score = 0;
    game_over = 0;

    // Seed random with something varying
    seed_rand(rand_state + snake[0].x * 31337);

    // Place initial food
    place_food();

    // Draw initial state
    draw_border();
    draw_score();
    draw_snake();
    draw_food();
}

// Check for collision with walls or self
static int check_collision(int x, int y) {
    // Wall collision
    if (x < 0 || x >= BOARD_WIDTH || y < 0 || y >= BOARD_HEIGHT) {
        return 1;
    }

    // Self collision (check against body, not head)
    for (int i = 1; i < snake_len; i++) {
        if (snake[i].x == x && snake[i].y == y) {
            return 1;
        }
    }

    return 0;
}

// Update game state
static void update_game(void) {
    // Apply queued direction change
    direction = next_direction;

    // Calculate new head position
    point_t new_head = snake[0];
    switch (direction) {
        case DIR_UP:    new_head.y--; break;
        case DIR_DOWN:  new_head.y++; break;
        case DIR_LEFT:  new_head.x--; break;
        case DIR_RIGHT: new_head.x++; break;
    }

    // Check collision
    if (check_collision(new_head.x, new_head.y)) {
        game_over = 1;
        return;
    }

    // Check if eating food
    int ate_food = (new_head.x == food.x && new_head.y == food.y);

    // Clear tail (unless we ate food)
    if (!ate_food) {
        clear_cell(snake[snake_len - 1].x, snake[snake_len - 1].y);
    }

    // Move body (shift all segments)
    if (ate_food && snake_len < MAX_SNAKE_LEN) {
        snake_len++;
    }
    for (int i = snake_len - 1; i > 0; i--) {
        snake[i] = snake[i - 1];
    }
    snake[0] = new_head;

    // Redraw previous head as body
    if (snake_len > 1) {
        draw_cell(snake[1].x, snake[1].y, 'o', COLOR_GREEN);
    }

    // Draw new head
    draw_cell(snake[0].x, snake[0].y, '@', COLOR_GREEN);

    // Handle food
    if (ate_food) {
        score += 10;
        if (score > high_score) {
            high_score = score;
        }
        draw_score();
        place_food();
        draw_food();
    }
}

// Process input
static int process_input(void) {
    while (keyboard_has_key()) {
        int c = keyboard_getc();

        switch (c) {
            case 'q':
            case 'Q':
                return -1;  // Quit

            case 'w':
            case 'W':
                if (direction != DIR_DOWN) next_direction = DIR_UP;
                break;
            case 's':
            case 'S':
                if (direction != DIR_UP) next_direction = DIR_DOWN;
                break;
            case 'a':
            case 'A':
                if (direction != DIR_RIGHT) next_direction = DIR_LEFT;
                break;
            case 'd':
            case 'D':
                if (direction != DIR_LEFT) next_direction = DIR_RIGHT;
                break;

            // Arrow keys (if keyboard driver sends them as escape sequences)
            // For now just WASD
        }
    }
    return 0;
}

// Show game over screen
static void show_game_over(void) {
    int center_y = BOARD_Y + BOARD_HEIGHT / 2;
    int center_x = BOARD_X + BOARD_WIDTH / 2 - 5;

    console_set_cursor(center_y - 1, center_x - 2);
    console_set_color(COLOR_RED, COLOR_BLACK);
    console_puts("  GAME OVER!  ");

    console_set_cursor(center_y + 1, center_x - 2);
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    printf("  Score: %d  ", score);

    console_set_cursor(center_y + 3, center_x - 4);
    console_set_color(COLOR_AMBER, COLOR_BLACK);
    console_puts("[R]estart  [Q]uit");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
}

// Wait for restart or quit
static int wait_for_restart(void) {
    while (1) {
        if (keyboard_has_key()) {
            int c = keyboard_getc();
            if (c == 'r' || c == 'R') {
                return 1;  // Restart
            }
            if (c == 'q' || c == 'Q') {
                return 0;  // Quit
            }
        }
        delay(10000);
    }
}

// Main game loop
int snake_run(void) {
    init_game();

    while (1) {
        // Process input
        if (process_input() < 0) {
            break;  // Quit
        }

        // Update game
        update_game();

        // Check game over
        if (game_over) {
            show_game_over();
            if (wait_for_restart()) {
                init_game();
                continue;
            } else {
                break;
            }
        }

        // Delay for game speed (adjust for difficulty)
        delay(25000000);
    }

    // Clean up
    console_clear();
    console_set_color(COLOR_WHITE, COLOR_BLACK);

    return score;
}
