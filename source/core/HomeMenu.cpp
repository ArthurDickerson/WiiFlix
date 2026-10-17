/* The Wii-style HOME Menu (Wii Menu / Reset). */
#include "AppInternal.h"
#include "Input.h"
#include "MusicBGM.h"
#include "SoundFX.h"
#include "Text.h"
#include "Utils.h"
#include "Lang.h"
#include "../ui/Ui.h"
#include <grrlib.h>
#include <wiiuse/wpad.h>
#include <cmath>
#include <stdio.h>
#include <ogc/lwp_watchdog.h>

/* -----------------------------------------------------------------------
 * The standard HOME Menu every Wii channel shows, drawn with the app's own
 * shapes: a dark bar at the top ("HOME Menu", pointing at it closes), the
 * Wii Menu and Reset buttons in the middle, a dark bar at the bottom with
 * the Wii Remotes' batteries, and a Yes / No question before leaving.  It
 * keeps the Wii's own light blue, not WiiFlix's orange.
 * ----------------------------------------------------------------------- */
namespace {

const u32 HM_BAR_TOP = 0x30343AF0, HM_BAR_BOTTOM = 0x101214F0;
const u32 HM_BLUE    = 0x34BEEDFF;              /* the Wii's highlight */
const u32 HM_INK     = 0x3A3A3AFF;              /* text on the white buttons */
const float TOP_H = 72, BOTTOM_Y = 384;

/* White glossy button with a light blue edge; hover: bigger, a blue glow */
void hmButton(float cx, float cy, float w, float h, const char* label, int size, float hot)
{
    const float g = 1.0f + 0.08f * hot;
    w *= g; h *= g;
    const float x = cx - w * 0.5f, y = cy - h * 0.5f, r = 18 * g;
    Ui::shadow(x, y + 4, w, h, r, 8.0f, 0x00000080);
    if (hot > 0.0f) Ui::shadow(x - 4, y - 4, w + 8, h + 8, r + 4, 10.0f, Ui::alpha(HM_BLUE, 0.8f * hot));
    Ui::roundRect(x, y, w, h, r, 0xFFFFFFFF, 0xD9ECF7FF);
    Ui::roundRect(x + 10, y + 4, w - 20, h * 0.4f, r * 0.6f, 0xFFFFFFB0, 0xFFFFFF00);   /* gloss */
    Ui::roundBorder(x, y, w, h, r, 2.0f + 1.5f * hot, Ui::mix(0x8FD3EEFF, HM_BLUE, hot));
    Ui::textCentered(cx, cy - size * 0.55f, tr(label), (int)(size * g), HM_INK);
}

/* A dark bar across the screen, its light blue edge on the side of the
 * middle (bottom edge for the top bar, top edge for the bottom one) */
void hmBar(float y, float h, bool edgeBelow, float lit)
{
    const float L = Ui::screenLeft(), W = Ui::screenWidth();
    Ui::roundRect(L, y, W, h, 0, edgeBelow ? HM_BAR_TOP : HM_BAR_BOTTOM, edgeBelow ? HM_BAR_BOTTOM : HM_BAR_TOP);
    if (lit > 0.0f) Ui::roundRect(L, y, W, h, 0, Ui::alpha(HM_BLUE, 0.18f * lit), Ui::alpha(HM_BLUE, 0.05f * lit));
    const float ey = edgeBelow ? y + h - 3 : y;
    Ui::roundRect(L, ey, W, 3, 0, HM_BLUE, HM_BLUE);
}

/* P1..P4: a battery each (four cells), dim when that remote is not there */
void batteries(float cy)
{
    const float STEP = 128, x0 = 320 - STEP * 1.5f;
    for (int i = 0; i < 4; ++i) {
        u32 type = 0;
        const bool on = WPAD_Probe(i, &type) == WPAD_ERR_NONE;
        /* the raw level runs to about 0xC0 when full */
        int cells = on ? (WPAD_BatteryLevel(i) * 4 + 0x5F) / 0xC0 : 0;
        if (cells > 4) cells = 4;
        if (on && cells < 1) cells = 1;
        const float cx = x0 + i * STEP;
        char pn[4];
        snprintf(pn, sizeof(pn), "P%d", i + 1);
        Ui::text(cx - 40, cy - 10, pn, 17, on ? 0xFFFFFFFF : 0xFFFFFF50);
        const float bx = cx - 12, by = cy - 9, bw = 44, bh = 18;
        Ui::roundBorder(bx, by, bw, bh, 3, 1.5f, on ? 0xFFFFFFFF : 0xFFFFFF50);
        Ui::roundRect(bx + bw, cy - 4, 3, 8, 1, on ? 0xFFFFFFFF : 0xFFFFFF50, on ? 0xFFFFFFFF : 0xFFFFFF50);
        const u32 cell = cells == 1 ? 0xE04040FF : HM_BLUE;   /* the last cell: red */
        for (int c = 0; c < cells; ++c)
            Ui::roundRect(bx + 3 + c * 10, by + 3, 8, bh - 6, 1, cell, cell);
    }
}

}  // namespace

/* -----------------------------------------------------------------------
 * doShowHomeOverlay — can be called from App::loop() (normal UI) or from
 * runPlaySession() (after the player stops with PLAYER_STOP_HOME) so both
 * contexts share identical UX.
 * Returns true if the user wants to exit (Wii Menu or Reset).
 * Sets g_restartApp = true when "Reset" is chosen.
 * ----------------------------------------------------------------------- */
bool doShowHomeOverlay(GRRLIB_ttfFont* font, GRRLIB_texImg* cursorPointerTex)
{
    (void)font;
    ir_t ir;
    ir.valid = false;
    int   hmSel      = 0;    /* 0 = Wii Menu, 1 = Reset (the D-pad's) */
    int   state      = 0;    /* 0 = HOME Menu, 1 = the question */
    int   confirmFor = 0;    /* which button asked it */

    /* the two big buttons */
    const float BTN_W = 230, BTN_H = 86, BTN_CY = 228;
    const float bcx[2] = { 320 - 130, 320 + 130 };
    /* the question: a dark box, Yes / No */
    const float DW = 400, DH = 200, DY = 240 - DH * 0.5f;
    const float PBW = 150, PBH = 52, PB_CY = DY + DH - 50;
    const float pbcx[2] = { 320 - 90, 320 + 90 };

    float openAnim = 0.0f, popAnim = 0.0f;
    float hot[2] = {}, popHot[2] = {}, closeHot = 0.0f;
    int   popSel = 1;             /* default: "No" */
    int   prevHoverMain = -1, prevHoverPop = -1;
    bool  prevOnClose = false;

    SYS_Report("[WiiFin] HOME menu\n");
    SoundFX::play(SoundFX::FX::MenuEnter);
    /* HOME closes it too, as on the Wii: not the press that opened it */
    const u64 openedAt = ticks_to_millisecs(gettime());

    while (true) {
        Input::update();
        Input::readIR(ir);
        orient_t orient; WPAD_Orientation(WPAD_CHAN_0, &orient);
        if (g_app_powerOff || g_app_reset) return true;

        int  hover = -1, popHover = -1;
        bool onClose = false;

        /* ---- Input ---- */
        if (state == 0) {
            const bool home = Input::isHomePressed() && ticks_to_millisecs(gettime()) - openedAt > 300;
            onClose = ir.valid && ir.y < TOP_H;
            if (Input::isBPressed() || home || (onClose && Input::isAJustPressed())) {
                SoundFX::play(SoundFX::FX::MenuExit);
                return false;
            }
            if (ir.valid)
                for (int i = 0; i < 2; i++)
                    if (fabsf(ir.x - bcx[i]) <= BTN_W * 0.5f && fabsf(ir.y - BTN_CY) <= BTN_H * 0.5f)
                        hover = i;
            if ((hover >= 0 && hover != prevHoverMain) || (onClose && !prevOnClose))
                SoundFX::play(SoundFX::FX::Select);
            prevHoverMain = hover;
            prevOnClose   = onClose;
            if (hover >= 0) hmSel = hover;
            /* with the pointer on the screen only a button under it counts;
             * the D-pad's choice when pointing away */
            if (Input::isAJustPressed() && (!ir.valid || hover >= 0)) {
                SoundFX::play(SoundFX::FX::Start);
                confirmFor = hover >= 0 ? hover : hmSel;
                state      = 1;
                popAnim    = 0.0f;
                popSel     = 1;
            }
            if (Input::isLeftPressed())  hmSel = 0;
            if (Input::isRightPressed()) hmSel = 1;
        } else {
            if (Input::isBPressed()) { SoundFX::play(SoundFX::FX::Back); state = 0; }
            if (ir.valid)
                for (int i = 0; i < 2; i++)
                    if (fabsf(ir.x - pbcx[i]) <= PBW * 0.5f && fabsf(ir.y - PB_CY) <= PBH * 0.5f)
                        popHover = i;
            if (popHover >= 0 && popHover != prevHoverPop)
                SoundFX::play(SoundFX::FX::Select);
            prevHoverPop = popHover;
            if (popHover >= 0) popSel = popHover;
            if (state == 1 && Input::isAJustPressed() && (!ir.valid || popHover >= 0)) {
                if (popSel == 0) {   /* Yes */
                    SoundFX::play(SoundFX::FX::MenuExit);
                    SoundFX::waitDone(SoundFX::FX::MenuExit);
                    if (confirmFor == 1) g_restartApp = true;
                    return true;
                }
                SoundFX::play(SoundFX::FX::Back);   /* No */
                state = 0;
            }
            if (Input::isLeftPressed())  popSel = 0;
            if (Input::isRightPressed()) popSel = 1;
        }

        /* ---- Animation ---- */
        openAnim = openAnim + 0.08f > 1.0f ? 1.0f : openAnim + 0.08f;
        const float oc = openAnim * openAnim * (3.0f - 2.0f * openAnim);   /* smoothstep */
        if (state == 1) popAnim = popAnim + 0.12f > 1.0f ? 1.0f : popAnim + 0.12f;
        const float pc = popAnim * popAnim * (3.0f - 2.0f * popAnim);
        for (int i = 0; i < 2; i++) {
            const bool h = state == 0 && (hover == i || (!ir.valid && hmSel == i));
            hot[i] = Ui::approach(hot[i], h ? 1.0f : 0.0f);
            const bool ph = popHover == i || (!ir.valid && popSel == i);
            popHot[i] = Ui::approach(popHot[i], ph ? 1.0f : 0.0f);
        }
        closeHot = Ui::approach(closeHot, onClose ? 1.0f : 0.0f);

        /* ---- Draw ---- */
        const float L = Ui::screenLeft(), W = Ui::screenWidth();
        Ui::roundRect(L, 0, W, 480, 0, 0x1C2026FF, 0x07080AFF);

        hmButton(bcx[0], BTN_CY, BTN_W, BTN_H, N_("Wii Menu"), 24, hot[0]);
        hmButton(bcx[1], BTN_CY, BTN_W, BTN_H, N_("Reset"),    24, hot[1]);

        /* the top bar slides down, the bottom one up */
        Ui::pushOffset(0, -(1.0f - oc) * (TOP_H + 10));
        hmBar(0, TOP_H, true, closeHot);
        Ui::text(40, 22, tr("HOME Menu"), 26, 0xFFFFFFFF);
        {
            /* Close, at the right: the whole bar closes it */
            const char* close = tr("Close");
            const float cw = (float)Ui::textWidth(close, 17), cx = 600 - cw;
            const u32 c = Ui::mix(0xFFFFFFA0, 0xFFFFFFFF, closeHot);
            /* an X: two strokes 2.6 px thick */
            auto stroke = [&](float x0, float y0, float x1, float y1) {
                const float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
                const float nx = -dy / l * 1.3f, ny = dx / l * 1.3f;
                Ui::triangle(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
                Ui::triangle(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
            };
            const float xx = cx - 22, xy = 35, a = 6;
            stroke(xx - a, xy - a, xx + a, xy + a);
            stroke(xx + a, xy - a, xx - a, xy + a);
            Ui::text(cx, 26, close, 17, c);
        }
        Ui::popOffset();

        Ui::pushOffset(0, (1.0f - oc) * (480 - BOTTOM_Y + 10));
        hmBar(BOTTOM_Y, 480 - BOTTOM_Y, false, 0.0f);
        Ui::textCentered(320, BOTTOM_Y + 16, tr("Wii Remote"), 15, 0xFFFFFFB0);
        batteries(BOTTOM_Y + 58);
        Ui::popOffset();

        /* ---- the question ---- */
        if (state == 1 && pc > 0.01f) {
            Ui::roundRect(L, 0, W, 480, 0, Ui::alpha(0x000000B0, pc), Ui::alpha(0x000000B0, pc));
            const float s = 0.85f + 0.15f * pc;
            Ui::pushOffset(0, (1.0f - s) * 60);
            Ui::shadow(320 - DW * 0.5f, DY + 4, DW, DH, 18, 10.0f, Ui::alpha(0x000000C0, pc));
            Ui::roundRect(320 - DW * 0.5f, DY, DW, DH, 18, Ui::alpha(HM_BAR_TOP, pc), Ui::alpha(HM_BAR_BOTTOM, pc));
            Ui::roundBorder(320 - DW * 0.5f, DY, DW, DH, 18, 2.0f, Ui::alpha(HM_BLUE, pc));
            Ui::textCentered(320, DY + 34, confirmFor == 0 ? tr("Return to the Wii Menu?") : tr("Reset?"),
                             21, Ui::alpha(0xFFFFFFFF, pc));
            Ui::textCentered(320, DY + 70, tr("(Anything not saved will be lost.)"), 14, Ui::alpha(0xFFFFFFA0, pc));
            hmButton(pbcx[0], PB_CY, PBW, PBH, N_("Yes"), 20, popHot[0]);
            hmButton(pbcx[1], PB_CY, PBW, PBH, N_("No"),  20, popHot[1]);
            Ui::popOffset();
        }

        /* fade in from black while opening */
        if (oc < 1.0f)
            Ui::roundRect(L, 0, W, 480, 0, (u32)((1.0f - oc) * 255.0f), (u32)((1.0f - oc) * 255.0f));

        /* the pointer, always on top */
        if (ir.valid && cursorPointerTex)
            GRRLIB_DrawImg((int)ir.x - 10, (int)ir.y - 4, cursorPointerTex, orient.roll, 1, 1, 0xFFFFFFFF);

        GRRLIB_Render();
    }
}
