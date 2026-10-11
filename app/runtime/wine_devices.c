/* Native PS5 devices -> Wine hardware events and XInput. SPDX-License-Identifier: MIT
 * Interoperability layouts checked against PS5 native input research and AnyPad;
 * no pairing or virtual-device manipulation is needed to read a title's devices. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "cursor_arrow.h"
#ifndef POKEMMOPS5_INPUT_TEST
#include <ps5platform/kernel.h>
#endif

struct touch { uint16_t x, y; uint8_t id, reserved[3]; };
struct pad_data {
    uint32_t buttons;
    uint8_t lx, ly, rx, ry, l2, r2, reserved[2];
    float orientation[4], acceleration[3], angular_velocity[3];
    uint8_t touches, reserved1[3]; uint32_t reserved2;
    struct touch touch[2];
    uint8_t connected, reserved3[3]; uint64_t timestamp;
    uint8_t extension[16], connected_count, reserved4[15];
};
struct keyboard_data {
    uint64_t timestamp; uint8_t intercepted, reserved[7], connected, reserved1[3];
    int32_t length; uint32_t leds, modifiers; uint16_t keys[16]; uint8_t reserved2[32];
};
struct mouse_data {
    uint64_t timestamp; uint8_t connected, reserved[3]; uint32_t buttons;
    int32_t x, y, wheel, tilt; uint8_t reserved1[8];
};
struct gamepad { uint16_t buttons; uint8_t lt, rt; int16_t lx, ly, rx, ry; };
struct gamepad_state { uint32_t packet; struct gamepad pad; };
_Static_assert(sizeof(struct pad_data) == 120 && offsetof(struct pad_data, touch) == 60, "pad ABI");
_Static_assert(sizeof(struct keyboard_data) == 96 && offsetof(struct keyboard_data, keys) == 32, "keyboard ABI");
_Static_assert(sizeof(struct mouse_data) == 40 && offsetof(struct mouse_data, x) == 16, "mouse ABI");
_Static_assert(sizeof(struct gamepad_state) == 16, "XInput ABI");

extern int pokemmops5_input_queue(unsigned, unsigned, unsigned, int, int);
static pthread_mutex_t devices_lock = PTHREAD_MUTEX_INITIALIZER;
static struct gamepad_state gamepad;
static int pad_connected, pad_enabled = 1;
static uint8_t keyboard_held[8][256], key_held[256];
enum { MOUSE_PAD_SOURCE = 8, MOUSE_TAP_SOURCE, MOUSE_SOURCE_COUNT };
static uint32_t mouse_held[MOUSE_SOURCE_COUNT], buttons_held;
static int touch_active, touch_x, touch_y, touch_click;
static uint8_t touch_id;
static unsigned repeat_key;
static uint64_t repeat_at;
static uint64_t pad_records, touch_moves, keyboard_edges, mouse_moves;
static uint64_t stick_moves, stick_rejected;
static int64_t stick_pixels_x, stick_pixels_y;
static uint64_t cursor_activity;
static uint64_t taps_recognized, physical_primary_presses, physical_secondary_presses;
static uint64_t left_down_edges, left_up_edges, dpad_assist_samples;

static void bounded_increment(uint64_t *counter)
{
    if (*counter != UINT64_MAX) (*counter)++;
}
#define CURSOR_SPACE_WIDTH 1920
#define CURSOR_SPACE_HEIGHT 1080
#define CURSOR_SLOT_BYTES (64u * 1024u)
#define CURSOR_IMAGE_SIDE 64u
#define CURSOR_SCALE 2u
struct cursor_point { int x, y; };
static atomic_uint_fast64_t cursor_position_bits =
    ((uint64_t)(CURSOR_SPACE_HEIGHT / 2) << 32) | (CURSOR_SPACE_WIDTH / 2);
#define CURSOR_STICK_DEADZONE 24
#define CURSOR_STICK_SPEED 1400
#define CURSOR_FIXED_ONE (INT64_C(1) << 16)
static uint8_t cursor_rx = 128, cursor_ry = 128;
static int64_t cursor_remainder_x, cursor_remainder_y;
static uint64_t cursor_stick_frame_at;
#define PAD_BUTTON_UP 0x10u
#define PAD_BUTTON_RIGHT 0x20u
#define PAD_BUTTON_DOWN 0x40u
#define PAD_BUTTON_LEFT 0x80u
#define PAD_BUTTON_DPAD (PAD_BUTTON_UP | PAD_BUTTON_RIGHT | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT)
#define PAD_BUTTON_TOUCH 0x100000u
#define PAD_BUTTON_INTERCEPTED 0x80000000u
#define PAD_DPAD_ASSIST_RANGE 24
#define TOUCH_TAP_MAX_MS 200
#define TOUCH_TAP_MAX_EXCURSION 12
#define TAP_CLICK_HOLD_MS 80
#define CURSOR_IDLE_TIMEOUT_MS 3000
enum touch_tap_phase { TOUCH_TAP_CLEAR, TOUCH_TAP_CANDIDATE, TOUCH_TAP_BLOCKED };
static struct {
    enum touch_tap_phase phase;
    uint8_t id;
    uint16_t x, y;
    uint64_t started_at;
} touch_tap;
static uint32_t tap_click_pending;
static int tap_click_down;
static uint64_t tap_click_release_at;

struct cursor_idle_policy {
    uint64_t activity, active_at;
    int initialized;
};

static void cursor_idle_policy_begin(struct cursor_idle_policy *policy,
                                     uint64_t now, uint64_t activity)
{
    policy->activity = activity;
    policy->active_at = now;
    policy->initialized = 1;
}

static int cursor_idle_policy_visible(struct cursor_idle_policy *policy,
                                      uint64_t now, uint64_t activity, unsigned held)
{
    if (!policy->initialized) return 1;
    if (policy->activity != activity)
    {
        policy->activity = activity;
        policy->active_at = now;
    }
    if (held)
    {
        policy->active_at = now;
        return 1;
    }
    return now - policy->active_at < CURSOR_IDLE_TIMEOUT_MS;
}


static struct cursor_point cursor_position(void)
{
    const uint64_t bits = atomic_load_explicit(&cursor_position_bits, memory_order_relaxed);
    return (struct cursor_point){(int)(uint32_t)bits, (int)(bits >> 32)};
}

void pokemmops5_cursor_sync(int x, int y, int width, int height)
{
    if (width <= 1 || height <= 1) return;
    if (x < 0) x = 0;
    else if (x >= width) x = width - 1;
    if (y < 0) y = 0;
    else if (y >= height) y = height - 1;
    x = (int)((int64_t)x * (CURSOR_SPACE_WIDTH - 1) / (width - 1));
    y = (int)((int64_t)y * (CURSOR_SPACE_HEIGHT - 1) / (height - 1));
    atomic_store_explicit(&cursor_position_bits, ((uint64_t)(uint32_t)y << 32) | (uint32_t)x,
                          memory_order_relaxed);
}

/* All native devices share one client-space pointer. Type 3 includes its
 * target plus the original signed raw deltas in code/flags. */
static int cursor_move(int dx, int dy)
{
    if (!dx && !dy) return 0;
    const struct cursor_point current = cursor_position();
    const int64_t wanted_x = (int64_t)current.x + dx, wanted_y = (int64_t)current.y + dy;
    const int target_x = wanted_x < 0 ? 0 : wanted_x >= CURSOR_SPACE_WIDTH ? CURSOR_SPACE_WIDTH - 1 : (int)wanted_x;
    const int target_y = wanted_y < 0 ? 0 : wanted_y >= CURSOR_SPACE_HEIGHT ? CURSOR_SPACE_HEIGHT - 1 : (int)wanted_y;
    if (!pokemmops5_input_queue(3, (unsigned)dx, (unsigned)dy, target_x, target_y)) return -1;
    cursor_activity++;
    return 1;
}

static uint64_t microseconds(void)
{
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

static uint64_t milliseconds(void)
{
    return microseconds() / 1000;
}

static int64_t cursor_stick_velocity(uint8_t value)
{
    const int direction = (int)value - 128;
    const int magnitude = direction < 0 ? -direction : direction;
    if (magnitude <= CURSOR_STICK_DEADZONE) return 0;
    const int distance = magnitude - CURSOR_STICK_DEADZONE;
    const int range = 128 - CURSOR_STICK_DEADZONE;
    const int64_t velocity =
        (int64_t)distance * distance * CURSOR_STICK_SPEED * CURSOR_FIXED_ONE / (range * range);
    return direction < 0 ? -velocity : velocity;
}

static void cursor_stick_frame(uint64_t now)
{
    uint64_t elapsed = cursor_stick_frame_at ? now - cursor_stick_frame_at : 0;
    if (elapsed > 100000) elapsed = 100000;
    cursor_stick_frame_at = now;

    const int64_t elapsed_us = (int64_t)elapsed;
    int64_t moved_x = cursor_remainder_x + cursor_stick_velocity(cursor_rx) * elapsed_us / 1000000;
    int64_t moved_y = cursor_remainder_y + cursor_stick_velocity(cursor_ry) * elapsed_us / 1000000;
    const int dx = (int)(moved_x / CURSOR_FIXED_ONE);
    const int dy = (int)(moved_y / CURSOR_FIXED_ONE);
    cursor_remainder_x = moved_x - (int64_t)dx * CURSOR_FIXED_ONE;
    cursor_remainder_y = moved_y - (int64_t)dy * CURSOR_FIXED_ONE;
    const int result = cursor_move(dx, dy);
    if (result > 0)
    {
        stick_moves++;
        stick_pixels_x += dx;
        stick_pixels_y += dy;
    }
    else if (result < 0) stick_rejected++;
}

static unsigned key_vk(unsigned usage)
{
    static const uint8_t punctuation[] = {0x0d,0x1b,0x08,0x09,0x20,0xbd,0xbb,0xdb,0xdd,0xdc,0xdc,0xba,0xde,0xc0,0xbc,0xbe,0xbf,0x14};
    static const uint8_t navigation[] = {0x2c,0x91,0x13,0x2d,0x24,0x21,0x2e,0x23,0x22,0x27,0x25,0x28,0x26,0x90,0x6f,0x6a,0x6d,0x6b,0x0d};
    static const uint8_t modifiers[] = {0xa2,0xa0,0xa4,0x5b,0xa3,0xa1,0xa5,0x5c};
    if (usage >= 4 && usage <= 29) return 'A' + usage - 4;
    if (usage >= 30 && usage <= 38) return '1' + usage - 30;
    if (usage == 39) return '0';
    if (usage >= 40 && usage <= 57) return punctuation[usage-40];
    if (usage >= 58 && usage <= 69) return 0x70 + usage - 58;
    if (usage >= 70 && usage <= 88) return navigation[usage-70];
    if (usage >= 89 && usage <= 97) return 0x61 + usage - 89;
    if (usage == 98) return 0x60;
    if (usage == 99) return 0x6e;
    if (usage == 100) return 0xe2;
    if (usage == 101) return 0x5d;
    if (usage >= 104 && usage <= 115) return 0x7c + usage - 104;
    if (usage >= 224 && usage <= 231) return modifiers[usage-224];
    return 0;
}
static unsigned key_flags(unsigned vk)
{
    return vk == 0xa3 || vk == 0xa5 || vk == 0x5b || vk == 0x5c ||
           (vk >= 0x21 && vk <= 0x2e) || vk == 0x6f ? 1 : 0;
}
static void keyboard_sample(unsigned device, const struct keyboard_data *sample)
{
    uint8_t held[256] = {0};
    if (sample->connected && !sample->intercepted)
    {
        /* A rollover/error report is not a release of all held keys. */
        for (unsigned i = 0; i < 16; i++) if (sample->keys[i] && sample->keys[i] < 4) return;
        for (unsigned i = 0; i < 8; i++) if (sample->modifiers & (1u << i)) held[key_vk(224+i)] = 1;
        for (unsigned i = 0; i < 16; i++) { unsigned vk = key_vk(sample->keys[i]); if (vk) held[vk] = 1; }
    }
    memcpy(keyboard_held[device], held, sizeof(held));
    /* Modifiers precede ordinary keys on both edges, keeping chords intact. */
    for (unsigned pass = 0; pass < 2; pass++) for (unsigned vk = 1; vk < 256; vk++)
    {
        int modifier = (vk >= 0xa0 && vk <= 0xa5) || vk == 0x5b || vk == 0x5c;
        if (modifier != (pass == 0)) continue;
        unsigned down = 0;
        for (unsigned i = 0; i < 8; i++) down |= keyboard_held[i][vk];
        if (down != key_held[vk] && pokemmops5_input_queue(1, vk, key_flags(vk) | (down ? 0 : 2), 0, 0))
        {
            key_held[vk] = down;
            keyboard_edges++;
            if (!modifier && down) { repeat_key = vk; repeat_at = milliseconds() + 500; }
            else if (!down && repeat_key == vk) repeat_key = 0;
        }
    }
}
static unsigned mouse_buttons_wanted(void)
{
    unsigned all = 0;
    for (unsigned i = 0; i < MOUSE_SOURCE_COUNT; i++) all |= mouse_held[i];
    return all;
}


static unsigned mouse_buttons(unsigned device, unsigned held)
{
    static const unsigned down[] = {2,8,32,128,128}, up[] = {4,16,64,256,256};
    mouse_held[device] = held & 31;
    const unsigned all = mouse_buttons_wanted();
    for (unsigned i = 0; i < 5; i++) if ((all ^ buttons_held) & (1u << i))
    {
        const struct cursor_point position = cursor_position();
        if (pokemmops5_input_queue(4, i < 3 ? 0 : i-2,
                                  all & (1u << i) ? down[i] : up[i], position.x, position.y))
        {
            buttons_held = (buttons_held & ~(1u << i)) | (all & (1u << i));
            cursor_activity++;
            if (!i)
                bounded_increment(all & 1 ? &left_down_edges : &left_up_edges);
        }
    }
    return all;
}
static void mouse_sample(unsigned device, const struct mouse_data *sample)
{
    int usable = sample->connected && !(sample->buttons & 0x80000000u);
    if (usable && cursor_move(sample->x, sample->y) > 0) mouse_moves++;
    mouse_buttons(device, usable ? sample->buttons : 0);
    if (usable && (sample->wheel || sample->tilt))
    {
        const struct cursor_point position = cursor_position();
        if (sample->wheel &&
            pokemmops5_input_queue(4, (uint32_t)(sample->wheel * 120), 0x800,
                                   position.x, position.y))
            cursor_activity++;
        if (sample->tilt &&
            pokemmops5_input_queue(4, (uint32_t)(sample->tilt * 120), 0x1000,
                                   position.x, position.y))
            cursor_activity++;
    }
}
static int16_t axis(unsigned value, int invert)
{
    int n = (int)value - 128;
    /* Exact neutral, full signed range, native Y is down-positive. */
    if (n >= -1 && n <= 1) return 0;
    int result = n < 0 ? n * 256 : n * 32767 / 127;
    if (invert) result = result == -32768 ? 32767 : -result;
    return result;
}
/* --- on-screen keyboard: shortcut gate, system dialog, typing (wine_osk.c) --- */
#include "wine_osk.c"

static void tap_click_request(void)
{
    if (tap_click_pending != UINT32_MAX) tap_click_pending++;
}
static void tap_click_cancel_pending(void)
{
    tap_click_pending = 0;
    if (tap_click_down || mouse_held[MOUSE_TAP_SOURCE])
    {
        tap_click_down = 1;
        tap_click_release_at = 0;
    }
}


/* A completed tap stays down for at least 80 ms unless interrupted. Queue
 * refusal holds the current edge, and another source's left is never released. */
static void tap_click_tick(void)
{
    if (tap_click_down)
    {
        if (tap_click_release_at && OSK_NOW() < tap_click_release_at) return;
        const unsigned all = mouse_buttons(MOUSE_TAP_SOURCE, 0);
        if (!((all ^ buttons_held) & 1))
        {
            tap_click_down = 0;
            tap_click_release_at = 0;
        }
        return;
    }
    if (!tap_click_pending) return;
    const unsigned all = mouse_buttons(MOUSE_TAP_SOURCE, 1);
    if (!((all ^ buttons_held) & 1))
    {
        tap_click_pending--;
        tap_click_down = 1;
        tap_click_release_at = OSK_NOW() + TAP_CLICK_HOLD_MS;
    }
}

static void touch_tap_sample(const struct pad_data *sample, int usable, int owned)
{
    if (!sample->connected)
    {
        tap_click_cancel_pending();
        touch_tap.phase = TOUCH_TAP_BLOCKED;
        return;
    }
    if (!usable || owned)
    {
        tap_click_cancel_pending();
        touch_tap.phase = sample->touches ? TOUCH_TAP_BLOCKED : TOUCH_TAP_CLEAR;
        return;
    }
    if ((sample->buttons & PAD_BUTTON_TOUCH) || sample->touches > 1)
    {
        touch_tap.phase = sample->touches ? TOUCH_TAP_BLOCKED : TOUCH_TAP_CLEAR;
        return;
    }
    if (!sample->touches)
    {
        if (touch_tap.phase == TOUCH_TAP_CANDIDATE &&
            OSK_NOW() - touch_tap.started_at <= TOUCH_TAP_MAX_MS)
        {
            bounded_increment(&taps_recognized);
            tap_click_request();
        }
        touch_tap.phase = TOUCH_TAP_CLEAR;
        return;
    }

    const struct touch *t = &sample->touch[0];
    if (touch_tap.phase == TOUCH_TAP_CLEAR)
    {
        touch_tap.phase = TOUCH_TAP_CANDIDATE;
        touch_tap.id = t->id;
        touch_tap.x = t->x;
        touch_tap.y = t->y;
        touch_tap.started_at = OSK_NOW();
        return;
    }
    if (touch_tap.phase != TOUCH_TAP_CANDIDATE) return;
    const int dx = (int)t->x - touch_tap.x, dy = (int)t->y - touch_tap.y;
    if (t->id != touch_tap.id || dx < -TOUCH_TAP_MAX_EXCURSION ||
        dx > TOUCH_TAP_MAX_EXCURSION || dy < -TOUCH_TAP_MAX_EXCURSION ||
        dy > TOUCH_TAP_MAX_EXCURSION)
        touch_tap.phase = TOUCH_TAP_BLOCKED;
}
static void pad_sample(const struct pad_data *sample)
{
    static const uint32_t native[] = {PAD_BUTTON_UP,PAD_BUTTON_DOWN,PAD_BUTTON_LEFT,PAD_BUTTON_RIGHT,
                                     8,1,2,4,0x400,0x800,0x4000,0x2000,0x8000,0x1000};
    static const uint16_t xbox[] = {1,2,4,8,0x10,0x20,0x40,0x80,0x100,0x200,0x1000,0x2000,0x4000,0x8000};
    const uint32_t raw_buttons = sample->buttons;
    const int raw_usable = sample->connected && !(raw_buttons & PAD_BUTTON_INTERCEPTED);
    int usable = raw_usable;
    if (usable) pad_records++;
    /* --- on-screen keyboard: a pad it owns is as one the system intercepted,
     * except that the game keeps it: connected and neutral. Otherwise the game
     * sees the record less the shortcut buttons being held back. --- */
    struct pad_data gated = *sample;
    int owned = osk_pad(usable, &gated.buttons, &gated.l2, &gated.r2);
    int connected = usable || (owned && sample->connected);
    sample = &gated;
    if (owned) usable = 0;
    /* --- end on-screen keyboard --- */
    cursor_rx = usable ? sample->rx : 128;
    cursor_ry = usable ? sample->ry : 128;
    struct gamepad next = {0};
    if (usable && pad_enabled)
    {
        for (unsigned i = 0; i < 14; i++)
            if (sample->buttons & native[i]) next.buttons |= xbox[i];
        next.lt = sample->l2; next.rt = sample->r2;
        next.lx = axis(sample->lx, 0); next.ly = axis(sample->ly, 1);
        const int physical_lx = (int)sample->lx - 128;
        const int physical_ly = (int)sample->ly - 128;
        if ((sample->buttons & PAD_BUTTON_DPAD) &&
            physical_lx >= -PAD_DPAD_ASSIST_RANGE && physical_lx <= PAD_DPAD_ASSIST_RANGE &&
            physical_ly >= -PAD_DPAD_ASSIST_RANGE && physical_ly <= PAD_DPAD_ASSIST_RANGE)
        {
            bounded_increment(&dpad_assist_samples);
            const int horizontal = !!(sample->buttons & PAD_BUTTON_RIGHT) -
                                   !!(sample->buttons & PAD_BUTTON_LEFT);
            const int vertical = !!(sample->buttons & PAD_BUTTON_UP) -
                                 !!(sample->buttons & PAD_BUTTON_DOWN);
            next.lx = horizontal < 0 ? -32768 : horizontal > 0 ? 32767 : 0;
            next.ly = vertical < 0 ? -32768 : vertical > 0 ? 32767 : 0;
        }
        /* The right stick belongs exclusively to the native pointer. */
    }
    if (pad_connected != connected || memcmp(&next, &gamepad.pad, sizeof(next)))
    { gamepad.pad = next; if (!++gamepad.packet) ++gamepad.packet; }
    pad_connected = connected;
    if (usable && sample->touches)
    {
        const struct touch *t = &sample->touch[0];
        /* Lifting/replacing a finger rebases without moving the cursor. */
        if (touch_active && touch_id == t->id && (t->x != touch_x || t->y != touch_y))
        {
            if (cursor_move(((int)t->x-touch_x)*2, ((int)t->y-touch_y)*2) > 0) touch_moves++;
        }
        touch_active = 1; touch_id = t->id; touch_x = t->x; touch_y = t->y;
    }
    else touch_active = 0;
    touch_tap_sample(sample, raw_usable, owned);
    if (usable && (sample->buttons & PAD_BUTTON_TOUCH))
    {
        /* Latch the choice until release. Two fingers request secondary;
         * a one-finger or no-finger mechanical press is always primary. */
        if (!touch_click)
        {
            touch_click = sample->touches > 1 ? 2 : 1;
            bounded_increment(touch_click == 1 ? &physical_primary_presses :
                                                &physical_secondary_presses);
        }
    }
    else touch_click = 0;
    mouse_buttons(MOUSE_PAD_SOURCE, touch_click);
}


int pokemmops5_gamepad_state(unsigned index, void *state)
{
    if (index >= 4 || !state) return 160;
    pthread_mutex_lock(&devices_lock);
    int result = index == 0 && pad_connected ? 0 : 1167;
    if (!result) memcpy(state, &gamepad, sizeof(gamepad));
    static int reported;
    if (!result && !reported) { fprintf(stderr, "[PokeMMO PS5 input] Windows XInput reached native controller\n"); reported = 1; }
    pthread_mutex_unlock(&devices_lock);
    return result;
}

#ifndef POKEMMOPS5_INPUT_TEST
int wsi_videoout_cursor_set(const uint32_t *image, int hotspot_x, int hotspot_y);
int wsi_videoout_cursor_position(int x, int y, int space_width, int space_height);
int wsi_videoout_cursor_disable(void);

static uint32_t *cursor_image;
static struct cursor_idle_policy cursor_idle;
static int cursor_wsi_visible;

static void cursor_build_image(void)
{
    memset(cursor_image, 0, CURSOR_SLOT_BYTES);
    for (unsigned y = 0; y < POKEMMOPS5_CURSOR_HEIGHT; y++)
        for (unsigned x = 0; x < POKEMMOPS5_CURSOR_WIDTH; x++)
        {
            const char pixel = pokemmops5_cursor_arrow[y][x];
            if (pixel == ' ') continue;
            const uint32_t color = pixel == '#' ? 0xff000000u : 0xffffffffu;
            for (unsigned yy = 0; yy < CURSOR_SCALE; yy++)
                for (unsigned xx = 0; xx < CURSOR_SCALE; xx++)
                    cursor_image[(y * CURSOR_SCALE + yy) * CURSOR_IMAGE_SIDE + x * CURSOR_SCALE + xx] = color;
        }
}

static void cursor_flush(void)
{
    for (size_t i = 0; i < CURSOR_SLOT_BYTES; i += 64)
        __asm__ volatile("clflush (%0)" : : "r"((uint8_t *)cursor_image + i) : "memory");
    __asm__ volatile("mfence" ::: "memory");
}

static int cursor_allocate(void)
{
    int64_t physical = -1;
    int result = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(),
        CURSOR_SLOT_BYTES, CURSOR_SLOT_BYTES, PS5_KERNEL_DIRECT_TYPE_CPU, &physical);
    void *mapping = NULL;
    if (!result)
        result = sceKernelMapDirectMemory(&mapping, CURSOR_SLOT_BYTES,
            PS5_KERNEL_PROT_CPU_READ | PS5_KERNEL_PROT_CPU_WRITE |
            PS5_KERNEL_PROT_GPU_READ | PS5_KERNEL_PROT_GPU_WRITE,
            0, physical, CURSOR_SLOT_BYTES);
    if (result)
    {
        if (physical >= 0) sceKernelReleaseDirectMemory(physical, CURSOR_SLOT_BYTES);
        fprintf(stderr, "[PokeMMO PS5 input] cursor direct memory failed: %#x\n", (unsigned)result);
        return result;
    }
    cursor_image = mapping;
    cursor_build_image();
    cursor_flush();
    fprintf(stderr, "[PokeMMO PS5 input] native cursor image=%p bytes=%u scale=%u\n",
            cursor_image, CURSOR_SLOT_BYTES, CURSOR_SCALE);
    return 0;
}

static void cursor_update(void)
{
    static int visible_logged;
    if (!cursor_image) return;

    const uint64_t now = milliseconds();
    if (!cursor_idle_policy_visible(&cursor_idle, now, cursor_activity,
                                    buttons_held | mouse_buttons_wanted()))
    {
        if (cursor_wsi_visible && !wsi_videoout_cursor_disable()) cursor_wsi_visible = 0;
        return;
    }
    if (wsi_videoout_cursor_set(cursor_image, 0, 0)) return;
    const struct cursor_point position = cursor_position();
    if (wsi_videoout_cursor_position(position.x, position.y,
                                     CURSOR_SPACE_WIDTH, CURSOR_SPACE_HEIGHT))
        return;
    cursor_wsi_visible = 1;
    if (!cursor_idle.initialized)
        cursor_idle_policy_begin(&cursor_idle, now, cursor_activity);
    if (!visible_logged)
    {
        fprintf(stderr, "[PokeMMO PS5 input] native cursor visible in %dx%d pointer space\n",
                CURSOR_SPACE_WIDTH, CURSOR_SPACE_HEIGHT);
        visible_logged = 1;
    }
}

int sceUserServiceInitialize(const void *);
int sceUserServiceGetInitialUser(int32_t *);
int scePadInit(void);
int scePadOpen(int32_t, int32_t, int32_t, const void *);
int scePadRead(int32_t, void *, int32_t);
int scePadSetVibration(int32_t, const void *);
int scePadSetVibrationMode(int32_t, int32_t);
int sceSysmoduleLoadModule(uint16_t);
int sceKeyboardInit(void);
int sceKeyboardOpen(int32_t, int32_t, int32_t, const void *);
int sceKeyboardRead(int32_t, void *, int32_t);
int sceMouseInit(void);
int sceMouseOpen(int32_t, int32_t, int32_t, const void *);
int sceMouseRead(int32_t, void *, int32_t);
static int pad_handle = -1;
struct peripheral {
    int (*init)(void);
    int (*open)(int32_t, int32_t, int32_t, const void *);
    int (*read)(int32_t, void *, int32_t);
    int handles[8];
};
static struct peripheral keyboard, mouse;
/* Keep calls through the native PLT. Function-address imports in the converted
 * executable can remain null even when an ordinary linked call resolves. */
static int keyboard_init(void) { return sceKeyboardInit(); }
static int keyboard_open(int32_t user, int32_t type, int32_t index, const void *p) { return sceKeyboardOpen(user,type,index,p); }
static int keyboard_read(int32_t handle, void *p, int32_t n) { return sceKeyboardRead(handle,p,n); }
static int mouse_init(void) { return sceMouseInit(); }
static int mouse_open(int32_t user, int32_t type, int32_t index, const void *p) { return sceMouseOpen(user,type,index,p); }
static int mouse_read(int32_t handle, void *p, int32_t n) { return sceMouseRead(handle,p,n); }

static void peripheral_open(struct peripheral *p, uint16_t id, const char *prefix, int32_t user)
{
    for (unsigned i = 0; i < 8; i++) p->handles[i] = -1;
    int loaded = sceSysmoduleLoadModule(id);
    /* The native title imports system modules through its own loader, which
     * resolves their entries before main. General dlsym is unavailable here. */
    if (!p->init || !p->open || !p->read) { p->read = NULL; return; }
    int init = p->init();
    unsigned opened = 0;
    for (unsigned i = 0; i < 8; i++) { p->handles[i] = p->open(user, 0, i, NULL); if (p->handles[i] >= 0) opened++; }
    fprintf(stderr, "[PokeMMO PS5 input] %s module=%#x init=%#x open handles=%u\n", prefix, loaded, init, opened);
}
static void *devices_thread(void *unused)
{
    (void)unused;
    uint64_t report_at = milliseconds() + 10000;
    int diagnostic = !!getenv("POKEMMOPS5_INPUT_DIAGNOSTICS");
    for (;;)
    {
        pthread_mutex_lock(&devices_lock);
        if (pad_handle >= 0)
        {
            struct pad_data samples[64]; int n = scePadRead(pad_handle, samples, 64);
            for (int i = 0; i < n && i < 64; i++) pad_sample(&samples[i]);
            if (n < 0) { struct pad_data neutral = {0}; pad_sample(&neutral); }
        }
        for (unsigned device = 0; device < 8; device++)
        {
            if (keyboard.read && keyboard.handles[device] >= 0)
            {
                struct keyboard_data samples[16]; int n = keyboard.read(keyboard.handles[device], samples, 16);
                for (int i = 0; i < n && i < 16; i++) keyboard_sample(device, &samples[i]);
                if (n < 0) { struct keyboard_data neutral = {0}; keyboard_sample(device, &neutral); }
            }
            if (mouse.read && mouse.handles[device] >= 0)
            {
                struct mouse_data samples[64]; int n = mouse.read(mouse.handles[device], samples, 64);
                for (int i = 0; i < n && i < 64; i++) mouse_sample(device, &samples[i]);
                if (n < 0) { struct mouse_data neutral = {0}; mouse_sample(device, &neutral); }
            }
        }
        tap_click_tick();
        cursor_stick_frame(microseconds());
        if (repeat_key && milliseconds() >= repeat_at)
        { pokemmops5_input_queue(1, repeat_key, key_flags(repeat_key), 0, 0); repeat_at = milliseconds() + 33; }
        if (diagnostic && milliseconds() >= report_at)
        {
            const struct cursor_point position = cursor_position();
            fprintf(stderr, "[PokeMMO PS5 input] device counts: pad=%llu touch-moves=%llu key-edges=%llu "
                    "mouse-moves=%llu stick-moves=%llu rejected=%llu stick-pixels=%lld,%lld "
                    "taps=%llu physical-primary=%llu physical-secondary=%llu "
                    "left-down=%llu left-up=%llu dpad-assist=%llu "
                    "axes=%u,%u cursor=%d,%d connected=%d\n",
                (unsigned long long)pad_records, (unsigned long long)touch_moves,
                (unsigned long long)keyboard_edges, (unsigned long long)mouse_moves,
                (unsigned long long)stick_moves, (unsigned long long)stick_rejected,
                (long long)stick_pixels_x, (long long)stick_pixels_y,
                (unsigned long long)taps_recognized,
                (unsigned long long)physical_primary_presses,
                (unsigned long long)physical_secondary_presses,
                (unsigned long long)left_down_edges, (unsigned long long)left_up_edges,
                (unsigned long long)dpad_assist_samples,
                cursor_rx, cursor_ry, position.x, position.y, pad_connected);
            report_at = milliseconds() + 10000;
        }
        pthread_mutex_unlock(&devices_lock);
        cursor_update();
        osk_tick();   /* on-screen keyboard: the dialog and the typing, outside the lock */
        usleep(4000);
    }
    return NULL;
}
void pokemmops5_gamepad_enable(int enabled)
{
    pthread_mutex_lock(&devices_lock); pad_enabled = !!enabled;
    if (!enabled && pad_handle >= 0) { uint8_t zero[2] = {0}; scePadSetVibration(pad_handle, zero); }
    pthread_mutex_unlock(&devices_lock);
}
int pokemmops5_gamepad_vibrate(unsigned index, unsigned left, unsigned right)
{
    pthread_mutex_lock(&devices_lock);
    int result = index == 0 && pad_connected ? 0 : 1167;
    if (!result && pad_enabled) { uint8_t motors[2] = {left >> 8, right >> 8}; if (scePadSetVibration(pad_handle, motors) < 0) result = 50; }
    pthread_mutex_unlock(&devices_lock);
    return result;
}
void pokemmops5_devices_start(void)
{
    cursor_rx = cursor_ry = 128;
    cursor_remainder_x = cursor_remainder_y = 0;
    cursor_stick_frame_at = 0;
    cursor_activity = 0;
    touch_tap.phase = TOUCH_TAP_CLEAR;
    tap_click_pending = tap_click_down = 0;
    tap_click_release_at = 0;
    taps_recognized = physical_primary_presses = physical_secondary_presses = 0;
    left_down_edges = left_up_edges = dpad_assist_samples = 0;
    memset(mouse_held, 0, sizeof(mouse_held));
    buttons_held = 0;
    memset(&cursor_idle, 0, sizeof(cursor_idle));
    cursor_wsi_visible = 0;

    int32_t user = -1;
    sceUserServiceInitialize(NULL);
    if (sceUserServiceGetInitialUser(&user) < 0) { fprintf(stderr, "[PokeMMO PS5 input] no signed-in console user\n"); return; }
    int init = scePadInit();
    for (int i = 0; i < 10 && pad_handle < 0; i++) { pad_handle = scePadOpen(user, 0, 0, NULL); if (pad_handle < 0) usleep(100000); }
    fprintf(stderr, "[PokeMMO PS5 input] pad init=%#x handle=%#x\n", init, pad_handle);
    fprintf(stderr, "[PokeMMO PS5 input] left stick/D-pad/Cross use XInput; right stick uses native cursor\n");
    if (pad_handle >= 0) scePadSetVibrationMode(pad_handle, 2);
    keyboard.init = keyboard_init; keyboard.open = keyboard_open; keyboard.read = keyboard_read;
    mouse.init = mouse_init; mouse.open = mouse_open; mouse.read = mouse_read;
    peripheral_open(&keyboard, 0x0106, "sceKeyboard", user);
    peripheral_open(&mouse, 0x00a9, "sceMouse", user);
    osk_start(user);   /* on-screen keyboard: settings, sysmodule 0x0096 */
    cursor_allocate();
    pthread_t thread;
    if (!pthread_create(&thread, NULL, devices_thread, NULL)) pthread_detach(thread);
    else fprintf(stderr, "[PokeMMO PS5 input] device thread unavailable\n");
}
#endif
