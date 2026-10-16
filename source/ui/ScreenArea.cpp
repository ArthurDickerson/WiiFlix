#include "ScreenArea.h"
#include "Ui.h"
#include "../core/Lang.h"
#include "../input/Input.h"
#include <stdio.h>
#include <ogc/lwp_watchdog.h>

bool ScreenArea::update()
{
    if (Input::isBackPressed()) return false;
    if (Input::isAJustPressed()) calCorner ^= 1;
    if (Input::isLPressed()) Ui::setSafeArea(0, 0, 0, 0);   /* [-] reset */

    /* Single press = 1 px; holding repeats after 350 ms, faster after 1 s */
    u32 held = Input::held();
    u32 dirs = held & (WPAD_BUTTON_UP | WPAD_BUTTON_DOWN | WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT);
    u64 now  = ticks_to_millisecs(gettime());
    int dx = 0, dy = 0, step = 1;
    if (Input::isLeftPressed())  dx = -1;
    if (Input::isRightPressed()) dx = +1;
    if (Input::isUpPressed())    dy = -1;
    if (Input::isDownPressed())  dy = +1;
    if (dx || dy) { calHeldSince = now; calLastStep = now; }
    else if (dirs && calHeldSince && now - calHeldSince > 350 && now - calLastStep > 40) {
        calLastStep = now;
        if (held & WPAD_BUTTON_LEFT)  dx = -1;
        if (held & WPAD_BUTTON_RIGHT) dx = +1;
        if (held & WPAD_BUTTON_UP)    dy = -1;
        if (held & WPAD_BUTTON_DOWN)  dy = +1;
        if (now - calHeldSince > 1000) step = 3;
    }
    if (!dirs && !(dx || dy)) calHeldSince = 0;
    if (!dx && !dy) return true;

    int l, t, r, b;
    Ui::safeArea(l, t, r, b);
    if (calCorner == 0) { l += dx * step; t += dy * step; }   /* moving the corner inward grows the margin */
    else                { r -= dx * step; b -= dy * step; }
    Ui::setSafeArea(l, t, r, b);
    return true;
}

void ScreenArea::render()
{
    const Ui::Palette& p = Ui::pal();
    int l, t, r, b;
    Ui::safeArea(l, t, r, b);

    Ui::background(false);

    Ui::card(110, 150, 420, 150, 18, 0.0f);
    Ui::textCentered(320, 166, tr("Screen Area"), 22, p.text);
    Ui::textCentered(320, 200, tr("Move each arrow until its tip sits right in"), 15, p.textDim);
    Ui::textCentered(320, 220, tr("the corner of your TV screen."), 15, p.textDim);
    char vals[96];
    snprintf(vals, sizeof(vals), tr("Left %d   \xc2\xb7   Top %d   \xc2\xb7   Right %d   \xc2\xb7   Bottom %d"), l, t, r, b);
    Ui::textCentered(320, 258, vals, 14, p.accentDark);

    /* bar first: the brackets must stay visible on top of it */
    static const Ui::Hint left[]  = { { "UD", N_("Move") }, { "A", N_("Switch") }, { "-", N_("Reset") } };
    static const Ui::Hint right[] = { { "B", N_("Done") } };
    Ui::bottomBar(left, 3, right, 1);

    /* Corner brackets on the very edges of the picture: the whole picture
     * moves with the setting, so they show where the TV cuts it. */
    const float ARM = 46, TH = 5;
    const float SL = Ui::screenLeft(), SR = Ui::screenRight();
    Ui::roundBorder(SL, 0, SR - SL, 480, 2, 1.5f, Ui::alpha(p.textDim, 0.6f));
    for (int c = 0; c < 2; ++c) {
        bool on = (c == calCorner);
        u32 col = on ? Ui::mix(p.accent, 0xFFFFFFFF, 1.0f - Ui::pulse()) : p.textDim;
        float cx = c == 0 ? SL : SR, cy = c == 0 ? 0.0f : 480.0f;
        float sx = c == 0 ? 1.0f : -1.0f, sy = c == 0 ? 1.0f : -1.0f;
        /* L-shaped bracket hugging the corner */
        Ui::roundRect(sx > 0 ? cx : cx - ARM, sy > 0 ? cy : cy - TH, ARM, TH, 1, col);
        Ui::roundRect(sx > 0 ? cx : cx - TH, sy > 0 ? cy : cy - ARM, TH, ARM, 1, col);
        /* arrow pointing into the corner */
        float ax = cx + sx * 26, ay = cy + sy * 26;
        Ui::triangle(cx + sx * 9, cy + sy * 9, ax + sx * 6, ay - sy * 8, ax - sx * 8, ay + sy * 6, col);
        Ui::roundRect(ax - 3 + sx * 2, ay - 3 + sy * 2, 6, 6, 3, col);
    }
}
