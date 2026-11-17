#define _GNU_SOURCE
#include "locker.h"
#include "ext-session-lock-v1-client.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void buffer_drop(struct buffer *b) {
    if (b->proxy) wl_buffer_destroy(b->proxy);
    if (b->pixels) munmap(b->pixels,b->size);
    memset(b,0,sizeof(*b));
}
static void buffer_release(void *data, struct wl_buffer *proxy) {
    (void)proxy; ((struct buffer *)data)->busy=false;
}
static const struct wl_buffer_listener buffer_listener={.release=buffer_release};
static bool buffer_make(struct locker *app, struct buffer *b, int w, int h, size_t size) {
    buffer_drop(b);
    int fd=memfd_create("aerial-emergency-pixels",MFD_CLOEXEC);
    if (fd<0) return false;
    if (ftruncate(fd,(off_t)size)) { close(fd); return false; }
    void *p=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if (p==MAP_FAILED) { close(fd); return false; }
    struct wl_shm_pool *pool=wl_shm_create_pool(app->shm,fd,(int32_t)size);
    close(fd);
    if (!pool) { munmap(p,size); return false; }
    b->proxy=wl_shm_pool_create_buffer(pool,0,w,h,w*4,WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    if (!b->proxy) { munmap(p,size); return false; }
    b->pixels=p; b->size=size; b->width=w; b->height=h;
    wl_buffer_add_listener(b->proxy,&buffer_listener,b);
    return true;
}
static void output_drop(struct output *o) {
    if (o->role) ext_session_lock_surface_v1_destroy(o->role);
    if (o->surface) wl_surface_destroy(o->surface);
    for (unsigned i=0;i<2;++i) buffer_drop(&o->buffers[i]);
    if (wl_output_get_version(o->proxy)>=WL_OUTPUT_RELEASE_SINCE_VERSION) wl_output_release(o->proxy);
    else wl_output_destroy(o->proxy);
    free(o);
}
void wayland_redraw(struct locker *app) {
    if (!app->lock || app->done || app->releasing) return;
    for (struct output *o=app->outputs;o;o=o->next) {
        if (!o->configured || !o->dirty) continue;
        int w,h; size_t size;
        if (!emergency_dimensions(o->width,o->height,o->scale,&w,&h,&size)) {
            o->dirty=false; lock_log("surface-invalid-dimensions"); continue;
        }
        struct buffer *b=NULL;
        for (unsigned i=0;i<2;++i) if (!o->buffers[i].busy) { b=&o->buffers[i]; break; }
        if (!b) continue; /* Coalesce into the latest configure/state. */
        if ((!b->proxy || b->width!=w || b->height!=h) && !buffer_make(app,b,w,h,size)) {
            o->dirty=false; lock_log("surface-allocation-failed"); continue;
        }
        struct picture p={.dots=app->secret->scalars,.rtl=app->rtl,.busy=app->busy,
                          .pulse=app->pulse,.error=app->error_until>monotonic_ms()};
        emergency_draw(b->pixels,w,h,o->scale,&p);
        if (o->pending_ack) { ext_session_lock_surface_v1_ack_configure(o->role,o->serial); o->pending_ack=false; }
        if (wl_surface_get_version(o->surface)>=WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION)
            wl_surface_set_buffer_scale(o->surface,o->scale);
        wl_surface_attach(o->surface,b->proxy,0,0);
        wl_surface_damage(o->surface,0,0,INT32_MAX,INT32_MAX);
        wl_surface_commit(o->surface); b->busy=true; o->dirty=false;
        if (!o->ready) { o->ready=true; lock_log("surface-submitted"); }
    }
}
static void configure(void *data, struct ext_session_lock_surface_v1 *role, uint32_t serial, uint32_t w, uint32_t h) {
    (void)role;
    struct output *o=data;
    o->width=w; o->height=h; o->serial=serial;
    o->pending_ack=true; o->configured=true; o->dirty=true;
}
static const struct ext_session_lock_surface_v1_listener surface_listener={.configure=configure};
static void output_role(struct output *o) {
    if (o->surface || !o->app->lock || o->app->releasing) return;
    o->surface=wl_compositor_create_surface(o->app->compositor);
    o->role=ext_session_lock_v1_get_lock_surface(o->app->lock,o->surface,o->proxy);
    ext_session_lock_surface_v1_add_listener(o->role,&surface_listener,o);
}
static void geometry(void *d, struct wl_output *p, int32_t x,int32_t y,int32_t pw,int32_t ph,int32_t sub,const char *make,const char *model,int32_t transform) {
    (void)d;(void)p;(void)x;(void)y;(void)pw;(void)ph;(void)sub;(void)make;(void)model;(void)transform;
}
static void mode(void *d,struct wl_output *p,uint32_t flags,int32_t w,int32_t h,int32_t refresh) {
    (void)d;(void)p;(void)flags;(void)w;(void)h;(void)refresh;
}
static void output_done(void *d,struct wl_output *p) { (void)d;(void)p; }
static void scale(void *data,struct wl_output *p,int32_t factor) {
    (void)p; struct output *o=data;
    if (factor<1) { lock_log("output-invalid-scale"); return; }
    /* Compositor v1/v2 cannot set buffer scale: use the protocol's scale 1. */
    if (o->surface && wl_surface_get_version(o->surface)<WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION) factor=1;
    o->scale=factor; o->dirty=true;
}
static const struct wl_output_listener output_listener={.geometry=geometry,.mode=mode,.done=output_done,.scale=scale};
static void registry_global(void *data,struct wl_registry *registry,uint32_t name,const char *interface,uint32_t version) {
    struct locker *app=data;
    if (!version) return;
    if (!strcmp(interface,"wl_compositor") && !app->compositor)
        app->compositor=wl_registry_bind(registry,name,&wl_compositor_interface,version<4?version:4);
    else if (!strcmp(interface,"wl_shm") && !app->shm)
        app->shm=wl_registry_bind(registry,name,&wl_shm_interface,1);
    else if (!strcmp(interface,"ext_session_lock_manager_v1") && !app->manager)
        app->manager=wl_registry_bind(registry,name,&ext_session_lock_manager_v1_interface,1);
    else if (!strcmp(interface,"wl_output")) {
        struct output *o=calloc(1,sizeof(*o));
        if (!o) { lock_log("output-allocation-failed"); return; }
        o->app=app; o->name=name; o->scale=1; o->next=app->outputs; app->outputs=o;
        o->proxy=wl_registry_bind(registry,name,&wl_output_interface,version<3?version:3);
        wl_output_add_listener(o->proxy,&output_listener,o); output_role(o);
    } else if (!strcmp(interface,"wl_seat")) input_seat_create(app,name,version);
}
static void registry_remove(void *data,struct wl_registry *r,uint32_t name) {
    (void)r; struct locker *app=data;
    for (struct output **at=&app->outputs;*at;at=&(*at)->next) if ((*at)->name==name) {
        struct output *o=*at; *at=o->next;
        for (struct seat *s=app->seats;s;s=s->next) if (s->focus_surface==o->surface) {
            s->focused=false; s->focus_surface=NULL;
        }
        output_drop(o); input_reset_all(app); break;
    }
    for (struct seat **at=&app->seats;*at;at=&(*at)->next) if ((*at)->name==name) {
        struct seat *s=*at; *at=s->next; input_seat_destroy(s); break;
    }
}
static const struct wl_registry_listener registry_listener={.global=registry_global,.global_remove=registry_remove};
static void locked(void *data,struct ext_session_lock_v1 *lock) {
    (void)lock; struct locker *app=data;
    app->locked=true; lock_log("lock-confirmed");
}
static void finished(void *data,struct ext_session_lock_v1 *lock) {
    struct locker *app=data;
    if (app->locked) locker_release(app,"compositor-finished");
    else {
        ext_session_lock_v1_destroy(lock); app->lock=NULL;
        locker_stop(app,4,"lock-refused");
    }
}
static const struct ext_session_lock_v1_listener lock_listener={.locked=locked,.finished=finished};
static void initial_done(void *data,struct wl_callback *callback,uint32_t serial) {
    (void)serial; struct locker *app=data;
    wl_callback_destroy(callback); app->sync=NULL;
    if (!app->compositor || !app->shm || !app->manager) { locker_stop(app,2,"required-global-missing"); return; }
    app->lock=ext_session_lock_manager_v1_lock(app->manager);
    ext_session_lock_v1_add_listener(app->lock,&lock_listener,app);
    lock_log("lock-requested");
    for (struct output *o=app->outputs;o;o=o->next) {
        output_role(o);
        if (wl_surface_get_version(o->surface)<WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION) o->scale=1;
    }
}
static const struct wl_callback_listener initial_listener={.done=initial_done};
bool wayland_start(struct locker *app) {
    /* wl_display_connect(NULL) honors the exact supplied WAYLAND_DISPLAY or
     * WAYLAND_SOCKET. The supervisor already identifies its session. */
    if (!getenv("WAYLAND_DISPLAY") && !getenv("WAYLAND_SOCKET")) return false;
    app->display=wl_display_connect(NULL);
    if (!app->display) return false;
    app->registry=wl_display_get_registry(app->display);
    wl_registry_add_listener(app->registry,&registry_listener,app);
    app->sync=wl_display_sync(app->display);
    wl_callback_add_listener(app->sync,&initial_listener,app);
    return true;
}
void wayland_finish(struct locker *app) {
    /* Never issue unlock as error cleanup. Disconnect leaves compositor
     * protection intact. Only locker_release has release authority. */
    if (!app->display) return;
    while (app->outputs) { struct output *o=app->outputs; app->outputs=o->next; output_drop(o); }
    while (app->seats) { struct seat *s=app->seats; app->seats=s->next; input_seat_destroy(s); }
    if (app->sync) wl_callback_destroy(app->sync);
    if (app->lock) wl_proxy_destroy((struct wl_proxy *)app->lock);
    if (app->manager) ext_session_lock_manager_v1_destroy(app->manager);
    if (app->shm) wl_shm_destroy(app->shm);
    if (app->compositor) wl_compositor_destroy(app->compositor);
    if (app->registry) wl_registry_destroy(app->registry);
    wl_display_disconnect(app->display);
}
