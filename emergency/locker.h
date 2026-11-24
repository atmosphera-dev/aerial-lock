#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include "../native/auth-client.h"
#include "../native/auth-protocol.h"

struct secret {
    char draft[PAM_MAX_RESP_SIZE];
    char scratch[PAM_MAX_RESP_SIZE];
    size_t length, scalars;
};
struct picture { size_t dots; bool rtl, busy, pulse, error; };
struct box { int x, y, width, height, slot_x, slot_size, dot_start, dot_end; };
struct buffer {
    struct wl_buffer *proxy;
    void *pixels;
    size_t size;
    int width, height;
    bool busy;
};
struct output {
    struct output *next;
    struct locker *app;
    uint32_t name, width, height, serial;
    int scale;
    bool configured, pending_ack, dirty, ready;
    struct wl_output *proxy;
    struct wl_surface *surface;
    struct ext_session_lock_surface_v1 *role;
    struct buffer buffers[2];
};
struct seat {
    struct seat *next;
    struct locker *app;
    uint32_t name, repeat_key;
    int rate, delay;
    int64_t repeat_at;
    bool focused;
    struct wl_surface *focus_surface;
    struct wl_seat *proxy;
    struct wl_keyboard *keyboard;
    struct xkb_keymap *keymap;
    struct xkb_state *state;
    struct xkb_compose_state *compose;
};
struct locker {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct ext_session_lock_manager_v1 *manager;
    struct ext_session_lock_v1 *lock;
    struct wl_callback *sync;
    struct output *outputs;
    struct seat *seats;
    struct xkb_context *xkb;
    struct xkb_compose_table *compose_table;
    struct secret *secret;
    struct auth_client *auth;
    const char *service, *user;
    int watchdog, control, result;
    bool ignore_leading_spaces;
    size_t report_offset, command_size;
    char command[8];
    int64_t auth_deadline, error_until, release_deadline, pulse_at;
    bool locked, releasing, done, busy, prompted, retiring;
    bool rtl, pulse, memory_locked, control_connecting;
};

int64_t monotonic_ms(void);
void lock_log(const char *reason);
void locker_error(struct locker *, const char *reason);
void locker_changed(struct locker *);
void locker_submit(struct locker *);
void locker_cancel(struct locker *);
void locker_release(struct locker *, const char *reason);
void locker_stop(struct locker *, int result, const char *reason);
void input_reset(struct seat *);
void input_reset_all(struct locker *);
void input_seat_create(struct locker *, uint32_t name, uint32_t version);
void input_seat_destroy(struct seat *);
void input_repeat(struct locker *, int64_t now);
void input_key(struct seat *, uint32_t key, bool repeat);
bool secret_append(struct secret *, const char *, size_t, bool ignore_leading_spaces);
void secret_clear(struct secret *);
void secret_backspace(struct secret *);
bool secret_utf8(const char *, size_t, size_t *scalars);
bool emergency_dimensions(uint32_t, uint32_t, int, int *, int *, size_t *);
struct box emergency_box(int width, int height, int scale, bool rtl);
void emergency_draw(uint32_t *pixels, int width, int height, int scale, const struct picture *);
bool wayland_start(struct locker *);
void wayland_redraw(struct locker *);
void wayland_finish(struct locker *);
