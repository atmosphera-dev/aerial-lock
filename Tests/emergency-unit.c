#define _GNU_SOURCE
#include "../emergency/locker.h"
#include "test-check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>

static int submissions, errors;
int64_t monotonic_ms(void) { return 1000; }
void lock_log(const char *reason) { (void)reason; }
void locker_error(struct locker *app,const char *reason) { (void)app;(void)reason; ++errors; }
void locker_changed(struct locker *app) { (void)app; }
void locker_submit(struct locker *app) { ++submissions; input_reset_all(app); }
void locker_cancel(struct locker *app) { secret_clear(app->secret); input_reset_all(app); }

static void layout(struct seat *s,const char *name,const char *options) {
    xkb_state_unref(s->state); xkb_keymap_unref(s->keymap);
    struct xkb_rule_names names={.layout=name,.options=options};
    s->keymap=xkb_keymap_new_from_names(s->app->xkb,&names,0); CHECK(s->keymap);
    s->state=xkb_state_new(s->keymap); CHECK(s->state);
    xkb_compose_state_reset(s->compose); secret_clear(s->app->secret);
}
int main(void) {
    struct secret secret={0};
    CHECK(secret_append(&secret,"a\xc3\xa9\xf0\x9f\x98\x80",7)); CHECK(secret.scalars==3);
    secret_backspace(&secret); CHECK(secret.length==3 && secret.scalars==2);
    for (size_t i=3;i<sizeof(secret.draft);++i) CHECK(!secret.draft[i]);
    secret_backspace(&secret); CHECK(secret.length==1 && secret.scalars==1);
    secret_backspace(&secret); CHECK(!secret.length && !secret.scalars);
    const char *bad[]={"\0","\xc0\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\xe2\x82"};
    size_t sizes[]={1,2,3,4,2};
    for (unsigned i=0;i<5;++i) CHECK(!secret_append(&secret,bad[i],sizes[i]));
    for (size_t i=0;i<PAM_MAX_RESP_SIZE-1;++i) CHECK(secret_append(&secret,"x",1));
    CHECK(!secret_append(&secret,"a",1)); CHECK(secret.length==PAM_MAX_RESP_SIZE-1);
    secret_clear(&secret);
    for (size_t i=0;i<sizeof(secret);++i) CHECK(!((unsigned char *)&secret)[i]);

    struct locker app={.secret=&secret,.locked=true};
    app.xkb=xkb_context_new(0); CHECK(app.xkb);
    app.compose_table=xkb_compose_table_new_from_locale(app.xkb,"C.UTF-8",0); CHECK(app.compose_table);
    struct seat seat={.app=&app,.focused=true,.rate=25,.delay=400}; app.seats=&seat;
    seat.compose=xkb_compose_state_new(app.compose_table,0); CHECK(seat.compose);
    layout(&seat,"us","compose:ralt");
    input_key(&seat,30,false); CHECK(!strcmp(secret.draft,"a"));
    xkb_state_update_mask(seat.state,1,0,0,0,0,0);
    input_key(&seat,30,false); CHECK(!strcmp(secret.draft,"aA"));
    xkb_state_update_mask(seat.state,0,0,2,0,0,0);
    input_key(&seat,30,false); CHECK(!strcmp(secret.draft,"aAA"));
    xkb_state_update_mask(seat.state,4,0,0,0,0,0);
    input_key(&seat,22,false); CHECK(!secret.length); /* Ctrl-U */
    xkb_state_update_mask(seat.state,0,0,0,0,0,0);
    input_key(&seat,100,false); input_key(&seat,40,false); input_key(&seat,18,false);
    CHECK(!strcmp(secret.draft,"\xc3\xa9") && secret.scalars==1); /* Compose ' e */
    input_key(&seat,14,false); CHECK(!secret.length);
    input_key(&seat,28,false); input_key(&seat,28,true); CHECK(submissions==1 && !seat.repeat_at);
    app.busy=true; input_key(&seat,30,false); CHECK(!secret.length);
    app.busy=false; app.locked=false; input_key(&seat,30,false); CHECK(!secret.length); app.locked=true;
    seat.focused=false; input_key(&seat,30,false); CHECK(!secret.length); seat.focused=true;
    layout(&seat,"de",NULL);
    input_key(&seat,21,false); CHECK(!strcmp(secret.draft,"z"));
    xkb_mod_index_t altgr=xkb_keymap_mod_get_index(seat.keymap,"Mod5"); CHECK(altgr<32);
    xkb_state_update_mask(seat.state,1u<<altgr,0,0,0,0,0);
    input_key(&seat,16,false); CHECK(!strcmp(secret.draft,"z@"));
    input_reset_all(&app); CHECK(!seat.repeat_at);
    CHECK(!errors);
    xkb_compose_state_unref(seat.compose); xkb_state_unref(seat.state); xkb_keymap_unref(seat.keymap);
    xkb_compose_table_unref(app.compose_table); xkb_context_unref(app.xkb);

    int w,h; size_t bytes;
    CHECK(!emergency_dimensions(0,1,1,&w,&h,&bytes));
    CHECK(!emergency_dimensions(UINT32_MAX,UINT32_MAX,2,&w,&h,&bytes));
    CHECK(!emergency_dimensions(1,1,0,&w,&h,&bytes));
    CHECK(emergency_dimensions(800,600,2,&w,&h,&bytes) && w==1600 && h==1200 && bytes==7680000);
    for (int scale=1;scale<=3;++scale) for (int rtl=0;rtl<=1;++rtl) {
        w=800*scale; h=600*scale;
        uint32_t *pixels=malloc((size_t)w*(size_t)h*4); CHECK(pixels);
        struct picture p={.rtl=rtl,.dots=PAM_MAX_RESP_SIZE};
        emergency_draw(pixels,w,h,scale,&p);
        struct box b=emergency_box(w,h,scale,rtl);
        CHECK(b.x*2+b.width==w && b.y*2+b.height==h);
        CHECK(b.slot_size==b.height);
        CHECK((rtl?b.slot_x:b.slot_x+b.slot_size)==(rtl?b.x:b.x+b.width));
        CHECK(pixels[0]==0xff000000 && pixels[(size_t)b.y*w+b.x]==0xffffffff);
        p.error=true; emergency_draw(pixels,w,h,scale,&p);
        CHECK(pixels[(size_t)b.y*w+b.x]==0xffff6666);
        for (size_t i=0;i<(size_t)w*(size_t)h;++i)
            CHECK(pixels[i]==0xff000000 || pixels[i]==0xffffffff || pixels[i]==0xffff6666 || pixels[i]==0xff242424);
        free(pixels);
    }
    puts("EMERGENCY_UNIT_PASS utf8 limits wipe layouts shift caps altgr compose repeat focus geometry pixels");
    return 0;
}
