#define _GNU_SOURCE
#include "locker.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

bool secret_utf8(const char *text, size_t size, size_t *count) {
    size_t n = 0;
    for (size_t i = 0; i < size;) {
        unsigned char c = (unsigned char)text[i++];
        uint32_t cp; unsigned need;
        if (c > 0 && c < 0x80) { cp=c; need=0; }
        else if (c >= 0xc2 && c <= 0xdf) { cp=c&31u; need=1; }
        else if (c >= 0xe0 && c <= 0xef) { cp=c&15u; need=2; }
        else if (c >= 0xf0 && c <= 0xf4) { cp=c&7u; need=3; }
        else return false;
        if (size-i < need) return false;
        for (unsigned j=0;j<need;++j) {
            c=(unsigned char)text[i++];
            if ((c&0xc0u)!=0x80u) return false;
            cp=(cp<<6)|(c&63u);
        }
        if ((need==1 && cp<0x80) || (need==2 && cp<0x800) ||
            (need==3 && cp<0x10000) || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return false;
        ++n;
    }
    if (count) *count=n;
    return true;
}
void secret_clear(struct secret *s) { auth_wipe(s, sizeof(*s)); }
bool secret_append(struct secret *s, const char *text, size_t size, bool ignore_leading_spaces) {
    if (ignore_leading_spaces && !s->length) {
        while (size && *text==' ') { ++text; --size; }
        if (!size) return true;
    }
    size_t n;
    if (size > sizeof(s->draft)-1-s->length || !secret_utf8(text,size,&n)) return false;
    memcpy(s->draft+s->length,text,size); s->length+=size; s->scalars+=n;
    s->draft[s->length]=0;
    return true;
}
void secret_backspace(struct secret *s) {
    if (!s->length) return;
    size_t end=s->length;
    do { --s->length; } while (s->length && ((unsigned char)s->draft[s->length]&0xc0u)==0x80u);
    auth_wipe(s->draft+s->length,end-s->length); --s->scalars;
}
void input_reset(struct seat *s) {
    s->repeat_at=0;
    if (s->compose) xkb_compose_state_reset(s->compose);
}
void input_reset_all(struct locker *app) {
    for (struct seat *s=app->seats;s;s=s->next) input_reset(s);
}

void input_key(struct seat *s, uint32_t key, bool repeat) {
    struct locker *app=s->app;
    if (!app->locked || app->releasing || app->done || !s->focused || !s->state || key>UINT32_MAX-8) return;
    xkb_keysym_t sym=xkb_state_key_get_one_sym(s->state,key+8);
    if (sym==XKB_KEY_Escape && !repeat) { locker_cancel(app); return; }
    if (app->busy) return;
    bool ctrl=xkb_state_mod_name_is_active(s->state,XKB_MOD_NAME_CTRL,XKB_STATE_MODS_EFFECTIVE)>0;
    if ((sym==XKB_KEY_u || sym==XKB_KEY_U) && ctrl && !repeat) {
        secret_clear(app->secret); input_reset_all(app); locker_changed(app); return;
    }
    if (sym==XKB_KEY_Return || sym==XKB_KEY_KP_Enter) {
        if (!repeat) locker_submit(app);
        return;
    }
    bool repeatable=false;
    if (sym==XKB_KEY_BackSpace) {
        secret_backspace(app->secret); locker_changed(app); repeatable=true;
        if (s->compose) xkb_compose_state_reset(s->compose);
    } else if (!ctrl) {
        char *text=app->secret->scratch;
        int n=0;
        if (s->compose && !repeat) {
            xkb_compose_state_feed(s->compose,sym);
            enum xkb_compose_status status=xkb_compose_state_get_status(s->compose);
            if (status==XKB_COMPOSE_COMPOSING) return;
            if (status==XKB_COMPOSE_CANCELLED) { xkb_compose_state_reset(s->compose); return; }
            if (status==XKB_COMPOSE_COMPOSED) {
                n=xkb_compose_state_get_utf8(s->compose,text,sizeof(app->secret->scratch));
                xkb_compose_state_reset(s->compose);
            } else n=xkb_state_key_get_utf8(s->state,key+8,text,sizeof(app->secret->scratch));
        } else n=xkb_state_key_get_utf8(s->state,key+8,text,sizeof(app->secret->scratch));
        if (n>0) {
            /* Control characters are commands, not password text. */
            if ((unsigned char)text[0]>=0x20 && (unsigned char)text[0]!=0x7f) {
                if ((size_t)n>=sizeof(app->secret->scratch) || !secret_append(app->secret,text,(size_t)n,app->ignore_leading_spaces))
                    locker_error(app,"input-overflow");
                locker_changed(app); repeatable=true;
            }
        }
        auth_wipe(text,sizeof(app->secret->scratch));
    }
    if (!repeat && repeatable && s->rate>0 && xkb_keymap_key_repeats(s->keymap,key+8)) {
        s->repeat_key=key; s->repeat_at=monotonic_ms()+s->delay;
    }
}

void input_repeat(struct locker *app, int64_t now) {
    for (struct seat *s=app->seats;s;s=s->next) if (s->repeat_at && now>=s->repeat_at) {
        input_key(s,s->repeat_key,true);
        if (s->repeat_at && s->rate>0) s->repeat_at=now+(s->rate>1000 ? 1 : 1000/s->rate);
    }
}
static void keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size) {
    (void)keyboard;
    struct seat *s=data; struct stat st;
    input_reset(s);
    xkb_state_unref(s->state); s->state=NULL;
    xkb_keymap_unref(s->keymap); s->keymap=NULL;
    if (format==WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 && size>1 && !fstat(fd,&st) && st.st_size>=(off_t)size) {
        char *map=mmap(NULL,size,PROT_READ,MAP_PRIVATE,fd,0);
        if (map!=MAP_FAILED) {
            /* wl_keyboard supplies a byte length, not a C-string contract.
             * Smithay sends exactly the text bytes; wlroots includes a NUL. */
            size_t length=size-(map[size-1]==0 ? 1u : 0u);
            s->keymap=xkb_keymap_new_from_buffer(s->app->xkb,map,length,
                XKB_KEYMAP_FORMAT_TEXT_V1,XKB_KEYMAP_COMPILE_NO_FLAGS);
            munmap(map,size);
        }
    }
    close(fd);
    if (s->keymap) s->state=xkb_state_new(s->keymap);
    if (!s->state) lock_log("keymap-unavailable");
}
static void enter(void *data, struct wl_keyboard *k, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
    (void)k;(void)serial;(void)keys;
    struct seat *s=data; input_reset(s); s->focused=false; s->focus_surface=surface;
    for (struct output *o=s->app->outputs;o;o=o->next) if (o->surface==surface) s->focused=true;
}
static void leave(void *data, struct wl_keyboard *k, uint32_t serial, struct wl_surface *surface) {
    (void)k;(void)serial;(void)surface;
    struct seat *s=data; s->focused=false; s->focus_surface=NULL; input_reset(s);
}
static void key(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t time, uint32_t code, uint32_t state) {
    (void)k;(void)serial;(void)time;
    struct seat *s=data;
    if (state==WL_KEYBOARD_KEY_STATE_RELEASED) {
        if (s->repeat_key==code) s->repeat_at=0;
    } else { s->repeat_at=0; input_key(s,code,false); }
}
static void modifiers(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    (void)k;(void)serial;
    struct seat *s=data;
    if (s->state) xkb_state_update_mask(s->state,depressed,latched,locked,0,0,group);
}
static void repeat_info(void *data, struct wl_keyboard *k, int32_t rate, int32_t delay) {
    (void)k; struct seat *s=data; s->rate=rate>0?rate:0; s->delay=delay>0?delay:0; s->repeat_at=0;
}
static const struct wl_keyboard_listener keyboard_listener={
    .keymap=keymap,.enter=enter,.leave=leave,.key=key,.modifiers=modifiers,.repeat_info=repeat_info
};
static void drop_keyboard(struct seat *s) {
    input_reset(s); s->focused=false; s->focus_surface=NULL;
    if (s->keyboard) {
        if (wl_keyboard_get_version(s->keyboard)>=WL_KEYBOARD_RELEASE_SINCE_VERSION) wl_keyboard_release(s->keyboard);
        else wl_keyboard_destroy(s->keyboard);
        s->keyboard=NULL;
    }
    xkb_state_unref(s->state); s->state=NULL;
    xkb_keymap_unref(s->keymap); s->keymap=NULL;
}
static void capabilities(void *data, struct wl_seat *proxy, uint32_t caps) {
    struct seat *s=data;
    if (!(caps&WL_SEAT_CAPABILITY_KEYBOARD)) drop_keyboard(s);
    else if (!s->keyboard) {
        s->keyboard=wl_seat_get_keyboard(proxy);
        wl_keyboard_add_listener(s->keyboard,&keyboard_listener,s);
    }
}
static void seat_name(void *data, struct wl_seat *p, const char *name) { (void)data;(void)p;(void)name; }
static const struct wl_seat_listener seat_listener={.capabilities=capabilities,.name=seat_name};
void input_seat_create(struct locker *app, uint32_t name, uint32_t version) {
    struct seat *s=calloc(1,sizeof(*s));
    if (!s) { lock_log("seat-allocation-failed"); return; }
    s->app=app; s->name=name; s->next=app->seats; app->seats=s;
    s->proxy=wl_registry_bind(app->registry,name,&wl_seat_interface,version<5?version:5);
    if (app->compose_table) s->compose=xkb_compose_state_new(app->compose_table,XKB_COMPOSE_STATE_NO_FLAGS);
    if (app->compose_table && !s->compose) lock_log("compose-unavailable");
    wl_seat_add_listener(s->proxy,&seat_listener,s);
}
void input_seat_destroy(struct seat *s) {
    drop_keyboard(s); xkb_compose_state_unref(s->compose);
    if (wl_seat_get_version(s->proxy)>=WL_SEAT_RELEASE_SINCE_VERSION) wl_seat_release(s->proxy);
    else wl_seat_destroy(s->proxy);
    free(s);
}
