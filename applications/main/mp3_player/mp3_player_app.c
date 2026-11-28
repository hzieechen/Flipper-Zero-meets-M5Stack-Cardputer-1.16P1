#include "mp3_player.h"
#include "mp3_storage.h"
#include "mp3_i2s.h"
#include "mp3_decoder.h"

#include <stdio.h>
#include <string.h>

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <storage/storage.h>

#define TAG MP3_PLAYER_TAG

/* T-Embed mono canvas resolution (the GUI service scales 2x to RGB565). */
#define BROWSER_VISIBLE_ROWS 4
#define BROWSER_ROW_H        13
#define BROWSER_ROW_X_PAD    4

/* ---------- decoder callback ---------- */

static void on_track_ended(void* ctx) {
    Mp3App* app = ctx;
    Mp3Event ev = {.type = Mp3EventTypeTrackEnded};
    furi_message_queue_put(app->event_queue, &ev, 0);
}

/* ---------- input ---------- */

static void mp3_input_callback(InputEvent* input_event, void* ctx) {
    Mp3App* app = ctx;
    Mp3Event ev = {.type = Mp3EventTypeKey, .input = *input_event};
    furi_message_queue_put(app->event_queue, &ev, FuriWaitForever);
}

static void mp3_tick_callback(void* ctx) {
    Mp3App* app = ctx;
    Mp3Event ev = {.type = Mp3EventTypeTick};
    furi_message_queue_put(app->event_queue, &ev, 0);
}

/* ---------- helpers ---------- */

static void format_mmss(uint32_t ms, char* out, size_t out_size) {
    uint32_t total = ms / 1000;
    snprintf(out, out_size, "%02lu:%02lu", (unsigned long)(total / 60), (unsigned long)(total % 60));
}

/* Strip the .mp3/.wav extension for display, copy into out. */
static void track_display_name(const Mp3Track* t, char* out, size_t out_size) {
    strncpy(out, t->name, out_size - 1);
    out[out_size - 1] = '\0';
    size_t len = strlen(out);
    if(len >= 4 && out[len - 4] == '.') {
        const char* ext = out + len - 3;
        bool is_mp3 = (ext[0] == 'm' || ext[0] == 'M') &&
                      (ext[1] == 'p' || ext[1] == 'P') && ext[2] == '3';
        bool is_wav = (ext[0] == 'w' || ext[0] == 'W') &&
                      (ext[1] == 'a' || ext[1] == 'A') && (ext[2] == 'v' || ext[2] == 'V');
        if(is_mp3 || is_wav) out[len - 4] = '\0';
    }
}

/* Playlist/path helpers — which list is "current" depends on the view. */
static const Mp3Playlist* playlist_for_dir(const Mp3App* app, Mp3Dir dir) {
    return dir == Mp3DirRecordings ? &app->recordings : &app->playlist;
}

static const char* path_for_dir(Mp3Dir dir) {
    return dir == Mp3DirRecordings ? MP3_RECORDINGS_DIR : MP3_PLAYER_DATA_DIR;
}

/* ---------- render: list (shared by browser + recordings) ---------- */

/* Draw a playlist. `cursor` is 0..count-1, or -1 when a special row 0
 * (the "录音机" entry) is selected. `has_rec_entry` adds that first row. */
static void render_track_list(
    Canvas* canvas,
    const Mp3Playlist* pl,
    int32_t cursor,
    bool has_rec_entry) {
    canvas_set_font(canvas, FontSecondary);

    /* Row index space: 0 = 录音机 entry (optional); 1..count = tracks.
     * Map cursor (which counts only real tracks, -1 = entry) to a row. */
    int32_t cursor_row = has_rec_entry ? cursor + 1 : cursor;
    int32_t total_rows = (int32_t)pl->count + (has_rec_entry ? 1 : 0);
    if(total_rows == 0) {
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "无曲目");
        return;
    }

    int32_t top = cursor_row - BROWSER_VISIBLE_ROWS / 2;
    if(top < 0) top = 0;
    int32_t max_top = total_rows - BROWSER_VISIBLE_ROWS;
    if(max_top < 0) max_top = 0;
    if(top > max_top) top = max_top;

    int32_t end = top + BROWSER_VISIBLE_ROWS;
    if(end > total_rows) end = total_rows;

    char buf[MP3_PLAYER_NAME_MAX];
    for(int32_t row = top; row < end; row++) {
        int32_t y = 14 + (row - top) * BROWSER_ROW_H;
        bool selected = (row == cursor_row);
        if(selected) {
            canvas_draw_box(canvas, 0, y, 128, BROWSER_ROW_H);
            canvas_invert_color(canvas);
        }
        if(has_rec_entry && row == 0) {
            canvas_draw_str(canvas, BROWSER_ROW_X_PAD, y + 10, "录音机 >");
        } else {
            int32_t idx = row - (has_rec_entry ? 1 : 0);
            track_display_name(&pl->tracks[idx], buf, sizeof(buf));
            canvas_draw_str(canvas, BROWSER_ROW_X_PAD, y + 10, buf);
        }
        if(selected) canvas_invert_color(canvas);
    }
}

/* ---------- render: browser ---------- */

static void render_browser(Canvas* canvas, Mp3App* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "MP3 Player");
    canvas_draw_line(canvas, 0, 12, 127, 12);
    render_track_list(canvas, &app->playlist, app->selected, true);
}

/* ---------- render: recordings ---------- */

static void render_recordings(Canvas* canvas, Mp3App* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "录音机");
    canvas_draw_line(canvas, 0, 12, 127, 12);
    render_track_list(canvas, &app->recordings, app->selected_rec, false);
}

/* ---------- render: now-playing ---------- */

static void render_now_playing(Canvas* canvas, Mp3App* app) {
    canvas_set_font(canvas, FontPrimary);

    char title[MP3_PLAYER_NAME_MAX];
    const Mp3Playlist* playing_pl = playlist_for_dir(app, app->playing_dir);
    if(app->playing_index >= 0 && (size_t)app->playing_index < playing_pl->count) {
        track_display_name(&playing_pl->tracks[app->playing_index], title, sizeof(title));
    } else {
        snprintf(title, sizeof(title), "—");
    }
    canvas_draw_str(canvas, 2, 10, title);
    canvas_draw_line(canvas, 0, 12, 127, 12);

    /* Time */
    canvas_set_font(canvas, FontSecondary);
    char elapsed[16], total[16];
    format_mmss(app->elapsed_ms, elapsed, sizeof(elapsed));
    if(app->duration_ms > 0) {
        format_mmss(app->duration_ms, total, sizeof(total));
    } else {
        strncpy(total, "--:--", sizeof(total));
    }
    canvas_draw_str(canvas, 2, 26, elapsed);
    canvas_draw_str_aligned(canvas, 126, 26, AlignRight, AlignBottom, total);

    /* Progress bar */
    int32_t bar_y = 30;
    int32_t bar_w = 124;
    canvas_draw_frame(canvas, 2, bar_y, bar_w, 6);
    if(app->duration_ms > 0) {
        uint32_t fill = (app->elapsed_ms * (uint32_t)(bar_w - 2)) / app->duration_ms;
        if(fill > (uint32_t)(bar_w - 2)) fill = bar_w - 2;
        canvas_draw_box(canvas, 3, bar_y + 1, fill, 4);
    }

    /* Status & volume */
    const char* status =
        (app->playback == Mp3StatePlaying) ? "Playing" :
        (app->playback == Mp3StatePaused)  ? "Paused"  : "Stopped";
    canvas_draw_str(canvas, 2, 50, status);
    char vol[16];
    snprintf(vol, sizeof(vol), "Vol %u%%", (unsigned)app->volume);
    canvas_draw_str_aligned(canvas, 126, 50, AlignRight, AlignBottom, vol);

    elements_button_left(canvas, "返回");
    elements_button_center(canvas, app->playback == Mp3StatePlaying ? "Pause" : "Play");
}

static void mp3_render_callback(Canvas* canvas, void* ctx) {
    Mp3App* app = ctx;
    if(furi_mutex_acquire(app->mutex, 100) != FuriStatusOk) return;

    canvas_clear(canvas);
    if(app->view == Mp3ViewBrowser) {
        render_browser(canvas, app);
    } else if(app->view == Mp3ViewRecordings) {
        render_recordings(canvas, app);
    } else {
        render_now_playing(canvas, app);
    }

    furi_mutex_release(app->mutex);
}

/* ---------- actions ---------- */

static bool start_track(Mp3App* app, Mp3Dir dir, int32_t index) {
    const Mp3Playlist* pl = playlist_for_dir(app, dir);
    if(index < 0 || (size_t)index >= pl->count) return false;
    char path[256];
    if(!mp3_storage_track_path(pl, index, path_for_dir(dir), path, sizeof(path))) return false;

    FURI_LOG_I(TAG, "start_track dir=%d idx=%ld path=%s", (int)dir, (long)index, path);

    if(!mp3_decoder_play(path)) {
        FURI_LOG_E(TAG, "decoder_play failed for %s", path);
        return false;
    }
    app->playing_dir   = dir;
    app->playing_index = index;
    app->playback      = Mp3StatePlaying;
    app->elapsed_ms    = 0;
    app->duration_ms   = 0;
    app->view          = Mp3ViewNowPlaying;
    return true;
}

static void stop_track(Mp3App* app) {
    mp3_decoder_stop();
    app->playback = Mp3StateIdle;
}

static void toggle_pause(Mp3App* app) {
    if(app->playback == Mp3StatePlaying) {
        mp3_decoder_pause();
        app->playback = Mp3StatePaused;
    } else if(app->playback == Mp3StatePaused) {
        mp3_decoder_resume();
        app->playback = Mp3StatePlaying;
    }
}

static void volume_step(Mp3App* app, int delta) {
    int v = (int)app->volume + delta;
    if(v < 0) v = 0;
    if(v > 100) v = 100;
    app->volume = (uint8_t)v;
    mp3_i2s_set_volume(app->volume);
}

/* ---------- input handling ---------- */

/* Cursor domain: -1 = 录音机 entry, 0..count-1 = music tracks. */
static bool handle_browser_input(Mp3App* app, const InputEvent* in) {
    if(in->type != InputTypeShort && in->type != InputTypeRepeat) return true;

    switch(in->key) {
    case InputKeyUp:
        app->selected = app->selected <= 0 ? -1 : app->selected - 1;
        break;
    case InputKeyDown:
        if(app->selected < (int32_t)app->playlist.count - 1) app->selected++;
        break;
    case InputKeyOk:
        if(app->selected == -1) {
            /* Enter the recordings submenu (re-scan on entry for fresh files). */
            Storage* storage = furi_record_open(RECORD_STORAGE);
            mp3_storage_scan(storage, &app->recordings, MP3_RECORDINGS_DIR, ".wav");
            furi_record_close(RECORD_STORAGE);
            app->selected_rec = 0;
            app->view = Mp3ViewRecordings;
        } else {
            start_track(app, Mp3DirMusic, app->selected);
        }
        break;
    case InputKeyBack:
        return false;  /* exit app */
    default:
        break;
    }
    return true;
}

static bool handle_recordings_input(Mp3App* app, const InputEvent* in) {
    if(in->type != InputTypeShort && in->type != InputTypeRepeat) return true;

    switch(in->key) {
    case InputKeyUp:
        if(app->recordings.count > 0) {
            app->selected_rec = (app->selected_rec - 1 + (int32_t)app->recordings.count) %
                                (int32_t)app->recordings.count;
        }
        break;
    case InputKeyDown:
        if(app->recordings.count > 0) {
            app->selected_rec = (app->selected_rec + 1) % (int32_t)app->recordings.count;
        }
        break;
    case InputKeyOk:
        start_track(app, Mp3DirRecordings, app->selected_rec);
        break;
    case InputKeyBack:
        app->view = Mp3ViewBrowser;
        break;  /* back to browser, keep any playback going */
    default:
        break;
    }
    return true;
}

static bool handle_now_playing_input(Mp3App* app, const InputEvent* in) {
    if(in->type != InputTypeShort && in->type != InputTypeRepeat) {
        if(in->key == InputKeyBack && in->type == InputTypeLong) {
            /* Long-back from Now-Playing: stop and exit. */
            stop_track(app);
            return false;
        }
        return true;
    }

    switch(in->key) {
    case InputKeyUp:
        volume_step(app, +5);
        break;
    case InputKeyDown:
        volume_step(app, -5);
        break;
    case InputKeyLeft:
        /* Previous track within the same directory we're playing from. */
        if(app->playing_index > 0) {
            start_track(app, app->playing_dir, app->playing_index - 1);
        }
        break;
    case InputKeyRight: {
        const Mp3Playlist* pl = playlist_for_dir(app, app->playing_dir);
        if(app->playing_index + 1 < (int32_t)pl->count) {
            start_track(app, app->playing_dir, app->playing_index + 1);
        }
        break;
    }
    case InputKeyOk:
        toggle_pause(app);
        break;
    case InputKeyBack:
        /* Short-back: return to browser, keep playback going. */
        app->view = Mp3ViewBrowser;
        break;
    default:
        break;
    }
    return true;
}

/* ---------- entry point ---------- */

int32_t mp3_player_app(void* p) {
    UNUSED(p);
    FURI_LOG_I(TAG, "starting");

    Mp3App* app = malloc(sizeof(Mp3App));
    app->mutex       = furi_mutex_alloc(FuriMutexTypeNormal);
    app->event_queue = furi_message_queue_alloc(16, sizeof(Mp3Event));
    app->view        = Mp3ViewBrowser;
    app->selected    = -1;           /* start on the 录音机 entry */
    app->selected_rec = 0;
    app->playback    = Mp3StateIdle;
    app->playing_dir = Mp3DirMusic;
    app->playing_index = -1;
    app->volume      = 80;
    app->elapsed_ms  = 0;
    app->duration_ms = 0;

    if(!mp3_storage_playlist_alloc(&app->playlist, MP3_PLAYER_MAX_TRACKS) ||
       !mp3_storage_playlist_alloc(&app->recordings, MP3_RECORDINGS_MAX_TRACKS)) {
        FURI_LOG_E(TAG, "playlist alloc failed");
        mp3_storage_playlist_free(&app->playlist);
        goto cleanup_pre;
    }

    /* Tear down the standard speaker HAL — it owns I2S_NUM_1 on Cardputer-Adv
     * and registers a channel even when nobody is playing tones. We need
     * exclusive ownership for our own writer thread. Restored on exit. */
    furi_hal_speaker_deinit();
    if(!mp3_i2s_init(44100)) {
        FURI_LOG_E(TAG, "i2s init failed");
        furi_hal_speaker_init();   /* restore on failure */
        goto cleanup_playlist;
    }
    mp3_i2s_set_volume(app->volume);

    if(!mp3_decoder_init()) {
        FURI_LOG_E(TAG, "decoder init failed");
        goto cleanup_i2s;
    }
    mp3_decoder_set_ended_callback(on_track_ended, app);

    /* Scan for tracks in both directories. */
    Storage* storage = furi_record_open(RECORD_STORAGE);
    mp3_storage_scan(storage, &app->playlist, MP3_PLAYER_DATA_DIR, ".mp3");
    mp3_storage_scan(storage, &app->recordings, MP3_RECORDINGS_DIR, ".wav");
    furi_record_close(RECORD_STORAGE);

    /* GUI setup. */
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, mp3_render_callback, app);
    view_port_input_callback_set(view_port, mp3_input_callback, app);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    FuriTimer* timer = furi_timer_alloc(mp3_tick_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(timer, furi_kernel_get_tick_frequency() / 4); /* 4 Hz UI tick */

    /* Main event loop. */
    Mp3Event ev;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(app->event_queue, &ev, 200) != FuriStatusOk) continue;

        furi_mutex_acquire(app->mutex, FuriWaitForever);

        switch(ev.type) {
        case Mp3EventTypeKey:
            if(app->view == Mp3ViewBrowser) {
                running = handle_browser_input(app, &ev.input);
            } else if(app->view == Mp3ViewRecordings) {
                running = handle_recordings_input(app, &ev.input);
            } else {
                running = handle_now_playing_input(app, &ev.input);
            }
            break;
        case Mp3EventTypeTick:
            mp3_decoder_get_progress(&app->elapsed_ms, &app->duration_ms);
            if(mp3_decoder_is_paused())       app->playback = Mp3StatePaused;
            else if(mp3_decoder_is_playing()) app->playback = Mp3StatePlaying;
            break;
        case Mp3EventTypeTrackEnded:
            FURI_LOG_I(TAG, "track ended");
            /* Auto-advance within the directory currently playing. */
            {
                const Mp3Playlist* pl = playlist_for_dir(app, app->playing_dir);
                if(app->playing_index + 1 < (int32_t)pl->count) {
                    start_track(app, app->playing_dir, app->playing_index + 1);
                } else {
                    app->playback = Mp3StateIdle;
                }
            }
            break;
        }

        furi_mutex_release(app->mutex);
        view_port_update(view_port);
    }

    /* Teardown. */
    furi_timer_stop(timer);
    furi_timer_free(timer);
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);

    mp3_decoder_stop();
    mp3_decoder_deinit();
    mp3_i2s_deinit();
    furi_hal_speaker_init();   /* restore standard speaker HAL */
    mp3_storage_playlist_free(&app->playlist);
    mp3_storage_playlist_free(&app->recordings);
    furi_message_queue_free(app->event_queue);
    furi_mutex_free(app->mutex);
    free(app);
    FURI_LOG_I(TAG, "exit");
    return 0;

cleanup_i2s:
    mp3_i2s_deinit();
    furi_hal_speaker_init();
cleanup_playlist:
    mp3_storage_playlist_free(&app->playlist);
    mp3_storage_playlist_free(&app->recordings);
cleanup_pre:
    furi_message_queue_free(app->event_queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 255;
}
