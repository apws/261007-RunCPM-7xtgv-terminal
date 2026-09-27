#include "../src/terminal/terminal.h"
#include "../src/terminal/history_profiles.h"
#include <cassert>
#include <cstring>
#include <iostream>

static void feed(vt::Terminal &t, const char *s) { while (*s) t.feed(uint8_t(*s++)); }
static bool equal(vt::RGB a, vt::RGB b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
static void load(vt::HistoryProfiles &p, const char *s) {
    p.beginLoad(); while (*s) p.feed(uint8_t(*s++)); p.finishLoad();
}
static int selected = -1;
static void control(void *, vt::Terminal::ControlEvent event, uint8_t value,
                    uint16_t, uint16_t) {
    assert(event == vt::Terminal::Palette); selected = value;
}
int main() {
    // Exact RGB preservation in ANSI order, independent of new internal indices.
    const vt::RGB normal[] = {{100,100,100},{255,0,0},{0,242,0},{230,230,50},
        {0,0,255},{255,80,255},{76,242,242},{230,230,230}};
    const vt::RGB emphasis[] = {{0,0,0},{231,37,37},{21,200,47},{200,194,0},
        {37,89,221},{205,47,189},{0,184,194},{215,215,215}};
    vt::Terminal t;
    for (unsigned i=0; i<8; ++i) {
        char s[48]; t.reset();
        snprintf(s,sizeof(s),"\033[%umN\033[1mB\033[22mN\033[%umD",30+i,90+i);
        feed(t,s);
        assert(equal(t.foreground(0),normal[i]));
        assert(equal(t.foreground(1),emphasis[i]));
        assert(equal(t.foreground(2),normal[i]));
        assert(equal(t.foreground(3),emphasis[i]));
        assert((t.attrRAM[1] & 15) == ((t.attrRAM[0] & 7) | 8));
    }
    vt::HistoryProfiles p;
    load(p,"[global]\nmode_palette=2\nviewport_gutter_rgb=palette\n"
           "[palette2]\npaper0=1,2,3\nink00=7,8,9\nink08=17,18,19\n"
           "active_line_rgb=2,3,4\ncursor_rgb=5,6,7\nviewport_gutter_rgb=8,9,10\n"
           "viewport_active_gutter_rgb=11,12,13\n"
           "[bank3]\ncursor_rgb=33,44,55\nactive_line_rgb=palette\n");
    assert(!p.errors() && p.global().modePalette == 2);
    assert(equal(p.palette(2).paper[0],{1,2,3}));
    assert(equal(p.palette(2).ink[8],{17,18,19}));
    assert(equal(p.palette(2).ink[9],mixworx::Ink[9]));
    assert(!p.palette(2).activeGutterFollowsLine);
    assert(p.get(3).cursorOverride && !p.get(3).activeLineOverride);
    assert(!p.global().gutterOverride);
    t.palette = p.palette(2); t.reset(); feed(t,"X\033[1mY\033[7mZ");
    assert(equal(t.foreground(0),{7,8,9}) && equal(t.background(0),{1,2,3}));
    assert(equal(t.foreground(1),{17,18,19}));
    assert(equal(t.foreground(2),{1,2,3}) && equal(t.background(2),{17,18,19}));
    load(p,"[palette0]\nink00=20,30,40\nink00=999,1,2\nink16=1,2,3\n"
           "paper4=1,2,3\n[global]\nmode_palette=10\n"
           "[palette99]\nink00=1,2,3\n[palette1]\ncursor_rgb=4,5,6\n");
    assert(p.errors()==6 && p.errorLine(0).number==3);
    assert(equal(p.palette(0).ink[0],{20,30,40}));
    assert(equal(p.palette(1).cursor,{4,5,6}) && p.global().modePalette==0);
    load(p,"[global]\nviewport_active_gutter_rgb=1,2,3\n"
           "viewport_active_gutter_rgb=palette\n[bank0]\ncursor_rgb=1,2,3\ncursor_rgb=palette\n");
    assert(!p.errors() && !p.global().activeGutterOverride && !p.get(0).cursorOverride);
    // Dirty both staging slots, then reuse each. In-place reconstruction must
    // restore nonzero defaults as well as clear old palette/profile overrides.
    for (int slot = 0; slot < 2; ++slot) {
        load(p,"[global]\nmode_palette=9\nbacklight_level=3\ncrt_rampup_ms=900\n"
               "[palette9]\nink00=1,2,3\npaper0=4,5,6\n"
               "[bank3]\nname=CHANGED\ncursor_rgb=1,2,3\n");
        assert(!p.errors() && p.global().modePalette == 9);
    }
    for (int slot = 0; slot < 2; ++slot) {
        load(p,"");
        assert(!p.errors());
        assert(p.global().modePalette == 0 && p.global().backlightLevel == 16);
        assert(p.global().crtRampupMs == 0 && p.global().cursorBlinkMs == 500);
        assert(!strcmp(p.get(3).name,"TURBO") && !p.get(3).cursorOverride);
        assert(p.get(1).inputAdapter == vt::InputAdapterMbasicNative);
        assert(p.get(3).editingSurface == vt::EditingSurfaceScreen);
        for (uint8_t palette = 0; palette < vt::HistoryProfiles::Palettes; ++palette) {
            for (uint8_t ink = 0; ink < 16; ++ink)
                assert(equal(p.palette(palette).ink[ink],mixworx::Ink[ink]));
            for (uint8_t paper = 0; paper < 4; ++paper)
                assert(equal(p.palette(palette).paper[paper],mixworx::Paper[paper]));
        }
    }
    t.setControlSink(control,nullptr);
    for (bool ansi : {true,false}) {
        t.setMode(ansi); selected=-1; feed(t,"\033[>69;9v"); assert(selected==9);
        feed(t,"\033[>69;10v\033[>69v\033[>69;2;1v"); assert(selected==9);
        feed(t,"\033[>69;0v"); assert(selected==0);
    }
    std::cout << "palette RGB preservation, parser, inheritance, diagnostics and selectors passed\n";
}
