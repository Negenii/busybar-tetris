/**
 * @file tetris.c
 * @brief Tetris clock on the 72x16 front panel.
 *
 * Catalogue app: builds to a .fap, is not deployed with the firmware image.
 * The clock itself is in tetris_render.c, which knows nothing about the
 * device; this file is the shell around it, the same shape as the Nixie
 * clock: app_shell for Start and Setup, a periodic timer driving a Canvas on
 * the front panel, a MirrorCard on the back, the settings kept in a file.
 */

#include <furi.h>

#include <gui/gui.h>
#include <gui/modules/canvas.h>
#include <gui/modules/mirror_card.h>
#include <storage/storage.h>
#include <time/time.h>

#include "../common/app_shell.h"
#include "tetris_render.h"

#define TAG                     "Tetris"
#define TETRIS_INPUT_QUEUE_SIZE (16U)
/* Thirty frames a second: a piece falls a row in about 10 ms at the fast end,
 * so this is what keeps the fall smooth rather than what it is clocked by. */
#define TETRIS_FRAME_MS (33U)
/* How often to re-read the local time in whole seconds, in case the service
 * moved it (NTP, a timezone change). */
#define TETRIS_RESYNC_MS (10000U)
/* A year before this is a clock nobody has set yet: show zeros, not 1970. */
#define TETRIS_CLOCK_SET_YEAR (2025U)

#define TETRIS_SETTINGS_DIR   "/ext/apps_data/tetris"
#define TETRIS_SETTINGS_PATH  TETRIS_SETTINGS_DIR "/settings"
#define TETRIS_SETTINGS_MAGIC (0x3354U) /* "T3" (spike): one byte per Setup row */

typedef enum {
    TetrisCustomEventBack = 1UL << 0,
    TetrisCustomEventStart = 1UL << 1,
    TetrisCustomEventSetup = 1UL << 2,
} TetrisCustomEvent;

typedef enum {
    TetrisScreenMenu,
    TetrisScreenSetup,
    TetrisScreenRun,
} TetrisScreen;

typedef enum {
    TetrisSettingColour,
    TetrisSettingHour24,
    TetrisSettingSpeed,
    TetrisSettingColon,
    TetrisSettingSeconds,
    TetrisSettingAnimation,
    TetrisSettingCount,
} TetrisSettingId;

static const char* tetris_colour_labels[] = {"Classic", "Amber", "White", "Green", "Settle"};
static const char* tetris_hour24_labels[] = {"Off", "On"};
static const char* tetris_speed_labels[] = {"Slowest", "Slow", "Medium", "Fast", "Fastest"};
static const char* tetris_colon_labels[] = {"Blink", "Solid", "Off"};
static const char* tetris_seconds_labels[] = {"Off", "Classic", "Quick", "Quicker", "Snap"};
static const char* tetris_animation_labels[] = {"Off", "On"};
/* The TC002 speed byte for each step; Medium is its default. */
static const uint8_t tetris_speed_bytes[] = {0U, 64U, 128U, 192U, 255U};

typedef struct {
    FuriEventLoop* event_loop;
    FuriEventLoopTimer* timer;
    FuriMessageQueue* input_queue;
    Gui* gui;
    Time* time;

    Canvas* canvas;
    MirrorCard* mirror;
    AppShellChrome chrome;
    Widget* front_widget;
    Widget* back_widget;

    TetrisScreen screen;
    AppShellStart start;
    AppShellSetup setup;
    AppShellSetting setting_items[TetrisSettingCount];
    AppShellSettings settings;

    TetrisLook look;
    TetrisState state;
    uint8_t* frame; /* TETRIS_FRAME_BYTES */

    /* The clock, anchored: local seconds of the day as of the last resync,
     * and the RTC millisecond timestamp that second began at. */
    int64_t anchor_day_s;
    time_t anchor_ms;
    uint32_t last_resync_tick;
    bool clock_set;
} Tetris;

/* ---------------------------------------------------------------- settings */

static void tetris_apply_look(Tetris* instance) {
    const AppShellSetting* s = instance->setting_items;
    instance->look.colour = s[TetrisSettingColour].value;
    instance->look.hour24 = s[TetrisSettingHour24].value != 0U;
    instance->look.speed = tetris_speed_bytes[s[TetrisSettingSpeed].value];
    instance->look.colon = s[TetrisSettingColon].value;
    instance->look.seconds = s[TetrisSettingSeconds].value;
    instance->look.still = s[TetrisSettingAnimation].value == 0U;
}

/* A magic, then one byte per row. Anything else is the defaults. */
static void tetris_settings_load(Tetris* instance) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    uint8_t buf[2 + TetrisSettingCount];

    if(storage_file_open(file, TETRIS_SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        const size_t got = storage_file_read(file, buf, sizeof(buf));
        if((got == sizeof(buf)) && (buf[0] == (TETRIS_SETTINGS_MAGIC & 0xFFU)) &&
           (buf[1] == (TETRIS_SETTINGS_MAGIC >> 8))) {
            for(uint8_t i = 0U; i < TetrisSettingCount; i++) {
                if(buf[2 + i] < instance->setting_items[i].count) {
                    instance->setting_items[i].value = buf[2 + i];
                }
            }
        }
        storage_file_close(file);
    }

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static void tetris_settings_save(Tetris* instance) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    uint8_t buf[2 + TetrisSettingCount];

    buf[0] = TETRIS_SETTINGS_MAGIC & 0xFFU;
    buf[1] = TETRIS_SETTINGS_MAGIC >> 8;
    for(uint8_t i = 0U; i < TetrisSettingCount; i++) {
        buf[2 + i] = instance->setting_items[i].value;
    }

    storage_common_mkdir(storage, TETRIS_SETTINGS_DIR);
    if(storage_file_open(file, TETRIS_SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, buf, sizeof(buf));
        storage_file_close(file);
    } else {
        FURI_LOG_W(TAG, "Could not save settings");
    }

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

/* -------------------------------------------------------------------- time */

static void tetris_resync(Tetris* instance) {
    /* Both reads are of the same clock, so the millisecond phase of the RTC
     * stamp is the phase of the local second: timezones are whole minutes. */
    const time_t now_ms = time_get_timestamp_ms();
    const LocalTime local = time_get_local_time(instance->time);
    instance->anchor_day_s = (int64_t)local.dt.hour * 3600 + (int64_t)local.dt.minute * 60 +
                             (int64_t)local.dt.second;
    instance->anchor_ms = now_ms - (now_ms % 1000);
    instance->last_resync_tick = furi_get_tick();
    instance->clock_set = local.dt.year >= TETRIS_CLOCK_SET_YEAR;
}

/* Seconds keep counting past midnight until the next resync; the renderer
 * only uses the time of day, and a backwards step is clamped there. */
static void tetris_now(Tetris* instance, int64_t* day_s, uint32_t* ms) {
    if((furi_get_tick() - instance->last_resync_tick) >= TETRIS_RESYNC_MS) {
        tetris_resync(instance);
    }
    const time_t now_ms = time_get_timestamp_ms();
    int64_t elapsed = (int64_t)now_ms - (int64_t)instance->anchor_ms;
    if(elapsed < 0) elapsed = 0;
    *day_s = instance->anchor_day_s + elapsed / 1000;
    *ms = (uint32_t)(elapsed % 1000);
}

/* ------------------------------------------------------------------ canvas */

/* A canvas is drawn over whatever the front window holds and an empty one is
 * drawn as black, so it exists only while the clock does. */
static void tetris_acquire_panels(Tetris* instance) {
    if(instance->canvas) return;

    with_gui(instance->gui, {
        instance->canvas = canvas_alloc(instance->front_widget, TETRIS_PANEL_W, TETRIS_PANEL_H);

        instance->mirror = mirror_card_alloc(instance->back_widget);
        mirror_card_set_header_text(instance->mirror, "TETRIS");
        mirror_card_set_show_footer(instance->mirror, false);
        widget_set_align(mirror_card_get_base(instance->mirror), AlignCenter);
        widget_set_margin(mirror_card_get_base(instance->mirror), 0, 0, 2, 2);
    });
}

static void tetris_release_panels(Tetris* instance) {
    if(!instance->canvas) return;

    with_gui(instance->gui, {
        mirror_card_free(instance->mirror);
        instance->mirror = NULL;
        canvas_free(instance->canvas);
        instance->canvas = NULL;
    });
}

static void tetris_draw(Tetris* instance) {
    if(!instance->canvas) return;

    int64_t day_s;
    uint32_t ms;
    tetris_now(instance, &day_s, &ms);
    tetris_render(
        instance->frame, &instance->state, &instance->look, day_s, ms, instance->clock_set);

    gui_lock(instance->gui);
    canvas_draw_begin(instance->canvas);
    canvas_clear(instance->canvas);

    const uint8_t* p = instance->frame;
    for(int32_t y = 0; y < TETRIS_PANEL_H; y++) {
        for(int32_t x = 0; x < TETRIS_PANEL_W; x++, p += 3) {
            if((p[0] | p[1] | p[2]) == 0U) continue;
            canvas_draw_pixel(
                instance->canvas, x, y, (Color){.r = p[0], .g = p[1], .b = p[2], .a = 255});
        }
    }

    canvas_draw_end(instance->canvas);
    gui_unlock(instance->gui);
}

/* ----------------------------------------------------------------- screens */

static void tetris_dialog_callback(uint8_t index, void* context);
static void tetris_setup_changed(void* context, uint8_t index);

static void tetris_show_menu(Tetris* instance) {
    instance->screen = TetrisScreenMenu;

    tetris_release_panels(instance);
    with_gui(instance->gui, {
        app_shell_chrome_leave_setup(&instance->chrome);
        app_shell_setup_hide(&instance->setup);
        app_shell_start_show(
            &instance->start,
            instance->front_widget,
            instance->back_widget,
            "Tetris",
            APP_SHELL_ICON("Tetris"),
            tetris_dialog_callback,
            instance);
    });
}

static void tetris_show_setup(Tetris* instance) {
    instance->screen = TetrisScreenSetup;

    with_gui(instance->gui, {
        app_shell_chrome_enter_setup(&instance->chrome);
        app_shell_start_hide(&instance->start);
        app_shell_setup_show(
            &instance->setup,
            instance->front_widget,
            instance->back_widget,
            &instance->settings,
            tetris_setup_changed,
            instance);
    });
}

static void tetris_enter_run(Tetris* instance) {
    instance->screen = TetrisScreenRun;

    with_gui(instance->gui, {
        app_shell_chrome_leave_setup(&instance->chrome);
        app_shell_start_hide(&instance->start);
        app_shell_setup_hide(&instance->setup);
    });

    tetris_apply_look(instance);
    tetris_resync(instance);
    /* every start assembles the time from nothing: it is the nicest moment this face has */
    tetris_state_reset(&instance->state);
    tetris_acquire_panels(instance);
    tetris_draw(instance);
    furi_event_loop_timer_start(instance->timer, TETRIS_FRAME_MS);
}

static void tetris_leave_run(Tetris* instance) {
    furi_event_loop_timer_stop(instance->timer);
    tetris_show_menu(instance);
}

/* Posts rather than acts: the Dialog calls this from inside input dispatch,
 * which already holds the GUI lock. */
static void tetris_dialog_callback(uint8_t index, void* context) {
    Tetris* instance = context;

    furi_event_loop_set_custom_event(
        instance->event_loop,
        (index == AppShellChoiceStart) ? TetrisCustomEventStart : TetrisCustomEventSetup);
}

static void tetris_setup_changed(void* context, uint8_t index) {
    UNUSED(index);
    Tetris* instance = context;
    tetris_apply_look(instance);
    tetris_settings_save(instance);
}

static void tetris_timer_callback(void* context) {
    Tetris* instance = context;
    tetris_draw(instance);
}

static void tetris_handle_key(Tetris* instance, InputKey key) {
    switch(instance->screen) {
    case TetrisScreenMenu:
        if(key == InputKeyBack) {
            furi_event_loop_stop(instance->event_loop);
        }
        return;

    case TetrisScreenSetup:
        if(key == InputKeyBack) {
            tetris_show_menu(instance);
        }
        return;

    case TetrisScreenRun:
        if(key == InputKeyBack) {
            tetris_leave_run(instance);
            return;
        }
        /* A tap of the wheel turns the animation on or off; turning it steps
         * the colour either way. Neither is a different time, so the digits
         * stay where they are (switching animation off lands a falling one). */
        if(key == InputKeyOk) {
            AppShellSetting* anim = &instance->setting_items[TetrisSettingAnimation];
            anim->value = (uint8_t)((anim->value + 1U) % anim->count);
        } else if((key == InputKeyUp) || (key == InputKeyDown)) {
            AppShellSetting* colour = &instance->setting_items[TetrisSettingColour];
            colour->value = (key == InputKeyUp) ?
                                (uint8_t)((colour->value + 1U) % colour->count) :
                                (uint8_t)((colour->value + colour->count - 1U) % colour->count);
        } else {
            return;
        }
        tetris_apply_look(instance);
        tetris_settings_save(instance);
        tetris_draw(instance);
        return;
    }
}

/* ---------------------------------------------------------------- plumbing */

static bool tetris_input_callback(const InputEvent* event, void* context) {
    furi_assert(event);
    furi_assert(context);
    Tetris* instance = context;

    bool consumed = false;

    if(event->type == InputTypeShort) {
        if(event->key == InputKeyBack) {
            furi_event_loop_set_custom_event(instance->event_loop, TetrisCustomEventBack);
            consumed = true;
        } else if(
            (event->key == InputKeyUp) || (event->key == InputKeyDown) ||
            (event->key == InputKeyOk) || (event->key == InputKeyStart)) {
            if(furi_message_queue_put(instance->input_queue, &event->key, 0U) != FuriStatusOk) {
                FURI_LOG_W(TAG, "Input queue full; dropping %s", input_get_key_name(event->key));
            }
            consumed = true;
        }
    }

    return consumed;
}

static void tetris_input_queue_callback(FuriEventLoopObject* object, void* context) {
    furi_assert(context);
    Tetris* instance = context;
    furi_assert(object == instance->input_queue);

    InputKey key;
    while(furi_message_queue_get(instance->input_queue, &key, 0U) == FuriStatusOk) {
        tetris_handle_key(instance, key);
    }
}

static void tetris_custom_event_callback(uint32_t events, void* context) {
    furi_assert(context);
    Tetris* instance = context;

    if(events & TetrisCustomEventBack) {
        tetris_handle_key(instance, InputKeyBack);
    }
    if(events & TetrisCustomEventStart) {
        tetris_enter_run(instance);
    }
    if(events & TetrisCustomEventSetup) {
        tetris_show_setup(instance);
    }
}

static bool tetris_signal_callback(uint32_t signal, void* arg, void* context) {
    UNUSED(arg);
    Tetris* instance = context;

    switch(signal) {
    case FuriSignalExit:
        furi_event_loop_stop(instance->event_loop);
        return true;

    default:
        return false;
    }
}

static void tetris_settings_init(Tetris* instance) {
    AppShellSetting* s = instance->setting_items;
    s[TetrisSettingColour] = (AppShellSetting){
        "Colour", tetris_colour_labels, COUNT_OF(tetris_colour_labels), TetrisColourClassic};
    s[TetrisSettingHour24] =
        (AppShellSetting){"24-hour", tetris_hour24_labels, COUNT_OF(tetris_hour24_labels), 1U};
    s[TetrisSettingSpeed] =
        (AppShellSetting){"Speed", tetris_speed_labels, COUNT_OF(tetris_speed_labels), 2U};
    s[TetrisSettingColon] = (AppShellSetting){
        "Colon", tetris_colon_labels, COUNT_OF(tetris_colon_labels), TetrisColonBlink};
    s[TetrisSettingSeconds] = (AppShellSetting){
        "Seconds", tetris_seconds_labels, COUNT_OF(tetris_seconds_labels), TetrisSecondsQuick};
    s[TetrisSettingAnimation] = (AppShellSetting){
        "Animation", tetris_animation_labels, COUNT_OF(tetris_animation_labels), 1U};

    instance->settings.items = instance->setting_items;
    instance->settings.count = TetrisSettingCount;
    instance->settings.index = 0U;
    instance->settings.top = 0U;
}

static Tetris* tetris_alloc(bool skip_menu) {
    Tetris* instance = malloc(sizeof(Tetris));
    memset(instance, 0, sizeof(Tetris));

    instance->frame = malloc(TETRIS_FRAME_BYTES);

    /* The covers are solved once, here, rather than inside a frame. */
    if(!tetris_covers()) {
        FURI_LOG_E(TAG, "A digit could not be tiled; the clock will be blank");
    }

    instance->event_loop = furi_event_loop_alloc();
    instance->input_queue = furi_message_queue_alloc(TETRIS_INPUT_QUEUE_SIZE, sizeof(InputKey));
    instance->gui = furi_record_open(RECORD_GUI);
    instance->time = furi_record_open(RECORD_TIME);

    instance->screen = TetrisScreenMenu;

    tetris_settings_init(instance);
    tetris_settings_load(instance);
    tetris_apply_look(instance);
    tetris_state_reset(&instance->state);

    instance->timer = furi_event_loop_timer_alloc(
        instance->event_loop, tetris_timer_callback, FuriEventLoopTimerTypePeriodic, instance);

    furi_event_loop_subscribe_message_queue(
        instance->event_loop,
        instance->input_queue,
        FuriEventLoopEventIn,
        tetris_input_queue_callback,
        instance);

    furi_thread_set_signal_callback(furi_thread_get_current(), tetris_signal_callback, instance);

    furi_event_loop_set_custom_event_callback(
        instance->event_loop, tetris_custom_event_callback, instance);

    with_gui(instance->gui, {
        GuiLayer* main_layer = gui_get_layer(instance->gui, GuiLayerIdMain);
        gui_layer_add_input_callback(main_layer, tetris_input_callback, instance);

        app_shell_chrome_init(
            &instance->chrome,
            gui_layer_get_root_widget(main_layer, GuiDisplayIdFront),
            gui_layer_get_root_widget(main_layer, GuiDisplayIdBack),
            "TETRIS");
        instance->front_widget = instance->chrome.front_window;
        instance->back_widget = instance->chrome.back_window;
    });

    if(skip_menu) {
        tetris_enter_run(instance);
    } else {
        tetris_show_menu(instance);
    }

    return instance;
}

static void tetris_free(Tetris* instance) {
    furi_thread_set_signal_callback(furi_thread_get_current(), NULL, NULL);

    furi_event_loop_timer_stop(instance->timer);
    tetris_release_panels(instance);

    with_gui(instance->gui, {
        GuiLayer* main_layer = gui_get_layer(instance->gui, GuiLayerIdMain);
        gui_layer_remove_input_callback(main_layer, tetris_input_callback);

        app_shell_start_hide(&instance->start);
        app_shell_setup_hide(&instance->setup);
        app_shell_chrome_free(&instance->chrome);
    });

    furi_record_close(RECORD_TIME);
    furi_record_close(RECORD_GUI);

    furi_event_loop_unsubscribe(instance->event_loop, instance->input_queue);
    furi_message_queue_free(instance->input_queue);
    furi_event_loop_timer_free(instance->timer);
    furi_event_loop_free(instance->event_loop);

    free(instance->frame);
    free(instance);
}

/* "-s" (or "--skip-menu"): straight to the clock. The Apps menu passes it when
 * it resumes the app it remembers. Same flag as the built-in Clock and Nixie. */
static bool tetris_wants_skip_menu(const char* args) {
    if(!args) return false;
    while(*args) {
        while(*args == ' ') args++;
        const char* end = args;
        while(*end && (*end != ' ')) end++;
        const size_t len = (size_t)(end - args);
        if(((len == 2) && (strncmp(args, "-s", 2) == 0)) ||
           ((len == 11) && (strncmp(args, "--skip-menu", 11) == 0))) {
            return true;
        }
        args = end;
    }
    return false;
}

int32_t tetris_app(void* arg) {
    FURI_LOG_I(TAG, "Tetris started");

    Tetris* instance = tetris_alloc(tetris_wants_skip_menu((const char*)arg));
    furi_event_loop_run(instance->event_loop);
    tetris_free(instance);

    FURI_LOG_I(TAG, "Tetris exiting");

    return 0;
}
