/*
 * VibeOS Music Player
 *
 * Classic Mac System 7 style - 1-bit black & white
 */

#include "../lib/vibe.h"
#include "../lib/gfx.h"

// Disable SIMD - freestanding environment has no arm_neon.h
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "../../vendor/minimp3.h"

static kapi_t *api;
static int window_id = -1;
static uint32_t *win_buffer;
static int win_w, win_h;
static gfx_ctx_t gfx;

// ============ Colors - Classic Mac ============
#define BLACK   0x00000000
#define WHITE   0x00FFFFFF
#define GRAY    0x00808080

// ============ Layout Constants ============
#define SIDEBAR_W       160
#define CONTROLS_H      85
#define ALBUM_ITEM_H    20
#define TRACK_ITEM_H    18

// ============ Player State ============
#define MAX_ALBUMS      32
#define MAX_TRACKS      64
#define MAX_NAME_LEN    64

typedef struct {
    char name[MAX_NAME_LEN];
    char path[128];
} album_t;

typedef struct {
    char name[MAX_NAME_LEN];
    char path[128];
} track_t;

static album_t albums[MAX_ALBUMS];
static int album_count = 0;
static int selected_album = -1;

static track_t tracks[MAX_TRACKS];
static int track_count = 0;
static int selected_track = -1;
static int playing_track = -1;

// Playback state
static int is_playing = 0;
static int volume = 80;  // 0-100

// Decoded audio buffer (kept for async playback)
static int16_t *pcm_buffer = NULL;
static uint32_t pcm_samples = 0;
static uint32_t pcm_sample_rate = 44100;
static uint32_t playback_start_tick = 0;
static uint32_t pause_elapsed_ms = 0;  // Elapsed time when paused

// Scroll positions
static int album_scroll = 0;
static int track_scroll = 0;

// Error message (shown briefly)
static char error_msg[64] = {0};
static uint32_t error_tick = 0;

// Loading state machine
typedef enum {
    LOAD_STATE_IDLE = 0,
    LOAD_STATE_LOADING_FILE,
    LOAD_STATE_COUNTING_SAMPLES,
    LOAD_STATE_DECODING,
    LOAD_STATE_STARTING_PLAYBACK
} load_state_t;

static load_state_t load_state = LOAD_STATE_IDLE;
static int is_loading = 0;

// Loading progress tracking
static int load_progress = 0;  // 0-100 percent
static int load_target_track = -1;

// File loading state
static uint8_t *load_mp3_data = NULL;
static int load_file_size = 0;
static int load_file_offset = 0;
static void *load_file_handle = NULL;

// Decode state
static mp3dec_t load_mp3d;
static const uint8_t *load_decode_ptr = NULL;
static int load_decode_remaining = 0;
static uint32_t load_total_samples = 0;
static int load_channels = 0;
static int16_t *load_out_ptr = NULL;
static uint32_t load_decoded_samples = 0;

// Chunk sizes for non-blocking operations
#define FILE_CHUNK_SIZE     (32 * 1024)   // 32KB per file read chunk
#define DECODE_FRAMES_PER_YIELD  20        // Decode 20 MP3 frames per yield

// ============ Drawing Helpers ============

#define fill_rect(x, y, w, h, c)     gfx_fill_rect(&gfx, x, y, w, h, c)
#define draw_char(x, y, ch, fg, bg)  gfx_draw_char(&gfx, x, y, ch, fg, bg)
#define draw_string(x, y, s, fg, bg) gfx_draw_string(&gfx, x, y, s, fg, bg)
#define draw_rect(x, y, w, h, c)     gfx_draw_rect(&gfx, x, y, w, h, c)
#define draw_hline(x, y, w, c)       gfx_draw_hline(&gfx, x, y, w, c)
#define draw_vline(x, y, h, c)       gfx_draw_vline(&gfx, x, y, h, c)

// Draw text clipped to width
static void draw_text_clip(int x, int y, const char *s, uint32_t fg, uint32_t bg, int max_w) {
    int drawn = 0;
    while (*s && drawn + 8 <= max_w) {
        draw_char(x, y, *s, fg, bg);
        x += 8;
        drawn += 8;
        s++;
    }
}

// Show error message for 3 seconds
static void show_error(const char *msg) {
    int i = 0;
    while (msg[i] && i < 63) {
        error_msg[i] = msg[i];
        i++;
    }
    error_msg[i] = 0;
    error_tick = api->get_uptime_ticks();
}

// Draw 3D button (classic Mac style)
static void draw_button(int x, int y, int w, int h, const char *label, int pressed) {
    if (pressed) {
        fill_rect(x, y, w, h, BLACK);
        draw_string(x + (w - strlen(label) * 8) / 2, y + (h - 16) / 2, label, WHITE, BLACK);
    } else {
        fill_rect(x, y, w, h, WHITE);
        draw_rect(x, y, w, h, BLACK);
        // 3D effect
        draw_hline(x + 1, y + h - 2, w - 2, GRAY);
        draw_vline(x + w - 2, y + 1, h - 2, GRAY);
        draw_string(x + (w - strlen(label) * 8) / 2, y + (h - 16) / 2, label, BLACK, WHITE);
    }
}

// Draw checkerboard pattern (System 7 style)
static void draw_pattern(int x, int y, int w, int h) {
    for (int py = y; py < y + h && py < gfx.height; py++) {
        for (int px = x; px < x + w && px < gfx.width; px++) {
            if ((px + py) % 2 == 0) {
                gfx.buffer[py * gfx.width + px] = GRAY;
            } else {
                gfx.buffer[py * gfx.width + px] = WHITE;
            }
        }
    }
}

// ============ UI Drawing ============

static void draw_sidebar(void) {
    // Sidebar background
    fill_rect(0, 0, SIDEBAR_W, win_h - CONTROLS_H, WHITE);
    draw_vline(SIDEBAR_W - 1, 0, win_h - CONTROLS_H, BLACK);

    // Title
    draw_string(8, 6, "Albums", BLACK, WHITE);
    draw_hline(4, 24, SIDEBAR_W - 8, BLACK);

    // Album list
    int y = 28;
    int visible_albums = (win_h - CONTROLS_H - 32) / ALBUM_ITEM_H;

    for (int i = album_scroll; i < album_count && i < album_scroll + visible_albums; i++) {
        int item_y = y + (i - album_scroll) * ALBUM_ITEM_H;

        // Highlight selected
        if (i == selected_album) {
            fill_rect(2, item_y, SIDEBAR_W - 4, ALBUM_ITEM_H - 2, BLACK);
            draw_text_clip(6, item_y + 2, albums[i].name, WHITE, BLACK, SIDEBAR_W - 12);
        } else {
            draw_text_clip(6, item_y + 2, albums[i].name, BLACK, WHITE, SIDEBAR_W - 12);
        }
    }

    // Scroll arrows if needed
    if (album_count > visible_albums) {
        if (album_scroll > 0) {
            draw_string(SIDEBAR_W - 16, 28, "^", BLACK, WHITE);
        }
        if (album_scroll + visible_albums < album_count) {
            draw_string(SIDEBAR_W - 16, win_h - CONTROLS_H - 20, "v", BLACK, WHITE);
        }
    }
}

static void draw_track_list(void) {
    int x = SIDEBAR_W;
    int w = win_w - SIDEBAR_W;
    int h = win_h - CONTROLS_H;

    // Background
    fill_rect(x, 0, w, h, WHITE);

    // Show error message for 3 seconds
    if (error_msg[0] && api->get_uptime_ticks() - error_tick < 300) {
        int err_len = strlen(error_msg) * 8;
        int err_x = x + (w - err_len) / 2;
        fill_rect(err_x - 4, h - 30, err_len + 8, 20, BLACK);
        draw_string(err_x, h - 28, error_msg, WHITE, BLACK);
    } else {
        error_msg[0] = 0;  // Clear expired error
    }

    if (selected_album < 0 || selected_album >= album_count) {
        // No album selected
        draw_string(x + w/2 - 64, h/2 - 8, "Select album", BLACK, WHITE);
        return;
    }

    // Album header
    draw_string(x + 8, 6, albums[selected_album].name, BLACK, WHITE);

    // Track count
    char info[32];
    int tc = track_count;
    int len = 0;
    info[len++] = '(';
    if (tc >= 10) info[len++] = '0' + (tc / 10);
    info[len++] = '0' + (tc % 10);
    info[len++] = ' ';
    info[len++] = 't';
    info[len++] = 'r';
    info[len++] = 'a';
    info[len++] = 'c';
    info[len++] = 'k';
    if (tc != 1) info[len++] = 's';
    info[len++] = ')';
    info[len] = 0;
    draw_string(x + 8 + strlen(albums[selected_album].name) * 8 + 8, 6, info, GRAY, WHITE);

    draw_hline(x + 4, 24, w - 8, BLACK);

    // Track list
    int list_y = 28;
    int visible_tracks = (h - 32) / TRACK_ITEM_H;

    for (int i = track_scroll; i < track_count && i < track_scroll + visible_tracks; i++) {
        int item_y = list_y + (i - track_scroll) * TRACK_ITEM_H;

        // Track number
        char num[4];
        num[0] = '0' + ((i + 1) / 10);
        num[1] = '0' + ((i + 1) % 10);
        num[2] = '.';
        num[3] = 0;
        if (num[0] == '0') num[0] = ' ';

        // Highlight selected/playing
        if (i == selected_track) {
            fill_rect(x + 2, item_y, w - 4, TRACK_ITEM_H - 2, BLACK);
            draw_string(x + 6, item_y + 1, num, WHITE, BLACK);
        } else {
            draw_string(x + 6, item_y + 1, num, GRAY, WHITE);
        }

        // Playing indicator
        if (i == playing_track && is_playing) {
            if (i == selected_track) {
                draw_char(x + 6, item_y + 1, '>', WHITE, BLACK);
            } else {
                draw_char(x + 6, item_y + 1, '>', BLACK, WHITE);
            }
        }

        // Track name (strip .mp3)
        char display_name[MAX_NAME_LEN];
        int j = 0;
        for (; tracks[i].name[j] && j < MAX_NAME_LEN - 1; j++) {
            display_name[j] = tracks[i].name[j];
        }
        display_name[j] = 0;
        if (j > 4 && display_name[j-4] == '.' && display_name[j-3] == 'm') {
            display_name[j-4] = 0;
        }

        if (i == selected_track) {
            draw_text_clip(x + 32, item_y + 1, display_name, WHITE, BLACK, w - 40);
        } else {
            draw_text_clip(x + 32, item_y + 1, display_name, BLACK, WHITE, w - 40);
        }
    }
}

static void draw_controls(void) {
    int y = win_h - CONTROLS_H;

    // Background with border
    fill_rect(0, y, win_w, CONTROLS_H, WHITE);
    draw_hline(0, y, win_w, BLACK);

    // Now playing info (left side)
    if (playing_track >= 0 && playing_track < track_count) {
        char display_name[MAX_NAME_LEN];
        int j = 0;
        for (; tracks[playing_track].name[j] && j < MAX_NAME_LEN - 1; j++) {
            display_name[j] = tracks[playing_track].name[j];
        }
        display_name[j] = 0;
        if (j > 4 && display_name[j-4] == '.' && display_name[j-3] == 'm') {
            display_name[j-4] = 0;
        }

        draw_text_clip(8, y + 8, display_name, BLACK, WHITE, 180);
        draw_text_clip(8, y + 26, albums[selected_album].name, GRAY, WHITE, 180);
    } else if (is_loading) {
        const char *status = "Loading...";
        if (load_state == LOAD_STATE_LOADING_FILE) status = "Reading file...";
        else if (load_state == LOAD_STATE_DECODING) status = "Decoding...";
        draw_string(8, y + 16, status, BLACK, WHITE);
    } else {
        draw_string(8, y + 16, "No track", GRAY, WHITE);
    }

    // Center controls
    int cx = win_w / 2;
    int btn_y = y + 8;

    // |< Back
    draw_button(cx - 90, btn_y, 30, 24, "|<", 0);

    // Play/Pause
    if (is_playing) {
        draw_button(cx - 40, btn_y, 80, 24, "Pause", 0);
    } else {
        draw_button(cx - 40, btn_y, 80, 24, "Play", 0);
    }

    // >| Next
    draw_button(cx + 60, btn_y, 30, 24, ">|", 0);

    // Progress bar
    int prog_y = y + 42;
    int prog_x = 8;
    int prog_w = win_w - 100;

    // Time labels
    draw_string(prog_x, prog_y, "0:00", GRAY, WHITE);

    // Bar background
    fill_rect(prog_x + 40, prog_y + 4, prog_w - 80, 8, WHITE);
    draw_rect(prog_x + 40, prog_y + 4, prog_w - 80, 8, BLACK);

    // Progress fill - show for both playing and paused states
    if ((is_playing || (playing_track >= 0 && api->sound_is_paused && api->sound_is_paused()))
        && pcm_samples > 0 && pcm_sample_rate > 0) {
        uint32_t elapsed_ms;
        if (is_playing) {
            uint32_t now = api->get_uptime_ticks ? api->get_uptime_ticks() : 0;
            uint32_t elapsed_ticks = now - playback_start_tick;
            elapsed_ms = elapsed_ticks * 10;  // 100Hz timer
        } else {
            // Paused - use saved position
            elapsed_ms = pause_elapsed_ms;
        }
        uint32_t total_ms = ((uint64_t)pcm_samples * 1000) / pcm_sample_rate;

        if (elapsed_ms > total_ms) elapsed_ms = total_ms;

        int fill_w = ((prog_w - 84) * elapsed_ms) / (total_ms > 0 ? total_ms : 1);
        if (fill_w > 0) {
            // Dithered fill
            for (int py = prog_y + 5; py < prog_y + 11; py++) {
                for (int px = prog_x + 41; px < prog_x + 41 + fill_w; px++) {
                    if ((px + py) % 2 == 0) {
                        gfx.buffer[py * gfx.width + px] = BLACK;
                    }
                }
            }
        }

        // Time display
        int secs = elapsed_ms / 1000;
        int mins = secs / 60;
        secs = secs % 60;
        char time_str[8];
        time_str[0] = '0' + mins;
        time_str[1] = ':';
        time_str[2] = '0' + (secs / 10);
        time_str[3] = '0' + (secs % 10);
        time_str[4] = 0;
        draw_string(prog_x, prog_y, time_str, BLACK, WHITE);

        // Total time
        int total_secs = total_ms / 1000;
        int total_mins = total_secs / 60;
        total_secs = total_secs % 60;
        time_str[0] = '0' + total_mins;
        time_str[1] = ':';
        time_str[2] = '0' + (total_secs / 10);
        time_str[3] = '0' + (total_secs % 10);
        time_str[4] = 0;
        draw_string(prog_x + prog_w - 32, prog_y, time_str, GRAY, WHITE);
    } else {
        draw_string(prog_x + prog_w - 32, prog_y, "0:00", GRAY, WHITE);
    }

    // Volume (right side)
    int vol_x = win_w - 80;
    draw_string(vol_x, y + 8, "Vol:", BLACK, WHITE);

    // Volume bar
    fill_rect(vol_x, y + 28, 70, 10, WHITE);
    draw_rect(vol_x, y + 28, 70, 10, BLACK);
    int vol_fill = (volume * 66) / 100;
    fill_rect(vol_x + 2, y + 30, vol_fill, 6, BLACK);
}

static void draw_all(void) {
    draw_sidebar();
    draw_track_list();
    draw_controls();
    api->window_invalidate(window_id);
}

// ============ Album/Track Loading ============

static void scan_albums(void) {
    album_count = 0;

    void *dir = api->open("/home/user/Music");
    if (!dir || !api->is_dir(dir)) {
        return;
    }

    char name[MAX_NAME_LEN];
    uint8_t type;
    int idx = 0;

    while (album_count < MAX_ALBUMS && api->readdir(dir, idx, name, sizeof(name), &type) == 0) {
        idx++;
        if (name[0] == '.') continue;
        if (type != 2) continue;  // 2 = directory

        int i = 0;
        while (name[i] && i < MAX_NAME_LEN - 1) {
            albums[album_count].name[i] = name[i];
            i++;
        }
        albums[album_count].name[i] = 0;

        char *p = albums[album_count].path;
        const char *base = "/home/user/Music/";
        while (*base) *p++ = *base++;
        i = 0;
        while (name[i]) *p++ = name[i++];
        *p = 0;

        album_count++;
    }
}

static void load_tracks(int album_idx) {
    track_count = 0;
    selected_track = -1;
    track_scroll = 0;

    if (album_idx < 0 || album_idx >= album_count) return;

    void *dir = api->open(albums[album_idx].path);
    if (!dir || !api->is_dir(dir)) return;

    char name[MAX_NAME_LEN];
    uint8_t type;
    int idx = 0;

    while (track_count < MAX_TRACKS && api->readdir(dir, idx, name, sizeof(name), &type) == 0) {
        idx++;
        if (name[0] == '.') continue;
        if (type == 2) continue;

        int len = 0;
        while (name[len]) len++;
        if (len < 4) continue;
        if (name[len-4] != '.' || name[len-3] != 'm' || name[len-2] != 'p' || name[len-1] != '3') {
            continue;
        }

        int i = 0;
        while (name[i] && i < MAX_NAME_LEN - 1) {
            tracks[track_count].name[i] = name[i];
            i++;
        }
        tracks[track_count].name[i] = 0;

        char *p = tracks[track_count].path;
        const char *base = albums[album_idx].path;
        while (*base) *p++ = *base++;
        *p++ = '/';
        i = 0;
        while (name[i]) *p++ = name[i++];
        *p = 0;

        track_count++;
    }
}

// ============ Playback ============

static int play_track(int track_idx) {
    if (track_idx < 0 || track_idx >= track_count) return -1;

    // Stop current playback
    if (is_playing) {
        api->sound_stop();
        is_playing = 0;
    }

    // Free old buffer
    if (pcm_buffer) {
        api->free(pcm_buffer);
        pcm_buffer = NULL;
    }

    is_loading = 1;
    load_state = LOAD_STATE_LOADING_FILE;
    draw_all();
    api->yield();

    // Load file
    void *file = api->open(tracks[track_idx].path);
    if (!file) {
        is_loading = 0;
        load_state = LOAD_STATE_IDLE;
        show_error("Cannot open file");
        return -1;
    }

    int size = api->file_size(file);
    if (size <= 0) {
        is_loading = 0;
        load_state = LOAD_STATE_IDLE;
        show_error("Empty file");
        return -1;
    }

    uint8_t *mp3_data = api->malloc(size);
    if (!mp3_data) {
        is_loading = 0;
        load_state = LOAD_STATE_IDLE;
        show_error("Out of memory (file too large)");
        return -1;
    }

    int offset = 0;
    while (offset < size) {
        int n = api->read(file, (char *)mp3_data + offset, size - offset, offset);
        if (n <= 0) break;
        offset += n;
    }

    // Switch to decoding state
    load_state = LOAD_STATE_DECODING;
    draw_all();
    api->yield();

    // Single-pass decode with pre-allocated buffer
    // For 10MB MP3 @ 128kbps stereo: ~50 min = ~530MB PCM
    // Ratio ~53:1, but 320kbps would be ~21:1. Use 15x for safety.
    uint32_t max_pcm_bytes = (uint32_t)size * 15;
    pcm_buffer = api->malloc(max_pcm_bytes);
    if (!pcm_buffer) {
        api->free(mp3_data);
        is_loading = 0;
        load_state = LOAD_STATE_IDLE;
        show_error("Out of memory (song too long)");
        return -1;
    }

    mp3dec_t mp3d;
    mp3dec_init(&mp3d);
    mp3dec_frame_info_t info;
    int16_t temp_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

    const uint8_t *ptr = mp3_data;
    int remaining = size;
    int16_t *out_ptr = pcm_buffer;
    uint32_t decoded_samples = 0;
    int channels = 0;

    while (remaining > 0) {
        int samples = mp3dec_decode_frame(&mp3d, ptr, remaining, temp_pcm, &info);
        if (info.frame_bytes == 0) break;
        if (samples > 0) {
            if (channels == 0) {
                channels = info.channels;
                pcm_sample_rate = info.hz;
            }
            decoded_samples += samples;
            if (channels == 1) {
                for (int i = 0; i < samples; i++) {
                    *out_ptr++ = temp_pcm[i];
                    *out_ptr++ = temp_pcm[i];
                }
            } else {
                for (int i = 0; i < samples * 2; i++) {
                    *out_ptr++ = temp_pcm[i];
                }
            }
        }
        ptr += info.frame_bytes;
        remaining -= info.frame_bytes;
    }

    api->free(mp3_data);

    if (decoded_samples == 0 || channels == 0) {
        api->free(pcm_buffer);
        pcm_buffer = NULL;
        is_loading = 0;
        load_state = LOAD_STATE_IDLE;
        show_error("Invalid MP3 format");
        return -1;
    }

    // Use actual decoded sample count for accurate duration
    pcm_samples = decoded_samples;
    playing_track = track_idx;
    is_playing = 1;
    is_loading = 0;
    load_state = LOAD_STATE_IDLE;
    playback_start_tick = api->get_uptime_ticks ? api->get_uptime_ticks() : 0;
    pause_elapsed_ms = 0;

    api->sound_play_pcm_async(pcm_buffer, pcm_samples, 2, pcm_sample_rate);

    return 0;
}

static void toggle_play_pause(void) {
    if (playing_track < 0) {
        // Nothing loaded - play selected or first track
        if (selected_track >= 0) {
            play_track(selected_track);
        } else if (track_count > 0) {
            play_track(0);
        }
    } else if (is_playing) {
        // Currently playing - pause it
        if (api->sound_pause) {
            // Save current elapsed time before pausing
            uint32_t now = api->get_uptime_ticks ? api->get_uptime_ticks() : 0;
            uint32_t elapsed_ticks = now - playback_start_tick;
            pause_elapsed_ms = elapsed_ticks * 10;  // 100Hz timer
            api->sound_pause();
            is_playing = 0;
        }
    } else {
        // Currently paused - resume
        if (api->sound_resume && api->sound_is_paused && api->sound_is_paused()) {
            // Adjust start tick so progress bar continues from paused position
            uint32_t now = api->get_uptime_ticks ? api->get_uptime_ticks() : 0;
            playback_start_tick = now - (pause_elapsed_ms / 10);
            api->sound_resume();
            is_playing = 1;
        } else {
            // Fallback: restart from beginning
            playback_start_tick = api->get_uptime_ticks ? api->get_uptime_ticks() : 0;
            pause_elapsed_ms = 0;
            api->sound_play_pcm_async(pcm_buffer, pcm_samples, 2, pcm_sample_rate);
            is_playing = 1;
        }
    }
}

static void next_track(void) {
    if (track_count == 0) return;
    int next = (playing_track >= 0 ? playing_track : selected_track) + 1;
    if (next >= track_count) next = 0;
    play_track(next);
}

static void prev_track(void) {
    if (track_count == 0) return;
    int prev = (playing_track >= 0 ? playing_track : selected_track) - 1;
    if (prev < 0) prev = track_count - 1;
    play_track(prev);
}

// ============ Event Handling ============

static void handle_click(int mx, int my) {
    int ctrl_y = win_h - CONTROLS_H;

    // Click in controls area
    if (my >= ctrl_y) {
        int cx = win_w / 2;
        int btn_y = ctrl_y + 8;

        // Back button
        if (mx >= cx - 90 && mx < cx - 60 && my >= btn_y && my < btn_y + 24) {
            prev_track();
            return;
        }

        // Play/Pause
        if (mx >= cx - 40 && mx < cx + 40 && my >= btn_y && my < btn_y + 24) {
            toggle_play_pause();
            return;
        }

        // Next button
        if (mx >= cx + 60 && mx < cx + 90 && my >= btn_y && my < btn_y + 24) {
            next_track();
            return;
        }

        // Volume bar
        int vol_x = win_w - 80;
        if (mx >= vol_x && mx < vol_x + 70 && my >= ctrl_y + 28 && my < ctrl_y + 38) {
            volume = ((mx - vol_x) * 100) / 70;
            if (volume < 0) volume = 0;
            if (volume > 100) volume = 100;
            return;
        }

        return;
    }

    // Click in sidebar (albums)
    if (mx < SIDEBAR_W) {
        int y = 28;
        int visible_albums = (win_h - CONTROLS_H - 32) / ALBUM_ITEM_H;

        for (int i = album_scroll; i < album_count && i < album_scroll + visible_albums; i++) {
            int item_y = y + (i - album_scroll) * ALBUM_ITEM_H;
            if (my >= item_y && my < item_y + ALBUM_ITEM_H) {
                selected_album = i;
                load_tracks(i);
                return;
            }
        }
        return;
    }

    // Click in track list
    if (selected_album >= 0) {
        int list_y = 28;
        int visible_tracks = (win_h - CONTROLS_H - 32) / TRACK_ITEM_H;

        for (int i = track_scroll; i < track_count && i < track_scroll + visible_tracks; i++) {
            int item_y = list_y + (i - track_scroll) * TRACK_ITEM_H;
            if (my >= item_y && my < item_y + TRACK_ITEM_H) {
                selected_track = i;
                return;
            }
        }
    }
}

static void handle_double_click(int mx, int my) {
    if (mx >= SIDEBAR_W && selected_album >= 0) {
        int list_y = 28;
        int visible_tracks = (win_h - CONTROLS_H - 32) / TRACK_ITEM_H;

        for (int i = track_scroll; i < track_count && i < track_scroll + visible_tracks; i++) {
            int item_y = list_y + (i - track_scroll) * TRACK_ITEM_H;
            if (my >= item_y && my < item_y + TRACK_ITEM_H) {
                play_track(i);
                return;
            }
        }
    }
}

// ============ Main ============

int main(kapi_t *k, int argc, char **argv) {
    (void)argc; (void)argv;
    api = k;

    // Create window
    win_w = 500;
    win_h = 400;
    window_id = api->window_create(150, 80, win_w, win_h, "Music");
    if (window_id < 0) {
        return 1;
    }

    int bw, bh;
    win_buffer = api->window_get_buffer(window_id, &bw, &bh);
    if (!win_buffer) {
        api->window_destroy(window_id);
        return 1;
    }

    gfx_init(&gfx, win_buffer, bw, bh, api->font_data);

    scan_albums();
    draw_all();

    int running = 1;
    int last_click_tick = 0;
    int last_click_x = -1, last_click_y = -1;

    while (running) {
        int event_type, data1, data2, data3;

        while (api->window_poll_event(window_id, &event_type, &data1, &data2, &data3)) {
            switch (event_type) {
                case WIN_EVENT_CLOSE:
                    running = 0;
                    break;

                case WIN_EVENT_MOUSE_DOWN: {
                    int mx = data1;
                    int my = data2;

                    uint32_t now = api->get_uptime_ticks ? api->get_uptime_ticks() : 0;
                    if (now - last_click_tick < 30 &&
                        mx >= last_click_x - 5 && mx <= last_click_x + 5 &&
                        my >= last_click_y - 5 && my <= last_click_y + 5) {
                        handle_double_click(mx, my);
                    } else {
                        handle_click(mx, my);
                    }
                    last_click_tick = now;
                    last_click_x = mx;
                    last_click_y = my;
                    break;
                }

                case WIN_EVENT_KEY: {
                    int key = data1;
                    if (key == ' ') {
                        toggle_play_pause();
                    } else if (key == 'n' || key == 'N' || key == 0x103) {
                        next_track();
                    } else if (key == 'p' || key == 'P' || key == 0x102) {
                        prev_track();
                    } else if (key == 0x101) {
                        if (selected_track < track_count - 1) selected_track++;
                    } else if (key == 0x100) {
                        if (selected_track > 0) selected_track--;
                    } else if (key == '\n' || key == '\r') {
                        if (selected_track >= 0) play_track(selected_track);
                    } else if (key == 'q' || key == 'Q') {
                        running = 0;
                    }
                    break;
                }

                case WIN_EVENT_RESIZE: {
                    // Re-fetch buffer with new dimensions
                    int bw, bh;
                    win_buffer = api->window_get_buffer(window_id, &bw, &bh);
                    win_w = bw;
                    win_h = bh;
                    gfx_init(&gfx, win_buffer, bw, bh, api->font_data);
                    break;
                }
            }
        }

        // Check if playback finished
        if (is_playing && api->sound_is_playing && !api->sound_is_playing()) {
            next_track();
        }

        draw_all();
        api->yield();
    }

    if (is_playing) {
        api->sound_stop();
    }
    if (pcm_buffer) {
        api->free(pcm_buffer);
    }
    api->window_destroy(window_id);

    return 0;
}
