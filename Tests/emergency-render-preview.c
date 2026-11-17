#define _GNU_SOURCE
/* Test-only xdg window. Links the production drawing function, not the locker
 * or auth state machine. This executable is never installed. */
#include "../emergency/locker.h"
#include "idle-xdg-shell-client.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *shell;
static struct wl_surface *surface;
static struct wl_buffer *buffer;
static volatile sig_atomic_t running=1;
static void stop(int sig) { (void)sig; running=0; }
static void ping(void *data,struct xdg_wm_base *base,uint32_t serial) { (void)data; xdg_wm_base_pong(base,serial); }
static const struct xdg_wm_base_listener shell_listener={.ping=ping};
static void global(void *data,struct wl_registry *registry,uint32_t name,const char *interface,uint32_t version) {
    (void)data;(void)version;
    if (!strcmp(interface,"wl_compositor")) compositor=wl_registry_bind(registry,name,&wl_compositor_interface,1);
    else if (!strcmp(interface,"wl_shm")) shm=wl_registry_bind(registry,name,&wl_shm_interface,1);
    else if (!strcmp(interface,"xdg_wm_base")) {
        shell=wl_registry_bind(registry,name,&xdg_wm_base_interface,1);
        xdg_wm_base_add_listener(shell,&shell_listener,NULL);
    }
}
static void removed(void *data,struct wl_registry *r,uint32_t name) { (void)data;(void)r;(void)name; }
static const struct wl_registry_listener registry_listener={.global=global,.global_remove=removed};
static void presented(void *data,struct wl_callback *callback,uint32_t time) {
    (void)data;(void)time; wl_callback_destroy(callback); puts("WINDOW_RENDER_PRESENTED"); fflush(stdout);
}
static const struct wl_callback_listener frame_listener={.done=presented};
static void configure(void *data,struct xdg_surface *xdg,uint32_t serial) {
    (void)data; xdg_surface_ack_configure(xdg,serial);
    wl_surface_attach(surface,buffer,0,0); wl_surface_damage(surface,0,0,960,600);
    wl_callback_add_listener(wl_surface_frame(surface),&frame_listener,NULL); wl_surface_commit(surface);
}
static const struct xdg_surface_listener surface_listener={.configure=configure};
static void size(void *data,struct xdg_toplevel *top,int32_t w,int32_t h,struct wl_array *states) {
    (void)data;(void)top;(void)w;(void)h;(void)states;
}
static void close_window(void *data,struct xdg_toplevel *top) { (void)data;(void)top; running=0; }
static const struct xdg_toplevel_listener top_listener={.configure=size,.close=close_window};
int main(int argc,char **argv) {
    if (argc<2 || argc>3) return 2;
    struct picture picture={.rtl=argc==3 && !strcmp(argv[2],"rtl")};
    if (!strcmp(argv[1],"typing")) picture.dots=12;
    else if (!strcmp(argv[1],"checking")) { picture.busy=true; picture.pulse=true; }
    else if (!strcmp(argv[1],"wrong")) picture.error=true;
    else if (strcmp(argv[1],"idle")) return 2;
    struct sigaction action={.sa_handler=stop}; sigemptyset(&action.sa_mask);
    sigaction(SIGTERM,&action,NULL); sigaction(SIGINT,&action,NULL);
    struct wl_display *display=wl_display_connect(NULL); if (!display) return 1;
    struct wl_registry *registry=wl_display_get_registry(display);
    wl_registry_add_listener(registry,&registry_listener,NULL);
    if (wl_display_roundtrip(display)<0 || !compositor || !shm || !shell) return 1;
    const size_t bytes=960*600*4;
    int fd=memfd_create("emergency-window-render",MFD_CLOEXEC);
    if (fd<0 || ftruncate(fd,(off_t)bytes)) return 1;
    uint32_t *pixels=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); if (pixels==MAP_FAILED) return 1;
    emergency_draw(pixels,960,600,1,&picture);
    struct wl_shm_pool *pool=wl_shm_create_pool(shm,fd,(int32_t)bytes);
    buffer=wl_shm_pool_create_buffer(pool,0,960,600,960*4,WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool); close(fd);
    surface=wl_compositor_create_surface(compositor);
    struct xdg_surface *xdg=xdg_wm_base_get_xdg_surface(shell,surface);
    xdg_surface_add_listener(xdg,&surface_listener,NULL);
    struct xdg_toplevel *top=xdg_surface_get_toplevel(xdg);
    xdg_toplevel_add_listener(top,&top_listener,NULL);
    xdg_toplevel_set_title(top,"Emergency locker - test-only window render");
    xdg_toplevel_set_app_id(top,"aerial-emergency-window-render");
    xdg_toplevel_set_min_size(top,960,600); xdg_toplevel_set_max_size(top,960,600);
    wl_surface_commit(surface);
    while (running) {
        if (wl_display_dispatch_pending(display)<0 || wl_display_flush(display)<0) break;
        struct pollfd p={.fd=wl_display_get_fd(display),.events=POLLIN};
        int rc=poll(&p,1,100);
        if (rc<0 && errno==EINTR) continue;
        if (rc<0 || p.revents&(POLLERR|POLLHUP|POLLNVAL)) break;
        if ((p.revents&POLLIN) && wl_display_dispatch(display)<0) break;
    }
    xdg_toplevel_destroy(top); xdg_surface_destroy(xdg); wl_surface_destroy(surface); wl_buffer_destroy(buffer);
    xdg_wm_base_destroy(shell); wl_shm_destroy(shm); wl_compositor_destroy(compositor); wl_registry_destroy(registry);
    wl_display_flush(display); wl_display_disconnect(display); munmap(pixels,bytes);
    return 0;
}
