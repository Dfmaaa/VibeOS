/*
 * VibeOS Tetris
 *
 * Classic falling blocks game.
 * Controls: A/D or arrows to move, W to rotate, S to drop fast, Q to quit
 */

#include "tetris.h"
#include "console.h"
#include "keyboard.h"
#include "fb.h"
#include "string.h"
#include "printf.h"
#include <stdint.h>

// Board dimensions
#define BOARD_WIDTH  10
#define BOARD_HEIGHT 20
#define BOARD_X      25  // X offset on screen
#define BOARD_Y      2   // Y offset on screen

// Piece types
#define NUM_PIECES 7

// The 7 tetrominos (I, O, T, S, Z, J, L)
// Each piece has 4 rotations, each rotation is 4x4
static const uint8_t pieces[NUM_PIECES][4][4][4] = {
    // I piece
    {
        {{0,0,0,0}, {1,1,1,1}, {0,0,0,0}, {0,0,0,0}},
        {{0,0,1,0}, {0,0,1,0}, {0,0,1,0}, {0,0,1,0}},
        {{0,0,0,0}, {0,0,0,0}, {1,1,1,1}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,1,0,0}},
    },
    // O piece
    {
        {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
    },
    // T piece
    {
        {{0,1,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,1,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0}},
    },
    // S piece
    {
        {{0,1,1,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,1,0}, {0,0,1,0}, {0,0,0,0}},
        {{0,0,0,0}, {0,1,1,0}, {1,1,0,0}, {0,0,0,0}},
        {{1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0}},
    },
    // Z piece
    {
        {{1,1,0,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,0,1,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,0,0}, {0,1,1,0}, {0,0,0,0}},
        {{0,1,0,0}, {1,1,0,0}, {1,0,0,0}, {0,0,0,0}},
    },
    // J piece
    {
        {{1,0,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,1,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,1,0}, {0,0,1,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,0,0}, {1,1,0,0}, {0,0,0,0}},
    },
    // L piece
    {
        {{0,0,1,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,0,0}, {0,1,1,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,1,0}, {1,0,0,0}, {0,0,0,0}},
        {{1,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0}},
    },
};

// Piece colors
static const uint32_t piece_colors[NUM_PIECES] = {
    0x00FFFF,  // I - cyan
    0xFFFF00,  // O - yellow
    0xFF00FF,  // T - magenta
    0x00FF00,  // S - green
    0xFF0000,  // Z - red
    0x0000FF,  // J - blue
    0xFFA500,  // L - orange
};

// Game state
static uint8_t board[BOARD_HEIGHT][BOARD_WIDTH];
static uint32_t board_colors[BOARD_HEIGHT][BOARD_WIDTH];
static int current_piece;
static int current_rotation;
static int current_x;
static int current_y;
static int next_piece;
static int score;
static int lines;
static int level;
static int game_over;
static int high_score = 0;

// Random number generator
static uint32_t rand_state = 54321;

static uint32_t rand(void) {
    rand_state = rand_state * 1103515245 + 12345;
    return (rand_state >> 16) & 0x7FFF;
}

// Delay function
static void delay(int cycles) {
    for (volatile int i = 0; i < cycles; i++) {
        __asm__ volatile("nop");
    }
}

// Draw the board border
static void draw_border(void) {
    console_set_color(COLOR_WHITE, COLOR_BLACK);

    // Top border
    console_set_cursor(BOARD_Y - 1, BOARD_X - 1);
    console_putc('+');
    for (int x = 0; x < BOARD_WIDTH * 2; x++) {
        console_putc('-');
    }
    console_putc('+');

    // Side borders
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        console_set_cursor(BOARD_Y + y, BOARD_X - 1);
        console_putc('|');
        console_set_cursor(BOARD_Y + y, BOARD_X + BOARD_WIDTH * 2);
        console_putc('|');
    }

    // Bottom border
    console_set_cursor(BOARD_Y + BOARD_HEIGHT, BOARD_X - 1);
    console_putc('+');
    for (int x = 0; x < BOARD_WIDTH * 2; x++) {
        console_putc('-');
    }
    console_putc('+');
}

// Draw the info panel
static void draw_info(void) {
    int info_x = BOARD_X + BOARD_WIDTH * 2 + 4;

    console_set_cursor(BOARD_Y, info_x);
    console_set_color(COLOR_AMBER, COLOR_BLACK);
    console_puts("TETRIS");

    console_set_cursor(BOARD_Y + 2, info_x);
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    printf("Score: %d    ", score);

    console_set_cursor(BOARD_Y + 3, info_x);
    printf("Lines: %d    ", lines);

    console_set_cursor(BOARD_Y + 4, info_x);
    printf("Level: %d    ", level);

    console_set_cursor(BOARD_Y + 5, info_x);
    printf("High:  %d    ", high_score);

    console_set_cursor(BOARD_Y + 7, info_x);
    console_puts("Next:");

    // Draw next piece
    for (int y = 0; y < 4; y++) {
        console_set_cursor(BOARD_Y + 8 + y, info_x);
        for (int x = 0; x < 4; x++) {
            if (pieces[next_piece][0][y][x]) {
                console_set_color(piece_colors[next_piece], COLOR_BLACK);
                console_puts("[]");
            } else {
                console_puts("  ");
            }
        }
    }

    console_set_cursor(BOARD_Y + 14, info_x);
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    console_puts("Controls:");

    console_set_cursor(BOARD_Y + 15, info_x);
    console_puts("A/D  Move");

    console_set_cursor(BOARD_Y + 16, info_x);
    console_puts("W    Rotate");

    console_set_cursor(BOARD_Y + 17, info_x);
    console_puts("S    Drop");

    console_set_cursor(BOARD_Y + 18, info_x);
    console_puts("Q    Quit");
}

// Draw a cell on the board
static void draw_cell(int x, int y, uint32_t color) {
    console_set_cursor(BOARD_Y + y, BOARD_X + x * 2);
    if (color != COLOR_BLACK) {
        console_set_color(color, COLOR_BLACK);
        console_puts("[]");
    } else {
        console_set_color(COLOR_BLACK, COLOR_BLACK);
        console_puts("  ");
    }
}

// Draw the entire board
static void draw_board(void) {
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            draw_cell(x, y, board_colors[y][x]);
        }
    }
}

// Draw the current piece
static void draw_piece(int clear) {
    uint32_t color = clear ? COLOR_BLACK : piece_colors[current_piece];

    for (int py = 0; py < 4; py++) {
        for (int px = 0; px < 4; px++) {
            if (pieces[current_piece][current_rotation][py][px]) {
                int bx = current_x + px;
                int by = current_y + py;
                if (bx >= 0 && bx < BOARD_WIDTH && by >= 0 && by < BOARD_HEIGHT) {
                    draw_cell(bx, by, color);
                }
            }
        }
    }
}

// Check if piece can be placed at position
static int can_place(int piece, int rotation, int x, int y) {
    for (int py = 0; py < 4; py++) {
        for (int px = 0; px < 4; px++) {
            if (pieces[piece][rotation][py][px]) {
                int bx = x + px;
                int by = y + py;

                // Check bounds
                if (bx < 0 || bx >= BOARD_WIDTH || by >= BOARD_HEIGHT) {
                    return 0;
                }

                // Check collision with placed pieces (only if on board)
                if (by >= 0 && board[by][bx]) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

// Lock the current piece in place
static void lock_piece(void) {
    for (int py = 0; py < 4; py++) {
        for (int px = 0; px < 4; px++) {
            if (pieces[current_piece][current_rotation][py][px]) {
                int bx = current_x + px;
                int by = current_y + py;
                if (bx >= 0 && bx < BOARD_WIDTH && by >= 0 && by < BOARD_HEIGHT) {
                    board[by][bx] = 1;
                    board_colors[by][bx] = piece_colors[current_piece];
                }
            }
        }
    }
}

// Clear completed lines
static int clear_lines(void) {
    int cleared = 0;

    for (int y = BOARD_HEIGHT - 1; y >= 0; y--) {
        // Check if line is complete
        int complete = 1;
        for (int x = 0; x < BOARD_WIDTH; x++) {
            if (!board[y][x]) {
                complete = 0;
                break;
            }
        }

        if (complete) {
            cleared++;

            // Move all lines above down
            for (int yy = y; yy > 0; yy--) {
                for (int x = 0; x < BOARD_WIDTH; x++) {
                    board[yy][x] = board[yy - 1][x];
                    board_colors[yy][x] = board_colors[yy - 1][x];
                }
            }

            // Clear top line
            for (int x = 0; x < BOARD_WIDTH; x++) {
                board[0][x] = 0;
                board_colors[0][x] = COLOR_BLACK;
            }

            // Check this line again (it now has the line from above)
            y++;
        }
    }

    return cleared;
}

// Spawn a new piece
static void spawn_piece(void) {
    current_piece = next_piece;
    next_piece = rand() % NUM_PIECES;
    current_rotation = 0;
    current_x = BOARD_WIDTH / 2 - 2;
    current_y = -1;

    // Check if spawn position is valid
    if (!can_place(current_piece, current_rotation, current_x, current_y)) {
        game_over = 1;
    }
}

// Initialize game
static void init_game(void) {
    console_clear();

    // Clear board
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            board[y][x] = 0;
            board_colors[y][x] = COLOR_BLACK;
        }
    }

    score = 0;
    lines = 0;
    level = 1;
    game_over = 0;

    // Initialize pieces
    next_piece = rand() % NUM_PIECES;
    spawn_piece();

    // Draw initial state
    draw_border();
    draw_board();
    draw_info();
    draw_piece(0);
}

// Process input, returns -1 to quit
static int process_input(void) {
    while (keyboard_has_key()) {
        int c = keyboard_getc();

        switch (c) {
            case 'q':
            case 'Q':
                return -1;

            case 'a':
            case 'A':
                // Move left
                if (can_place(current_piece, current_rotation, current_x - 1, current_y)) {
                    draw_piece(1);
                    current_x--;
                    draw_piece(0);
                }
                break;

            case 'd':
            case 'D':
                // Move right
                if (can_place(current_piece, current_rotation, current_x + 1, current_y)) {
                    draw_piece(1);
                    current_x++;
                    draw_piece(0);
                }
                break;

            case 'w':
            case 'W':
                // Rotate
                {
                    int new_rot = (current_rotation + 1) % 4;
                    if (can_place(current_piece, new_rot, current_x, current_y)) {
                        draw_piece(1);
                        current_rotation = new_rot;
                        draw_piece(0);
                    }
                }
                break;

            case 's':
            case 'S':
                // Soft drop
                if (can_place(current_piece, current_rotation, current_x, current_y + 1)) {
                    draw_piece(1);
                    current_y++;
                    draw_piece(0);
                    score += 1;
                }
                break;

            case ' ':
                // Hard drop
                draw_piece(1);
                while (can_place(current_piece, current_rotation, current_x, current_y + 1)) {
                    current_y++;
                    score += 2;
                }
                draw_piece(0);
                break;
        }
    }
    return 0;
}

// Update game state
static void update_game(void) {
    // Try to move piece down
    if (can_place(current_piece, current_rotation, current_x, current_y + 1)) {
        draw_piece(1);
        current_y++;
        draw_piece(0);
    } else {
        // Lock piece
        lock_piece();

        // Clear lines
        int cleared = clear_lines();
        if (cleared > 0) {
            lines += cleared;

            // Scoring: 100, 300, 500, 800 for 1, 2, 3, 4 lines
            int points[] = {0, 100, 300, 500, 800};
            score += points[cleared] * level;

            // Level up every 10 lines
            level = (lines / 10) + 1;
            if (level > 10) level = 10;

            // Redraw board after clearing lines
            draw_board();
        }

        // Update high score
        if (score > high_score) {
            high_score = score;
        }

        // Update info
        draw_info();

        // Spawn new piece
        spawn_piece();
        if (!game_over) {
            draw_piece(0);
        }
    }
}

// Show game over screen
static void show_game_over(void) {
    int center_y = BOARD_Y + BOARD_HEIGHT / 2;
    int center_x = BOARD_X + BOARD_WIDTH - 4;

    console_set_cursor(center_y - 1, center_x - 2);
    console_set_color(COLOR_RED, COLOR_BLACK);
    console_puts(" GAME OVER! ");

    console_set_cursor(center_y + 1, center_x - 2);
    console_set_color(COLOR_WHITE, COLOR_BLACK);
    printf(" Score: %d ", score);

    console_set_cursor(center_y + 3, center_x - 4);
    console_set_color(COLOR_AMBER, COLOR_BLACK);
    console_puts("[R]estart [Q]uit");
    console_set_color(COLOR_WHITE, COLOR_BLACK);
}

// Wait for restart or quit
static int wait_for_restart(void) {
    while (1) {
        if (keyboard_has_key()) {
            int c = keyboard_getc();
            if (c == 'r' || c == 'R') {
                return 1;
            }
            if (c == 'q' || c == 'Q') {
                return 0;
            }
        }
        delay(10000);
    }
}

// Main game loop
int tetris_run(void) {
    init_game();

    int drop_counter = 0;
    int drop_speed = 20;  // Frames between drops

    while (1) {
        // Process input
        if (process_input() < 0) {
            break;
        }

        // Update drop counter
        drop_counter++;
        if (drop_counter >= drop_speed) {
            drop_counter = 0;
            update_game();

            // Adjust speed based on level
            drop_speed = 21 - level * 2;
            if (drop_speed < 2) drop_speed = 2;
        }

        // Check game over
        if (game_over) {
            show_game_over();
            if (wait_for_restart()) {
                init_game();
                drop_counter = 0;
                continue;
            } else {
                break;
            }
        }

        // Frame delay
        delay(1500000);
    }

    console_clear();
    console_set_color(COLOR_WHITE, COLOR_BLACK);

    return score;
}
