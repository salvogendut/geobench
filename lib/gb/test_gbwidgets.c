#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gb.h"

typedef struct {
    unsigned char x, y, w, h, pen;
} fill_call_t;

typedef struct {
    unsigned char x, y;
    const char *value;
} text_call_t;

static fill_call_t fills[8];
static fill_call_t frames[8];
static text_call_t texts[8];
static unsigned char nfill, nframe, ntext;

void gb_fill(unsigned char x, unsigned char y, unsigned char w,
             unsigned char h, unsigned char pen)
{
    fills[nfill++] = (fill_call_t){x, y, w, h, pen};
}

void gb_frame(unsigned char x, unsigned char y, unsigned char w,
              unsigned char h, unsigned char pen)
{
    frames[nframe++] = (fill_call_t){x, y, w, h, pen};
}

void gb_textbw(unsigned char x, unsigned char y, const char *text)
{
    texts[ntext++] = (text_call_t){x, y, text};
}

static void reset_calls(void)
{
    nfill = nframe = ntext = 0;
}

static void check(int ok, const char *name)
{
    if (!ok) {
        fprintf(stderr, "FAIL %s\n", name);
        exit(1);
    }
    printf("ok   %s\n", name);
}

int main(void)
{
    static const gb_action_t actions[2] = {
        { "Save", 8 },
        { "Cancel", 11 }
    };

    reset_calls();
    gb_button(10, 20, 20, 15, "Go", 0);
    check(nfill == 1 && fills[0].pen == 1, "button surface");
    check(nframe == 1 && frames[0].pen == 2, "button edge");
    check(ntext == 1 && texts[0].x == 18 && texts[0].y == 24 &&
          !strcmp(texts[0].value, "Go"),
          "button centred label");

    reset_calls();
    gb_button(10, 20, 20, 15, "Go", GB_WIDGET_PRESSED);
    check(nframe == 1 && frames[0].pen == 3 && texts[0].y == 25,
          "pressed button accent and inset");

    reset_calls();
    gb_button(10, 20, 20, 15, "Go", GB_WIDGET_DISABLED);
    check(nframe == 1 && frames[0].pen == 1,
          "disabled button suppresses edge");
    check(gb_button_hit(10, 20, 20, 15, 12, 22, 0),
          "enabled button accepts hit");
    check(!gb_button_hit(10, 20, 20, 15, 12, 22, GB_WIDGET_DISABLED),
          "disabled button rejects hit");

    reset_calls();
    gb_field(2, 8, 30, 13, "URL", GB_WIDGET_FOCUSED);
    check(nframe == 1 && frames[0].pen == 3, "focused field accent");
    check(ntext == 1 && texts[0].x == 3 && texts[0].y == 11,
          "field text inset");

    reset_calls();
    gb_field(2, 8, 30, 13, "URL", GB_WIDGET_DISABLED);
    check(nframe == 1 && frames[0].pen == 1,
          "disabled field suppresses edge");

    check(gb_widget_hit(4, 5, 10, 8, 4, 5), "widget includes top-left");
    check(!gb_widget_hit(4, 5, 10, 8, 14, 12), "widget excludes right edge");

    reset_calls();
    gb_actions(4, 12, actions, 2, 2);
    check(nfill == 0 && nframe == 2 &&
          frames[0].x == 4 && frames[0].w == 8 &&
          frames[1].x == 14 && frames[1].w == 11,
          "compact action row uses caller widths and gap");
    check(ntext == 2 && texts[0].x == 5 && texts[0].y == 13 &&
          !strcmp(texts[1].value, "Cancel"),
          "compact action row labels a surface panel");
    check(gb_actions_hit(4, 12, actions, 2, 2, 6, 14) == 0 &&
          gb_actions_hit(4, 12, actions, 2, 2, 16, 14) == 1,
          "compact action row identifies actions");
    check(gb_actions_hit(4, 12, actions, 2, 2, 13, 14) ==
              GB_ACTION_NONE &&
          gb_actions_hit(4, 12, actions, 2, 2, 6, 22) ==
              GB_ACTION_NONE,
          "compact action row rejects gaps and lower edge");
    check(gb_actions_hit(250, 250, actions, 1, 0, 1, 252) ==
              GB_ACTION_NONE &&
          gb_actions_hit(4, 250, actions, 1, 0, 6, 1) ==
              GB_ACTION_NONE,
          "compact action row does not wrap screen edges");

    check(gb_vscroll_hit(1, 10, 3, 80, 10, 40, 8, 2, 12,
                         GB_WIDGET_ARROWS) == GB_SCROLL_UP,
          "scroll up arrow");
    check(gb_vscroll_hit(1, 10, 3, 80, 10, 40, 8, 2, 88,
                         GB_WIDGET_ARROWS) == GB_SCROLL_DOWN,
          "scroll down arrow");
    check(gb_vscroll_hit(1, 10, 3, 80, 10, 40, 8, 2, 20,
                         GB_WIDGET_ARROWS) == GB_SCROLL_PAGE_UP,
          "scroll page up");
    check(gb_vscroll_hit(1, 10, 3, 80, 10, 40, 8, 2, 70,
                         GB_WIDGET_ARROWS) == GB_SCROLL_PAGE_DOWN,
          "scroll page down");

    reset_calls();
    gb_vscroll(1, 10, 3, 80, 10, 40, 8, GB_WIDGET_ARROWS);
    check(nfill == 4 && ntext == 2 &&
          !strcmp(texts[1].value, GLYPH_TRI_DOWN),
          "scrollbar track thumb and arrows");

    reset_calls();
    gb_vscroll16(1, 10, 3, 80, 300, 1000, 160);
    check(nfill == 2 && fills[1].pen == 3 && fills[1].y > 10,
          "large vertical scrollbar");
    check(gb_vscroll16_value(10, 80, 1000, 160, 10) == 0,
          "large vertical scrollbar maps top");
    check(gb_vscroll16_value(10, 80, 1000, 160, 89) == 840,
          "large vertical scrollbar maps bottom");
    reset_calls();
    gb_vscroll16(1, 10, 3, 200, 0, 512, 400);
    check(nfill == 2 && fills[1].h == 154, "half-zoom thumb uses a 16-bit page count");

    reset_calls();
    gb_hscroll16(8, 12, 40, 6, 30, 100, 25);
    check(nfill == 2 && fills[1].pen == 3 && fills[1].x > 8,
          "large horizontal scrollbar");
    check(gb_hscroll16_value(8, 40, 100, 25, 47) == 75,
          "large horizontal scrollbar maps right edge");

    reset_calls();
    gb_toggle(4, 6, 20, 10, "Enabled",
              GB_WIDGET_CHECKED | GB_WIDGET_FOCUSED);
    check(nfill == 1 && nframe == 1 && frames[0].pen == 3,
          "focused toggle frame");
    check(ntext == 2 && !strcmp(texts[0].value, "x") &&
          !strcmp(texts[1].value, "Enabled"),
          "checked toggle mark and label");
    check(gb_toggle_hit(4, 6, 20, 10, 23, 15),
          "toggle includes bottom-right pixel");
    check(!gb_toggle_hit(4, 6, 20, 10, 24, 15),
          "toggle excludes right edge");

    reset_calls();
    gb_stepper(10, 20, 16, 10, "12", 0);
    check(nfill == 1 && nframe == 3 && ntext == 3,
          "stepper draws three parts");
    check(!strcmp(texts[0].value, "-") && !strcmp(texts[1].value, "12") &&
          !strcmp(texts[2].value, "+"),
          "stepper labels");
    check(gb_stepper_hit(10, 20, 16, 10, 11, 24) == GB_STEPPER_DEC,
          "stepper decrement hit");
    check(gb_stepper_hit(10, 20, 16, 10, 18, 24) == GB_STEPPER_VALUE,
          "stepper value hit");
    check(gb_stepper_hit(10, 20, 16, 10, 24, 24) == GB_STEPPER_INC,
          "stepper increment hit");

    reset_calls();
    gb_select(5, 7, 26, 10, "DEFAULT", 0);
    check(nfill == 1 && nframe == 1 && ntext == 2 &&
          !strcmp(texts[0].value, "DEFAULT") &&
          !strcmp(texts[1].value, ">"),
          "selector value and affordance");
    check(gb_select_hit(5, 7, 26, 10, 30, 16),
          "selector hit");

    reset_calls();
    gb_slider(8, 12, 20, 9, 5, 10, GB_WIDGET_FOCUSED);
    check(nfill == 3 && nframe == 1 && frames[0].pen == 3,
          "focused slider track and thumb");
    check(gb_slider_hit(8, 12, 20, 9, 27, 20),
          "slider hit");
    check(gb_slider_value(8, 20, 10, 9) == 0,
          "slider maps left edge");
    check(gb_slider_value(8, 20, 10, 25) == 10,
          "slider maps right edge");
    check(gb_slider_value(8, 20, 10, 17) == 5,
          "slider maps midpoint");
    return 0;
}
