#define _GNU_SOURCE
#include "locker.h"
#include "ext-session-lock-v1-client.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#ifndef AERIAL_LOCK_AUTH_HELPER
#define AERIAL_LOCK_AUTH_HELPER "/usr/libexec/aerial-lock/aerial-lock-auth"
#endif
static volatile sig_atomic_t stopped;
static volatile sig_atomic_t signal_fd=-1;
static void signal_stop(int signal) {
    (void)signal; stopped=1;
    int saved=errno;
    if (signal_fd>=0) { char c=1; ssize_t rc=write((int)signal_fd,&c,1); (void)rc; }
    errno=saved;
}
int64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC,&ts)) abort();
    return (int64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;
}
void lock_log(const char *reason) { fprintf(stderr,"aerial-lock-emergency: lock-state: %s\n",reason); }
void locker_changed(struct locker *app) {
    for (struct output *o=app->outputs;o;o=o->next) o->dirty=true;
}
void locker_error(struct locker *app,const char *reason) {
    app->error_until=monotonic_ms()+700; locker_changed(app); lock_log(reason);
}
void locker_stop(struct locker *app,int result,const char *reason) {
    if (app->done) return;
    app->result=result; app->done=true; locker_cancel(app); lock_log(reason);
}
void locker_cancel(struct locker *app) {
    secret_clear(app->secret); input_reset_all(app);
    if (app->auth) { auth_client_abort(app->auth); app->retiring=true; }
    app->prompted=false; app->busy=app->auth!=NULL;
    locker_changed(app);
}
static void release_done(void *data,struct wl_callback *callback,uint32_t serial) {
    (void)serial; struct locker *app=data;
    wl_callback_destroy(callback); app->sync=NULL;
    locker_stop(app,0,"release-confirmed");
}
static const struct wl_callback_listener release_listener={.done=release_done};
void locker_release(struct locker *app,const char *reason) {
    if (!app->locked || app->releasing || app->done) return;
    locker_cancel(app); app->releasing=true; lock_log(reason);
    ext_session_lock_v1_unlock_and_destroy(app->lock); app->lock=NULL;
    app->sync=wl_display_sync(app->display);
    wl_callback_add_listener(app->sync,&release_listener,app);
    app->release_deadline=monotonic_ms()+3000;
}
static bool send_draft(struct locker *app) {
    if (!secret_utf8(app->secret->draft,app->secret->length,NULL) ||
        !auth_client_respond(app->auth,app->secret->draft,app->secret->length)) return false;
    secret_clear(app->secret); app->busy=true;
    input_reset_all(app); app->auth_deadline=monotonic_ms()+app->watchdog;
    lock_log("auth-busy"); locker_changed(app); return true;
}
void locker_submit(struct locker *app) {
    /* Only the initial hidden PAM prompt is supported here and it is always
     * answered from a queued draft. Extra prompts fail closed in auth_step. */
    if (app->auth || !app->secret->length) return;
    input_reset_all(app);
    if (!app->service || !*app->service) { locker_error(app,"auth-service-unavailable"); secret_clear(app->secret); return; }
    app->auth=auth_client_create();
    if (!app->auth) { locker_error(app,"helper-allocation-failed"); secret_clear(app->secret); return; }
    if (!auth_client_memory_locked(app->auth)) lock_log("helper-memory-unlocked");
    if (!auth_client_start(app->auth,AERIAL_LOCK_AUTH_HELPER,app->service,app->user,"/etc/pam.d")) {
        locker_error(app,"helper-start-failed"); locker_cancel(app); return;
    }
    app->busy=true;
    app->auth_deadline=monotonic_ms()+app->watchdog;
    app->pulse_at=monotonic_ms()+350;
    lock_log("auth-busy"); locker_changed(app);
}
static void auth_step(struct locker *app,int64_t now) {
    if (!app->auth) return;
    if (app->retiring) {
        if (auth_client_reap(app->auth) && auth_client_destroy(app->auth)) {
            app->auth=NULL; app->retiring=false; app->busy=false; locker_changed(app);
            if (!app->releasing && !app->done) lock_log("auth-ready");
        }
        return;
    }
    enum auth_event event;
    do {
        event=auth_client_step(app->auth);
        if (event==AUTH_MESSAGE) {
            app->auth_deadline=now+app->watchdog;
            if (auth_client_response_required(app->auth)) {
                if (app->prompted || auth_client_response_visible(app->auth)) {
                    locker_error(app,"auth-unsupported-prompt"); locker_cancel(app); return;
                }
                app->prompted=true; input_reset_all(app);
                if (!send_draft(app)) { locker_error(app,"helper-response-error"); locker_cancel(app); return; }
            }
        }
    } while (event==AUTH_MESSAGE);
    if (event==AUTH_COMPLETE) {
        bool success=auth_client_result(app->auth)==AUTH_SUCCESS;
        if (success) locker_release(app,"auth-success");
        else { locker_error(app,auth_client_result(app->auth)==AUTH_ERROR?"auth-error":"auth-failed"); locker_cancel(app); }
    } else if (event==AUTH_BROKEN || (app->busy && now>=app->auth_deadline)) {
        locker_error(app,event==AUTH_BROKEN?"helper-protocol-error":"helper-timeout"); locker_cancel(app);
    }
}
static void control_open(struct locker *app) {
    const char *path=getenv("AERIAL_LOCK_ENGAGEMENT_SOCKET");
    if (!path || !*path) return;
    struct sockaddr_un address={.sun_family=AF_UNIX};
    if (strlen(path)>=sizeof(address.sun_path)) { lock_log("channel-path-invalid"); return; }
    strcpy(address.sun_path,path);
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if (fd<0) { lock_log("channel-unavailable"); return; }
    if (connect(fd,(struct sockaddr *)&address,sizeof(address)) && errno!=EINPROGRESS) {
        close(fd); lock_log("channel-unavailable"); return;
    }
    app->control=fd; app->control_connecting=true;
}
static void control_step(struct locker *app) {
    if (app->control<0) return;
    if (app->control_connecting) {
        int error=0; socklen_t size=sizeof(error);
        if (getsockopt(app->control,SOL_SOCKET,SO_ERROR,&error,&size) || error) goto lost;
        app->control_connecting=false;
    }
    if (app->locked && !app->releasing) {
        const char report[]="locked\n";
        while (app->report_offset<sizeof(report)-1) {
            ssize_t n=send(app->control,report+app->report_offset,sizeof(report)-1-app->report_offset,MSG_NOSIGNAL);
            if (n<0 && errno==EINTR) continue;
            if (n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) break;
            if (n<=0) goto lost;
            app->report_offset+=(size_t)n;
        }
    }
    for (;;) {
        char c;
        ssize_t n=recv(app->control,&c,1,MSG_DONTWAIT);
        if (n<0 && errno==EINTR) continue;
        if (n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) break;
        if (n<=0) goto lost;
        /* Only a complete exact command from the supervisor stream releases.
         * Unknown/oversized input closes the channel, never the lock. */
        const char command[]="release\n";
        if (c!=command[app->command_size]) goto lost;
        if (++app->command_size==sizeof(command)-1) {
            app->command_size=0; locker_release(app,"release-requested");
        }
    }
    return;
lost:
    close(app->control); app->control=-1; lock_log("channel-lost");
}
static int poll_delay(struct locker *app,int64_t now) {
    int delay=app->auth?50:-1;
    int64_t times[]={app->error_until,app->releasing?app->release_deadline:0,app->busy?app->pulse_at:0};
    for (unsigned i=0;i<sizeof(times)/sizeof(times[0]);++i) if (times[i]) {
        int64_t d=times[i]-now; if (d<0) d=0; if (d>INT_MAX) d=INT_MAX;
        if (delay<0 || d<delay) delay=(int)d;
    }
    for (struct seat *s=app->seats;s;s=s->next) if (s->repeat_at) {
        int64_t d=s->repeat_at-now; if (d<0) d=0; if (d>INT_MAX) d=INT_MAX;
        if (delay<0 || d<delay) delay=(int)d;
    }
    return delay;
}
int main(int argc,char **argv) {
    struct rlimit core={0,0};
    if (setrlimit(RLIMIT_CORE,&core) || prctl(PR_SET_DUMPABLE,0)) return 2;
    int signal_pipe[2];
    if (pipe2(signal_pipe,O_CLOEXEC|O_NONBLOCK)) return 2;
    signal_fd=signal_pipe[1];
    struct sigaction action={.sa_handler=signal_stop}; sigemptyset(&action.sa_mask);
    sigaction(SIGTERM,&action,NULL); sigaction(SIGINT,&action,NULL);
    signal(SIGUSR1,SIG_IGN); signal(SIGPIPE,SIG_IGN);
    sigset_t empty; sigemptyset(&empty); sigprocmask(SIG_SETMASK,&empty,NULL);
    struct locker app={.control=-1,.watchdog=60000,.result=2};
    if (argc!=3 && !(argc==4 && !strcmp(argv[3],"--ignore-leading-spaces"))) { lock_log("invalid-arguments"); return 2; }
    app.ignore_leading_spaces=argc==4;
    app.service=argv[1];
    errno=0; char *end; long watchdog=strtol(argv[2],&end,10);
    if (errno || !*argv[2] || *end || watchdog<0 || watchdog>INT_MAX) { lock_log("invalid-watchdog"); return 2; }
    app.watchdog=(int)watchdog;
    struct passwd *user=getpwuid(getuid());
    if (!user || !user->pw_name || !*user->pw_name) return 2;
    app.user=strdup(user->pw_name);
    if (!app.user) return 2;
    const char *locale=setlocale(LC_ALL,"");
    if (!locale) locale="C";
    app.rtl=!strncmp(locale,"ar",2)||!strncmp(locale,"he",2)||!strncmp(locale,"fa",2)||!strncmp(locale,"ur",2);
    app.secret=mmap(NULL,sizeof(*app.secret),PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (app.secret==MAP_FAILED) return 2;
    madvise(app.secret,sizeof(*app.secret),MADV_DONTDUMP);
    app.memory_locked=mlock(app.secret,sizeof(*app.secret))==0;
    if (!app.memory_locked) lock_log("secret-memory-unlocked");
    app.xkb=xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!app.xkb) { secret_clear(app.secret); munmap(app.secret,sizeof(*app.secret)); return 2; }
    xkb_context_set_log_level(app.xkb,XKB_LOG_LEVEL_CRITICAL);
    app.compose_table=xkb_compose_table_new_from_locale(app.xkb,locale,XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (!app.compose_table) lock_log("compose-unavailable");
    lock_log("stage-start");
    if (!wayland_start(&app)) locker_stop(&app,2,"wayland-unavailable");
    else control_open(&app);
    while (!app.done || app.auth) {
        if (stopped && !app.done) locker_stop(&app,2,"signal-exit");
        int64_t now=monotonic_ms();
        auth_step(&app,now);
        if (app.done) {
            if (app.auth) poll(NULL,0,10);
            continue;
        }
        if (app.error_until && now>=app.error_until) { app.error_until=0; locker_changed(&app); }
        if (app.releasing && now>=app.release_deadline) { locker_stop(&app,2,"release-timeout"); continue; }
        if (app.busy && now>=app.pulse_at) { app.pulse=!app.pulse; app.pulse_at=now+350; locker_changed(&app); }
        input_repeat(&app,now);
        if (wl_display_dispatch_pending(app.display)<0) { locker_stop(&app,2,"wayland-error"); continue; }
        wayland_redraw(&app);
        control_step(&app);
        if (app.done) continue;
        while (wl_display_prepare_read(app.display)) {
            if (wl_display_dispatch_pending(app.display)<0) { locker_stop(&app,2,"wayland-error"); break; }
        }
        if (app.done) continue;
        bool writable=false;
        if (wl_display_flush(app.display)<0) {
            if (errno==EAGAIN) writable=true;
            else { wl_display_cancel_read(app.display); locker_stop(&app,2,"wayland-flush-error"); continue; }
        }
        struct pollfd fds[]={
            {.fd=wl_display_get_fd(app.display),.events=POLLIN|(writable?POLLOUT:0)},
            {.fd=app.control,.events=POLLIN|((app.control_connecting || (app.locked && app.report_offset<7 && !app.releasing))?POLLOUT:0)},
            {.fd=auth_client_fd(app.auth),.events=(auth_client_wants_read(app.auth)?POLLIN:0)|(auth_client_wants_write(app.auth)?POLLOUT:0)},
            {.fd=signal_pipe[0],.events=POLLIN}
        };
        int rc=poll(fds,4,poll_delay(&app,monotonic_ms()));
        if (rc<0) {
            wl_display_cancel_read(app.display);
            if (errno!=EINTR) locker_stop(&app,2,"poll-error");
        } else if (fds[0].revents&(POLLIN|POLLHUP|POLLERR)) {
            if (wl_display_read_events(app.display)<0) locker_stop(&app,2,"wayland-disconnected");
        } else wl_display_cancel_read(app.display);
    }
    if (app.control>=0) close(app.control);
    wayland_finish(&app);
    xkb_compose_table_unref(app.compose_table); xkb_context_unref(app.xkb);
    secret_clear(app.secret);
    if (app.memory_locked) munlock(app.secret,sizeof(*app.secret));
    munmap(app.secret,sizeof(*app.secret)); lock_log("exit");
    free((void *)app.user);
    signal_fd=-1; close(signal_pipe[0]); close(signal_pipe[1]);
    return app.result;
}
