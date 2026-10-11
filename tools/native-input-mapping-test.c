/* Device transitions that are difficult to reproduce reliably by hand. MIT. */
#include <assert.h>
#define POKEMMOPS5_INPUT_TEST
#include "../app/runtime/wine_devices.c"
struct queued { unsigned type, code, flags; int x, y; } captured[1024];
static unsigned queued;
static int queue_accept = 1;
int pokemmops5_input_queue(unsigned type, unsigned code, unsigned flags, int x, int y)
{
    if (!queue_accept) return 0;
    assert(queued < 1024);
    if (type == 3) pokemmops5_cursor_sync(x,y,1920,1080);
    captured[queued++] = (struct queued){type,code,flags,x,y};
    return 1;
}

enum {
    N_L3 = 0x2, N_R3 = 0x4, N_UP = 0x10, N_RIGHT = 0x20,
    N_DOWN = 0x40, N_LEFT = 0x80, N_R1 = 0x800,
    N_CROSS = 0x4000, N_TOUCH = 0x100000, N_TAKEN = 0x80000000u
};
enum {
    X_UP = 1, X_DOWN = 2, X_LEFT = 4, X_RIGHT = 8,
    X_L3 = 0x40, X_R3 = 0x80, X_R1 = 0x200, X_A = 0x1000
};

static void pad_record_after(struct pad_data *pad, uint32_t buttons, unsigned ms)
{
    osk_test_now += ms;
    pad->buttons = buttons;
    pad_sample(pad);
}

static void sample_pad_after(struct pad_data *pad, uint32_t buttons, unsigned ms)
{
    pad_record_after(pad, buttons, ms);
    tap_click_tick();
}

static void sample_pad(struct pad_data *pad, uint32_t buttons)
{
    sample_pad_after(pad, buttons, 4);
}
static void tap_tick_after(unsigned ms)
{
    osk_test_now += ms;
    tap_click_tick();
}


static unsigned event_count(unsigned type, unsigned flags)
{
    unsigned count = 0;
    for (unsigned i = 0; i < queued; i++)
        if (captured[i].type == type && captured[i].flags == flags) count++;
    return count;
}

int main(void)
{
    struct pad_data p = {.connected=1,.lx=128,.ly=128,.rx=128,.ry=128};
    struct gamepad_state state;
    struct cursor_point point;
    unsigned before, packet;
    uint64_t activity, moves;

    /* Left stick, Cross, and ordinary buttons always remain XInput. The right
     * stick always belongs to the pointer and is absent from XInput. */
    sample_pad(&p, 0);
    pokemmops5_cursor_sync(1910,5,1920,1080);
    p.lx=0; p.ly=255; p.rx=255; p.ry=0; p.r2=255;
    sample_pad(&p, N_CROSS|N_R1|N_UP);
    assert(!queued && cursor_rx==255 && cursor_ry==0);
    assert(!pokemmops5_gamepad_state(0,&state));
    assert(state.pad.buttons==(X_A|X_R1|X_UP) && state.pad.lx==-32768 &&
           state.pad.ly==-32767 && !state.pad.rx && !state.pad.ry && state.pad.rt==255);
    packet=state.packet; sample_pad(&p, N_CROSS|N_R1|N_UP); assert(gamepad.packet==packet);
    cursor_stick_frame_at=0; cursor_remainder_x=cursor_remainder_y=0;
    activity=cursor_activity;
    cursor_stick_frame(1000000); cursor_stick_frame(1100000);
    assert(queued==1 && captured[0].type==3 && (int32_t)captured[0].code>0 &&
           (int32_t)captured[0].flags<0 && captured[0].x==1919 && captured[0].y==0 &&
           cursor_activity==activity+1);                            /* clamped motion is activity */
    const uint32_t native_dpad[] = {N_DOWN,N_LEFT,N_RIGHT};
    const uint16_t xbox_dpad[] = {X_DOWN,X_LEFT,X_RIGHT};
    for (unsigned i=0; i<3; i++)
    {
        queued=0; pokemmops5_cursor_sync(960,540,1920,1080);
        p.rx=255; p.ry=128;
        sample_pad(&p,N_CROSS|native_dpad[i]);
        assert(gamepad.pad.buttons==(X_A|xbox_dpad[i]) &&
               gamepad.pad.lx==-32768 && gamepad.pad.ly==-32767 &&
               !gamepad.pad.rx && !gamepad.pad.ry);
        cursor_stick_frame_at=0; cursor_remainder_x=cursor_remainder_y=0;
        const uint64_t frame=(uint64_t)(12+i*2)*100000;
        cursor_stick_frame(frame); cursor_stick_frame(frame+100000);
        assert(queued==1 && captured[0].type==3);                    /* no keyboard/mouse D-pad duplicate */
    }
    p.rx=p.ry=128; queued=0; activity=cursor_activity;
    sample_pad(&p, N_CROSS|N_R1|N_UP);
    cursor_stick_frame_at=0; cursor_remainder_x=cursor_remainder_y=0;
    cursor_stick_frame(2000000); cursor_stick_frame(2100000);
    assert(!queued && cursor_activity==activity);                    /* left stick/Cross never move or wake it */
    assert(gamepad.pad.buttons==(X_A|X_R1|X_UP) && gamepad.pad.lx==-32768 &&
           gamepad.pad.ly==-32767 && !gamepad.pad.rx && !gamepad.pad.ry);
    assert(pokemmops5_gamepad_state(1,&state)==1167 && pokemmops5_gamepad_state(4,&state)==160);
    p.lx=p.ly=128; p.r2=0; sample_pad(&p, 0);
    /* Neutral physical axes gain an analog D-pad assist while retaining every
     * digital bit. Opposites cancel, and either physically deflected axis gives
     * both physical axes priority. */
    const uint32_t assist_native[] = {N_UP,N_DOWN,N_LEFT,N_RIGHT};
    const uint16_t assist_xbox[] = {X_UP,X_DOWN,X_LEFT,X_RIGHT};
    const int16_t assist_lx[] = {0,0,-32768,32767};
    const int16_t assist_ly[] = {32767,-32768,0,0};
    const uint64_t assist_before=dpad_assist_samples;
    queued=0; activity=cursor_activity;
    for (unsigned i=0; i<4; i++)
    {
        sample_pad(&p,assist_native[i]);
        assert(gamepad.pad.buttons==assist_xbox[i] &&
               gamepad.pad.lx==assist_lx[i] && gamepad.pad.ly==assist_ly[i]);
    }
    sample_pad(&p,N_LEFT|N_RIGHT);
    assert(gamepad.pad.buttons==(X_LEFT|X_RIGHT) && !gamepad.pad.lx && !gamepad.pad.ly);
    sample_pad(&p,N_UP|N_DOWN);
    assert(gamepad.pad.buttons==(X_UP|X_DOWN) && !gamepad.pad.lx && !gamepad.pad.ly);
    p.lx=104; p.ly=152; sample_pad(&p,0);                           /* no assist means no added dead zone */
    assert(gamepad.pad.lx==axis(p.lx,0) && gamepad.pad.ly==axis(p.ly,1));
    p.lx=104; p.ly=152; sample_pad(&p,N_RIGHT);                     /* inclusive ±24 boundary */
    assert(gamepad.pad.buttons==X_RIGHT && gamepad.pad.lx==32767 && !gamepad.pad.ly);
    p.lx=103; p.ly=128; sample_pad(&p,N_RIGHT|N_UP);                /* one axis at -25: physical wins */
    assert(gamepad.pad.buttons==(X_RIGHT|X_UP) && gamepad.pad.lx==-6400 && !gamepad.pad.ly);
    assert(dpad_assist_samples==assist_before+7 && !queued && cursor_activity==activity);
    p.lx=p.ly=128; sample_pad(&p,0);

    /* A mechanical press is primary with zero or one finger regardless of
     * position; exactly two fingers select secondary. */
    const uint64_t primary_before=physical_primary_presses;
    const uint64_t secondary_before=physical_secondary_presses;
    pokemmops5_cursor_sync(960,540,1920,1080); queued=0;
    p.touches=1; p.touch[0]=(struct touch){100,100,1,{0}};
    sample_pad(&p, 0);
    p.touch[0].x+=10; p.touch[0].y-=5; sample_pad(&p, 0);
    assert(queued==1 && captured[0].type==3 && captured[0].code==20 &&
           captured[0].flags==(uint32_t)-10 && captured[0].x==980 && captured[0].y==530);
    sample_pad(&p, N_TOUCH);
    assert(event_count(4,2)==1 && touch_click==1);
    p.touch[0].x=1500; sample_pad(&p, N_TOUCH); assert(touch_click==1);
    sample_pad(&p, 0); p.touches=0; sample_pad(&p, 0); tap_click_tick();
    assert(event_count(4,2)==1 && event_count(4,4)==1);              /* physical press canceled the tap */
    queued=0;
    p.touches=1; p.touch[0]=(struct touch){1500,200,2,{0}};
    sample_pad(&p,N_TOUCH); sample_pad(&p,0); p.touches=0; sample_pad(&p,0);
    assert(event_count(4,2)==1 && event_count(4,4)==1 &&
           !event_count(4,8) && physical_primary_presses==primary_before+2);
    queued=0;
    sample_pad(&p,N_TOUCH); sample_pad(&p,0);
    assert(event_count(4,2)==1 && event_count(4,4)==1 &&
           physical_primary_presses==primary_before+3);
    queued=0;
    p.touches=2;
    p.touch[0]=(struct touch){200,200,3,{0}};
    p.touch[1]=(struct touch){300,200,4,{0}};
    sample_pad(&p,N_TOUCH); sample_pad(&p,0); p.touches=0; sample_pad(&p,0);
    assert(event_count(4,8)==1 && event_count(4,16)==1 && !event_count(4,2) &&
           physical_secondary_presses==secondary_before+1);

    /* A one-finger release at both recognition limits holds primary down for
     * the full 80 ms; release starts at the exact boundary. */
    queued=0;
    const uint64_t recognized_before=taps_recognized;
    const uint64_t down_before=left_down_edges, up_before=left_up_edges;
    p.touches=1; p.touch[0]=(struct touch){400,400,4,{0}}; sample_pad(&p,0);
    p.touch[0].x=412; p.touch[0].y=388; sample_pad(&p,0);
    p.touches=0; sample_pad_after(&p,0,196);                         /* exactly 200 ms since contact */
    assert(event_count(4,2)==1 && event_count(4,4)==0 && tap_click_down &&
           taps_recognized==recognized_before+1 && left_down_edges==down_before+1);
    tap_tick_after(79);
    assert(event_count(4,4)==0 && tap_click_down && mouse_held[MOUSE_TAP_SOURCE]);
    tap_tick_after(1);
    assert(event_count(4,2)==1 && event_count(4,4)==1 && !tap_click_down &&
           !tap_click_pending && left_up_edges==up_before+1);

    /* Every cancellation is sticky until all fingers lift. */
    queued=0;
    p.touches=1; p.touch[0]=(struct touch){500,500,5,{0}}; sample_pad(&p,0);
    p.touch[0].x=513; sample_pad(&p,0); p.touch[0].x=500; sample_pad(&p,0);
    p.touches=0; sample_pad(&p,0); tap_click_tick();
    assert(!event_count(4,2));                                      /* excess X excursion */
    p.touches=1; p.touch[0]=(struct touch){500,500,6,{0}}; sample_pad(&p,0);
    p.touches=0; sample_pad_after(&p,0,201); tap_click_tick();
    assert(!event_count(4,2));                                      /* long hold */
    p.touches=1; p.touch[0]=(struct touch){500,500,7,{0}}; sample_pad(&p,0);
    p.touches=2; p.touch[1]=(struct touch){600,600,8,{0}}; sample_pad(&p,0);
    p.touches=1; sample_pad(&p,0); p.touches=0; sample_pad(&p,0); tap_click_tick();
    assert(!event_count(4,2));                                      /* multiple fingers */
    p.touches=1; p.touch[0]=(struct touch){500,500,9,{0}}; sample_pad(&p,0);
    p.touch[0].id=10; sample_pad(&p,0); p.touches=0; sample_pad(&p,0); tap_click_tick();
    assert(!event_count(4,2));                                      /* touch-id replacement */
    p.touches=1; p.touch[0]=(struct touch){500,500,11,{0}}; sample_pad(&p,0);
    osk.state=OSK_RUNNING; sample_pad(&p,0); p.touches=0; sample_pad(&p,0);
    osk.state=OSK_IDLE; osk.previous=osk.pending=osk.swallowed=osk.tapping=0;
    sample_pad(&p,0); tap_click_tick();
    assert(!event_count(4,2));                                      /* OSK ownership */
    p.touches=1; p.touch[0]=(struct touch){500,500,12,{0}}; sample_pad(&p,0);
    sample_pad(&p,N_TAKEN); p.touches=0; sample_pad(&p,N_TAKEN); sample_pad(&p,0);
    tap_click_tick(); assert(!event_count(4,2));                     /* system interception */
    p.touches=1; p.touch[0]=(struct touch){500,500,13,{0}}; sample_pad(&p,0);
    p.connected=0; sample_pad(&p,0); p.connected=1; p.touches=0; sample_pad(&p,0);
    tap_click_tick(); assert(!event_count(4,2));                     /* disconnect */

    /* Physical press plus lift cannot append a synthetic click. */
    queued=0; p.touches=1; p.touch[0]=(struct touch){300,300,14,{0}};
    sample_pad(&p,0); sample_pad(&p,N_TOUCH); sample_pad(&p,0);
    p.touches=0; sample_pad(&p,0); tap_click_tick();
    assert(event_count(4,2)==1 && event_count(4,4)==1 && !tap_click_pending && !tap_click_down);

    /* Rapid taps batched before a device tick remain distinct primary pulses,
     * including a tap on the physical pad's right half. */
    queued=0;
    p.touches=1; p.touch[0]=(struct touch){300,300,15,{0}}; pad_record_after(&p,0,4);
    p.touches=0; pad_record_after(&p,0,4);
    p.touches=1; p.touch[0]=(struct touch){1500,300,16,{0}}; pad_record_after(&p,0,4);
    p.touches=0; pad_record_after(&p,0,4);
    assert(tap_click_pending==2 && !tap_click_down && !queued);
    tap_click_tick(); tap_tick_after(80); tap_click_tick(); tap_tick_after(80);
    assert(queued==4 && captured[0].flags==2 && captured[1].flags==4 &&
           captured[2].flags==2 && captured[3].flags==4 &&
           !tap_click_pending && !tap_click_down);

    /* USB, physical-pad, and virtual-tap ownership aggregate without an early
     * release. A tap while USB owns left cannot release or duplicate it. */
    struct mouse_data m={.connected=1,.buttons=1};
    queued=0; mouse_sample(0,&m);
    assert(queued==1 && captured[0].flags==2 && (buttons_held&1));
    p.touches=1; p.touch[0]=(struct touch){200,200,17,{0}};
    sample_pad(&p,0); sample_pad(&p,N_TOUCH); sample_pad(&p,0);
    p.touches=0; sample_pad(&p,0);
    p.touches=1; p.touch[0]=(struct touch){200,200,18,{0}};
    sample_pad(&p,0); p.touches=0; sample_pad(&p,0); tap_tick_after(80);
    assert(queued==1 && (buttons_held&1) && !mouse_held[MOUSE_PAD_SOURCE] &&
           !mouse_held[MOUSE_TAP_SOURCE]);
    m.buttons=0; mouse_sample(0,&m);
    assert(queued==2 && captured[1].flags==4 && !(buttons_held&1));

    /* A rejected synthetic release stays pending across ticks; a rejected down
     * preserves the queued tap and starts its full pulse when capacity returns. */
    queued=0;
    p.touches=1; p.touch[0]=(struct touch){250,250,19,{0}}; sample_pad(&p,0);
    p.touches=0; sample_pad(&p,0);
    assert(queued==1 && captured[0].flags==2 && tap_click_down && (buttons_held&1));
    queue_accept=0; tap_tick_after(80); tap_click_tick();
    assert(queued==1 && tap_click_down && (buttons_held&1) && !mouse_held[MOUSE_TAP_SOURCE]);
    queue_accept=1; tap_click_tick();
    assert(queued==2 && captured[1].flags==4 && !tap_click_down && !(buttons_held&1));
    p.touches=1; p.touch[0]=(struct touch){250,250,20,{0}}; sample_pad(&p,0);
    queue_accept=0; p.touches=0; sample_pad(&p,0);
    assert(!tap_click_down && tap_click_pending==1 && !(buttons_held&1));
    queue_accept=1; tap_click_tick(); tap_tick_after(80);
    assert(queued==4 && captured[2].flags==2 && captured[3].flags==4 &&
           !tap_click_down && !tap_click_pending && !(buttons_held&1));
    /* Interception cancels a recognized tap whose down has not been accepted,
     * but an accepted down always drains its release through queue pressure. */
    queued=0;
    p.touches=1; p.touch[0]=(struct touch){250,250,22,{0}}; sample_pad(&p,0);
    queue_accept=0; p.touches=0; sample_pad(&p,0);
    assert(tap_click_pending==1 && !tap_click_down &&
           mouse_held[MOUSE_TAP_SOURCE] && !(buttons_held&1) && !queued);
    pad_record_after(&p,N_TAKEN,4);
    assert(!tap_click_pending && tap_click_down);
    tap_click_tick();
    assert(!tap_click_down && !mouse_held[MOUSE_TAP_SOURCE] && !(buttons_held&1));
    queue_accept=1; tap_click_tick();
    assert(!queued);                                                /* no delayed down after interception */
    sample_pad(&p,0);

    p.touches=1; p.touch[0]=(struct touch){250,250,23,{0}}; sample_pad(&p,0);
    p.touches=0; sample_pad(&p,0);
    assert(queued==1 && captured[0].flags==2 && tap_click_down && (buttons_held&1) &&
           tap_click_release_at > OSK_NOW());
    pad_record_after(&p,N_TAKEN,4);
    assert(!tap_click_release_at);                                  /* interruption overrides the 80 ms hold */
    queue_accept=0; tap_click_tick();
    assert(queued==1 && tap_click_down && (buttons_held&1) &&
           !mouse_held[MOUSE_TAP_SOURCE]);
    queue_accept=1; tap_click_tick();
    assert(queued==2 && captured[1].flags==4 && !tap_click_down && !(buttons_held&1));
    sample_pad(&p,0);


    /* Refused movement neither publishes the shared point nor records activity. */
    struct mouse_data refused_move = {.connected=1,.x=-500,.y=500};
    pokemmops5_cursor_sync(400,300,1920,1080);
    before=queued; moves=mouse_moves; activity=cursor_activity;
    queue_accept=0; mouse_sample(7,&refused_move); queue_accept=1;
    point=cursor_position();
    assert(queued==before && mouse_moves==moves && cursor_activity==activity &&
           point.x==400 && point.y==300);

    /* The private idle policy does not age before the first successful display
     * window, hides at the exact boundary, wakes for every native producer,
     * and grants a full interval after the final accepted release. */
    struct cursor_idle_policy idle={0};
    assert(cursor_idle_policy_visible(&idle,999999,cursor_activity,0) && !idle.initialized);
    cursor_idle_policy_begin(&idle,100,cursor_activity);
    assert(cursor_idle_policy_visible(&idle,3099,cursor_activity,0));
    assert(!cursor_idle_policy_visible(&idle,3100,cursor_activity,0));
    struct cursor_idle_policy requested_idle={0};
    cursor_idle_policy_begin(&requested_idle,0,cursor_activity);
    before=queued; queue_accept=0;
    m=(struct mouse_data){.connected=1,.buttons=1}; mouse_sample(1,&m);
    assert(queued==before && !(buttons_held&1) && (mouse_buttons_wanted()&1));
    assert(cursor_idle_policy_visible(&requested_idle,CURSOR_IDLE_TIMEOUT_MS,
                                      cursor_activity,buttons_held|mouse_buttons_wanted()));
    m.buttons=0; mouse_sample(1,&m); queue_accept=1;
    assert(!(mouse_buttons_wanted()&1));


    uint64_t now=3100;
    queued=0; pokemmops5_cursor_sync(1919,1079,1920,1080);
    p.rx=p.ry=255; sample_pad(&p,0);
    cursor_stick_frame_at=0; cursor_remainder_x=cursor_remainder_y=0;
    activity=cursor_activity; cursor_stick_frame(3000000); cursor_stick_frame(3100000);
    assert(cursor_activity==activity+1 && captured[0].x==1919 && captured[0].y==1079);
    assert(cursor_idle_policy_visible(&idle,now,cursor_activity,buttons_held));
    now+=CURSOR_IDLE_TIMEOUT_MS;
    assert(!cursor_idle_policy_visible(&idle,now,cursor_activity,buttons_held));

    queued=0; p.rx=p.ry=128; p.touches=1; p.touch[0]=(struct touch){100,100,21,{0}};
    sample_pad(&p,0); activity=cursor_activity; p.touch[0].x=113; sample_pad(&p,0);
    assert(cursor_activity==activity+1);
    assert(cursor_idle_policy_visible(&idle,now,cursor_activity,buttons_held));
    p.touches=0; sample_pad(&p,0);                           /* canceled by excursion, no tap */
    now+=CURSOR_IDLE_TIMEOUT_MS;
    assert(!cursor_idle_policy_visible(&idle,now,cursor_activity,buttons_held));

    queued=0; pokemmops5_cursor_sync(0,0,1920,1080);
    m=(struct mouse_data){.connected=1,.x=-4};
    activity=cursor_activity; mouse_sample(0,&m);
    assert(cursor_activity==activity+1 && captured[0].x==0);
    assert(cursor_idle_policy_visible(&idle,now,cursor_activity,buttons_held));
    now+=CURSOR_IDLE_TIMEOUT_MS;
    assert(!cursor_idle_policy_visible(&idle,now,cursor_activity,buttons_held));

    m=(struct mouse_data){.connected=1,.buttons=1};
    activity=cursor_activity; mouse_sample(0,&m);
    assert(cursor_activity==activity+1 && (buttons_held&1));
    assert(cursor_idle_policy_visible(&idle,now+10000,cursor_activity,buttons_held));
    now+=10000; m.buttons=0; activity=cursor_activity; mouse_sample(0,&m);
    assert(cursor_activity==activity+1 && !(buttons_held&1));
    assert(cursor_idle_policy_visible(&idle,now,cursor_activity,0));
    assert(cursor_idle_policy_visible(&idle,now+CURSOR_IDLE_TIMEOUT_MS-1,cursor_activity,0));
    assert(!cursor_idle_policy_visible(&idle,now+CURSOR_IDLE_TIMEOUT_MS,cursor_activity,0));
    now+=CURSOR_IDLE_TIMEOUT_MS;

    m=(struct mouse_data){.connected=1,.wheel=-1};
    activity=cursor_activity; mouse_sample(0,&m);
    assert(cursor_activity==activity+1 && captured[queued-1].flags==0x800);
    assert(cursor_idle_policy_visible(&idle,now,cursor_activity,0));
    now+=CURSOR_IDLE_TIMEOUT_MS;
    assert(!cursor_idle_policy_visible(&idle,now,cursor_activity,0));

    queued=0; activity=cursor_activity;
    p.lx=0; p.ly=255; p.rx=p.ry=128; sample_pad(&p,N_CROSS);
    cursor_stick_frame_at=0; cursor_remainder_x=cursor_remainder_y=0;
    cursor_stick_frame(4000000); cursor_stick_frame(4100000);
    assert(!queued && cursor_activity==activity &&
           !cursor_idle_policy_visible(&idle,now,cursor_activity,0));
    assert(gamepad.pad.buttons==X_A && gamepad.pad.lx==-32768 && gamepad.pad.ly==-32767);

    /* Keyboard aggregation and interception remain unchanged. */
    struct keyboard_data k={.connected=1,.modifiers=2,.keys={4}};
    queued=0; keyboard_sample(0,&k);
    assert(queued==2 && captured[0].type==1 && captured[0].code==0xa0 &&
           captured[1].type==1 && captured[1].code=='A');
    keyboard_sample(1,&k); assert(queued==2);
    k.connected=0; keyboard_sample(0,&k); assert(queued==2);
    keyboard_sample(1,&k); assert(queued==4 && captured[2].flags==2 && captured[3].flags==2);

    puts("PASS D-pad assist, always-on right-stick cursor, 80 ms taps, click routing, retry, and idle");
}
