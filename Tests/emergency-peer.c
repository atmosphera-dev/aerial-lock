#define _GNU_SOURCE
#include "test-check.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>
#include "emergency-server.h"

/* A protocol peer rather than a rendering compositor. Every accepted commit
 * is checked at the actual shm boundary; no client log is a frame witness. */
struct peer;
struct surface {
    struct peer *peer;
    struct wl_resource *resource, *role, *buffer;
    struct surface *next;
    uint32_t serial, ack;
    int width,height,scale;
    unsigned commits;
    struct wl_resource *held[2];
    uint64_t hashes[2];
    bool hold;
};
struct peer {
    struct wl_display *display;
    struct wl_event_loop *loop;
    struct wl_client *client;
    struct wl_resource *lock,*keyboard;
    struct wl_global *output;
    struct surface *surfaces;
    const char *mode;
    unsigned commits, unlocks, serial;
    bool refused, granted;
};
static int64_t now(void) { struct timespec t; CHECK(!clock_gettime(CLOCK_MONOTONIC,&t)); return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static void destroy(struct wl_client *c,struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void attach(struct wl_client *c,struct wl_resource *r,struct wl_resource *b,int32_t x,int32_t y) {
    (void)c; CHECK(!x && !y && b); ((struct surface *)wl_resource_get_user_data(r))->buffer=b;
}
static void damage(struct wl_client *c,struct wl_resource *r,int32_t x,int32_t y,int32_t w,int32_t h) {
    (void)c;(void)r;(void)x;(void)y;(void)w;(void)h;
}
static uint64_t buffer_hash(struct wl_resource *resource) {
    struct wl_shm_buffer *b=wl_shm_buffer_get(resource); CHECK(b);
    wl_shm_buffer_begin_access(b);
    const unsigned char *p=wl_shm_buffer_get_data(b);
    size_t size=(size_t)wl_shm_buffer_get_stride(b)*(size_t)wl_shm_buffer_get_height(b);
    uint64_t hash=1469598103934665603ull;
    for (size_t i=0;i<size;++i) hash=(hash^p[i])*1099511628211ull;
    wl_shm_buffer_end_access(b);
    return hash;
}
static void commit(struct wl_client *c,struct wl_resource *r) {
    (void)c; struct surface *s=wl_resource_get_user_data(r);
    CHECK(s->ack==s->serial && s->buffer);
    struct wl_shm_buffer *b=wl_shm_buffer_get(s->buffer); CHECK(b);
    CHECK(wl_shm_buffer_get_width(b)==s->width*s->scale);
    CHECK(wl_shm_buffer_get_height(b)==s->height*s->scale);
    CHECK(wl_shm_buffer_get_stride(b)==s->width*s->scale*4);
    CHECK(wl_shm_buffer_get_format(b)==WL_SHM_FORMAT_XRGB8888);
    wl_shm_buffer_begin_access(b);
    const uint32_t *pixels=wl_shm_buffer_get_data(b);
    CHECK(pixels[0]==0xff000000);
    unsigned foreground=0;
    for (size_t i=0;i<(size_t)s->width*s->height*s->scale*s->scale;++i) {
        CHECK(pixels[i]==0xff000000 || pixels[i]==0xff242424 || pixels[i]==0xffffffff ||
               pixels[i]==0xffff6666 || pixels[i]==0xff555555 || pixels[i]==0xff999999);
        if (pixels[i]==0xffffffff || pixels[i]==0xffff6666) ++foreground;
    }
    CHECK(foreground); wl_shm_buffer_end_access(b);
    ++s->commits; ++s->peer->commits;
    if (s->hold) {
        unsigned at=s->held[0]?1:0; CHECK(!s->held[at]);
        s->held[at]=s->buffer; s->hashes[at]=buffer_hash(s->buffer);
    } else wl_buffer_send_release(s->buffer);
    s->buffer=NULL;
    if (!s->peer->granted && !s->peer->refused) {
        ext_session_lock_v1_send_locked(s->peer->lock); s->peer->granted=true;
    }
}
static void set_scale(struct wl_client *c,struct wl_resource *r,int32_t scale) {
    (void)c; CHECK(scale>0); ((struct surface *)wl_resource_get_user_data(r))->scale=scale;
}
static const struct wl_surface_interface surface_impl={.destroy=destroy,.attach=attach,.damage=damage,.commit=commit,.set_buffer_scale=set_scale,.damage_buffer=damage};
static void surface_gone(struct wl_resource *r) {
    struct surface *s=wl_resource_get_user_data(r); s->resource=NULL;
}
static void create_surface(struct wl_client *c,struct wl_resource *r,uint32_t id) {
    struct peer *p=wl_resource_get_user_data(r);
    struct surface *s=calloc(1,sizeof(*s)); CHECK(s);
    s->peer=p; s->scale=1; s->next=p->surfaces; p->surfaces=s;
    s->resource=wl_resource_create(c,&wl_surface_interface,wl_resource_get_version(r),id);
    wl_resource_set_implementation(s->resource,&surface_impl,s,surface_gone);
}
static const struct wl_compositor_interface compositor_impl={.create_surface=create_surface};
static void bind_compositor(struct wl_client *c,void *data,uint32_t version,uint32_t id) {
    struct wl_resource *r=wl_resource_create(c,&wl_compositor_interface,(int)version,id);
    wl_resource_set_implementation(r,&compositor_impl,data,NULL);
}
static void bind_output(struct wl_client *c,void *data,uint32_t version,uint32_t id) {
    (void)data;
    struct wl_resource *r=wl_resource_create(c,&wl_output_interface,(int)version,id);
    static const struct wl_output_interface impl={.release=destroy};
    wl_resource_set_implementation(r,&impl,data,NULL);
    wl_output_send_geometry(r,0,0,300,200,0,"test","test",0);
    wl_output_send_mode(r,WL_OUTPUT_MODE_CURRENT,800,600,60000);
    if (version>=2) { wl_output_send_scale(r,2); wl_output_send_done(r); }
}
static void keyboard_get(struct wl_client *c,struct wl_resource *r,uint32_t id) {
    struct peer *p=wl_resource_get_user_data(r);
    p->keyboard=wl_resource_create(c,&wl_keyboard_interface,wl_resource_get_version(r),id);
    static const struct wl_keyboard_interface impl={.release=destroy};
    wl_resource_set_implementation(p->keyboard,&impl,p,NULL);
    struct xkb_context *context=xkb_context_new(0);
    struct xkb_rule_names names={.layout="us",.options="compose:ralt"};
    struct xkb_keymap *map=xkb_keymap_new_from_names(context,&names,0); CHECK(map);
    char *text=xkb_keymap_get_as_string(map,XKB_KEYMAP_FORMAT_TEXT_V1); CHECK(text);
    size_t size=strlen(text)+(!strcmp(p->mode,"sized-keymap")?0u:1u);
    int fd=memfd_create("fake-keymap",MFD_CLOEXEC); CHECK(fd>=0);
    CHECK(write(fd,text,size)==(ssize_t)size);
    wl_keyboard_send_keymap(p->keyboard,!strcmp(p->mode,"no-keymap")?WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP:WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,fd,(uint32_t)size);
    close(fd); free(text); xkb_keymap_unref(map); xkb_context_unref(context);
    if (wl_resource_get_version(r)>=4) wl_keyboard_send_repeat_info(p->keyboard,25,400);
}
static const struct wl_seat_interface seat_impl={.get_keyboard=keyboard_get,.release=destroy};
static void bind_seat(struct wl_client *c,void *data,uint32_t version,uint32_t id) {
    struct wl_resource *r=wl_resource_create(c,&wl_seat_interface,(int)version,id);
    wl_resource_set_implementation(r,&seat_impl,data,NULL);
    wl_seat_send_capabilities(r,WL_SEAT_CAPABILITY_KEYBOARD);
    if (version>=2) wl_seat_send_name(r,"test");
}
static void ack(struct wl_client *c,struct wl_resource *r,uint32_t serial) {
    (void)c; struct surface *s=wl_resource_get_user_data(r);
    CHECK(serial==s->serial); s->ack=serial;
}
static const struct ext_session_lock_surface_v1_interface role_impl={.destroy=destroy,.ack_configure=ack};
static void get_surface(struct wl_client *c,struct wl_resource *r,uint32_t id,struct wl_resource *surface,struct wl_resource *output) {
    (void)output;
    struct peer *p=wl_resource_get_user_data(r);
    struct surface *s=wl_resource_get_user_data(surface); CHECK(!s->role);
    s->role=wl_resource_create(c,&ext_session_lock_surface_v1_interface,1,id);
    wl_resource_set_implementation(s->role,&role_impl,s,NULL);
    s->serial=++p->serial; s->width=800; s->height=600;
    if (!p->refused) ext_session_lock_surface_v1_send_configure(s->role,s->serial,s->width,s->height);
}
static void unlock(struct wl_client *c,struct wl_resource *r) {
    (void)c; struct peer *p=wl_resource_get_user_data(r);
    CHECK(p->granted); ++p->unlocks; p->lock=NULL; wl_resource_destroy(r);
}
static const struct ext_session_lock_v1_interface lock_impl={.destroy=destroy,.get_lock_surface=get_surface,.unlock_and_destroy=unlock};
static void lock_get(struct wl_client *c,struct wl_resource *r,uint32_t id) {
    struct peer *p=wl_resource_get_user_data(r);
    p->lock=wl_resource_create(c,&ext_session_lock_v1_interface,1,id);
    wl_resource_set_implementation(p->lock,&lock_impl,p,NULL);
    if (!strcmp(p->mode,"refused")) { p->refused=true; ext_session_lock_v1_send_finished(p->lock); }
    if (!strcmp(p->mode,"early-locked") || !strcmp(p->mode,"no-outputs")) { p->granted=true; ext_session_lock_v1_send_locked(p->lock); }
}
static const struct ext_session_lock_manager_v1_interface manager_impl={.destroy=destroy,.lock=lock_get};
static void bind_manager(struct wl_client *c,void *data,uint32_t version,uint32_t id) {
    struct wl_resource *r=wl_resource_create(c,&ext_session_lock_manager_v1_interface,(int)version,id);
    wl_resource_set_implementation(r,&manager_impl,data,NULL);
}
static void pump(struct peer *p,int ms) {
    int64_t end=now()+ms;
    do {
        if (strcmp(p->mode,"stalled-sync") || !p->unlocks) wl_display_flush_clients(p->display);
        CHECK(wl_event_loop_dispatch(p->loop,5)>=0);
    } while (now()<end);
}
static void press(struct peer *p,uint32_t key) {
    wl_keyboard_send_key(p->keyboard,++p->serial,1,key,WL_KEYBOARD_KEY_STATE_PRESSED);
    wl_keyboard_send_key(p->keyboard,++p->serial,2,key,WL_KEYBOARD_KEY_STATE_RELEASED);
}
static void run(const char *binary,const char *mode) {
    struct peer p={.mode=mode};
    p.display=wl_display_create(); CHECK(p.display); p.loop=wl_display_get_event_loop(p.display);
    CHECK(!wl_display_init_shm(p.display));
    if (strcmp(mode,"missing-global")) wl_global_create(p.display,&wl_compositor_interface,4,&p,bind_compositor);
    if (strcmp(mode,"no-outputs")) p.output=wl_global_create(p.display,&wl_output_interface,3,&p,bind_output);
    wl_global_create(p.display,&wl_seat_interface,!strcmp(mode,"core-v1")?1:5,&p,bind_seat);
    wl_global_create(p.display,&ext_session_lock_manager_v1_interface,1,&p,bind_manager);
    int pair[2]; CHECK(!socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair));
    char *directory=NULL; CHECK(asprintf(&directory,"%s/emergency-peer-XXXXXX",getenv("TMPDIR"))>0);
    CHECK(mkdtemp(directory));
    char path[108]; CHECK(snprintf(path,sizeof(path),"%s/control",directory)<(int)sizeof(path));
    struct sockaddr_un addr={.sun_family=AF_UNIX}; strcpy(addr.sun_path,path);
    int listener=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0); CHECK(listener>=0);
    CHECK(!bind(listener,(struct sockaddr *)&addr,sizeof(addr)) && !listen(listener,1));
    pid_t pid=fork(); CHECK(pid>=0);
    if (!pid) {
        close(pair[0]); CHECK(!fcntl(pair[1],F_SETFD,0));
        char fd[32]; snprintf(fd,sizeof(fd),"%d",pair[1]);
        setenv("WAYLAND_SOCKET",fd,1); setenv("AERIAL_LOCK_ENGAGEMENT_SOCKET",path,1); unsetenv("WAYLAND_DEBUG");
        if (!strcmp(mode,"mixed-locale")) {
            unsetenv("LC_ALL"); setenv("LANG","C.UTF-8",1); setenv("LC_NUMERIC","C",1);
        }
        execl(binary,binary,mode,"150",NULL); _exit(127);
    }
    close(pair[1]); p.client=wl_client_create(p.display,pair[0]); CHECK(p.client);
    bool negative=!strcmp(mode,"missing-global") || !strcmp(mode,"refused");
    int control=-1, status=0;
    int64_t end=now()+5000;
    while (((!p.commits && !(!strcmp(mode,"no-outputs") && p.granted)) || !p.keyboard || control<0) && now()<end) {
        pump(&p,5);
        if (control<0) control=accept4(listener,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);
        if (negative && waitpid(pid,&status,WNOHANG)==pid) { pid=0; break; }
    }
    if (negative) { CHECK(!pid && WIFEXITED(status) && WEXITSTATUS(status)==(!strcmp(mode,"refused")?4:2)); CHECK(!p.unlocks); }
    else {
        CHECK((p.commits || !strcmp(mode,"no-outputs")) && p.keyboard && control>=0);
        char report[8]={0}; size_t used=0; end=now()+1000;
        while (used<7 && now()<end) {
            pump(&p,5); ssize_t n=read(control,report+used,7-used); if (n>0) used+=(size_t)n;
        }
        CHECK(used==7 && !memcmp(report,"locked\n",7));
        if (!strcmp(mode,"no-outputs")) {
            pump(&p,150); CHECK(!p.commits && !p.unlocks && !waitpid(pid,&status,WNOHANG));
            p.output=wl_global_create(p.display,&wl_output_interface,3,&p,bind_output);
            pump(&p,100); CHECK(p.commits);
        }
        struct surface *s=p.surfaces;
        /* A configure storm commits only matching latest geometry, at scale 2. */
        for (int i=0;i<20;++i) { s->width=800+i; s->height=600+i; s->serial=++p.serial; ext_session_lock_surface_v1_send_configure(s->role,s->serial,s->width,s->height); }
        pump(&p,100); CHECK(s->commits>=2);
        if (!strcmp(mode,"busy-buffers")) {
            s->hold=true;
            for (int n=0;n<2;++n) {
                s->width+=1; s->serial=++p.serial;
                ext_session_lock_surface_v1_send_configure(s->role,s->serial,s->width,s->height);
                pump(&p,100);
            }
            unsigned before=p.commits;
            for (int n=0;n<100;++n) {
                s->width+=1; s->serial=++p.serial;
                ext_session_lock_surface_v1_send_configure(s->role,s->serial,s->width,s->height);
            }
            pump(&p,150); CHECK(p.commits==before);
            for (int n=0;n<2;++n) {
                CHECK(s->held[n] && buffer_hash(s->held[n])==s->hashes[n]);
                wl_buffer_send_release(s->held[n]); s->held[n]=NULL;
            }
            s->hold=false; pump(&p,100); CHECK(p.commits>before);
        }
        struct wl_array held; wl_array_init(&held); uint32_t *held_key=wl_array_add(&held,4); CHECK(held_key); *held_key=30;
        wl_keyboard_send_enter(p.keyboard,++p.serial,s->resource,&held); wl_array_release(&held);
        wl_keyboard_send_modifiers(p.keyboard,++p.serial,0,0,0,0);
        pump(&p,30); /* held keys must not be replayed as text */
        if (!strcmp(mode,"release") || !strcmp(mode,"early-locked") || !strcmp(mode,"busy-buffers") || !strcmp(mode,"core-v1") || !strcmp(mode,"stalled-sync") || !strcmp(mode,"no-outputs")) {
            CHECK(write(control,"release\n",8)==8);
        } else if (!strcmp(mode,"finished")) ext_session_lock_v1_send_finished(p.lock);
        else if (!strcmp(mode,"disconnect")) { wl_client_destroy(p.client); p.client=NULL; }
        else if (!strcmp(mode,"protocol-error")) wl_resource_post_error(s->resource,0,"test-error");
        else if (!strcmp(mode,"channel-eof")) { close(control); control=-1; pump(&p,50); CHECK(!p.unlocks && !waitpid(pid,&status,WNOHANG)); kill(pid,SIGTERM); }
        else {
            if (!strcmp(mode,"mixed-locale")) { press(&p,100); press(&p,40); press(&p,18); }
            else press(&p,30);
            if (!strcmp(mode,"hotplug")) {
                pump(&p,50); wl_global_destroy(p.output); pump(&p,100);
                CHECK(!s->resource && !p.unlocks); /* zero-output interval */
                p.output=wl_global_create(p.display,&wl_output_interface,3,&p,bind_output);
                pump(&p,100); s=p.surfaces; CHECK(s->resource && s->commits);
                struct wl_array empty; wl_array_init(&empty);
                wl_keyboard_send_enter(p.keyboard,++p.serial,s->resource,&empty); wl_array_release(&empty);
                pump(&p,30);
            }
            press(&p,28); /* one ASCII fixture response */
            pump(&p,500);
            if (strcmp(mode,"success") && strcmp(mode,"hotplug") && strcmp(mode,"mixed-locale") && strcmp(mode,"sized-keymap")) {
                CHECK(!p.unlocks && !waitpid(pid,&status,WNOHANG));
                /* A failed attempt remains the same client. Explicit release
                 * afterward proves failures did not terminate or unlock it. */
                CHECK(write(control,"release\n",8)==8);
            }
        }
        end=now()+4000;
        while (now()<end) {
            pump(&p,5);
            if (waitpid(pid,&status,WNOHANG)==pid) { pid=0; break; }
        }
        CHECK(!pid && WIFEXITED(status));
        bool error=!strcmp(mode,"disconnect") || !strcmp(mode,"protocol-error") || !strcmp(mode,"channel-eof");
        CHECK(WEXITSTATUS(status)==((error || !strcmp(mode,"stalled-sync"))?2:0)); CHECK(p.unlocks==(error?0u:1u));
    }
    if (control>=0) close(control); close(listener); unlink(path); rmdir(directory); free(directory);
    wl_display_destroy_clients(p.display); wl_display_destroy(p.display);
    while (p.surfaces) { struct surface *s=p.surfaces; p.surfaces=s->next; free(s); }
    printf("EMERGENCY_PEER_PASS %s commits=%u unlocks=%u\n",mode,p.commits,p.unlocks);
}
int main(int argc,char **argv) {
    CHECK(argc==2 && getenv("TMPDIR") && strncmp(getenv("TMPDIR"),"/tmp",4));
    const char *modes[]={"missing-global","refused","release","early-locked","finished","disconnect","protocol-error","channel-eof",
                         "busy-buffers","core-v1","no-outputs","no-keymap","stalled-sync","hotplug","mixed-locale","sized-keymap","success","failed","visible","second","extra","truncated","crash","timeout"};
    for (unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);++i) run(argv[1],modes[i]);
    return 0;
}
