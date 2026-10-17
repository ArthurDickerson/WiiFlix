#include "TrackList.h"
#include "Ui.h"

#include <math.h>

/* ---- Layout ------------------------------------------------------------
 * PANEL_W wide, centred; the title's line, then rows ROW_STEP apart. */
static const int   ROWS     = 7;
static const float PANEL_W  = 380, RADIUS = 6;
static const float HEAD_H   = 46;              /* title and ✕ above the rows */
static const float ROW_STEP = 34, ROW_H = 30, PAD = 14;
static const float BTN_R    = 3;               /* the rectangular buttons' corners */
static const float CLOSE_A  = 7;               /* the ✕'s half size */

/* A stroke of thickness t from (x0, y0) to (x1, y1). */
static void stroke(float x0, float y0, float x1, float y1, float t, u32 c)
{
    const float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len <= 0.0f) return;
    const float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
    Ui::triangle(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
    Ui::triangle(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}

/* Trim s (adding "...") until it fits in maxW pixels. */
static std::string fit(const std::string& s, int size, int maxW)
{
    if (Ui::textWidth(s.c_str(), size) <= maxW) return s;
    std::string t = s;
    while (!t.empty()) {
        t.pop_back();
        while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back();   /* UTF-8 */
        if (Ui::textWidth((t + "...").c_str(), size) <= maxW) return t + "...";
    }
    return "...";
}

void TrackList::open(const std::string& t, const std::vector<std::string>& rows, int cur)
{
    shown    = true;
    title    = t;
    labels   = rows;
    current  = cur;
    sel      = cur >= 0 && cur < (int)rows.size() ? cur : 0;
    top      = sel >= ROWS ? sel - ROWS + 1 : 0;
    hover    = -2;        /* the pointer's first row counts as a move */
    dpad     = false;
    closeHot = false;
}

int TrackList::shownRows() const
{
    const int n = (int)labels.size();
    return n < ROWS ? n : ROWS;
}

void TrackList::box(float& x, float& y, float& w, float& h) const
{
    w = PANEL_W;
    h = HEAD_H + shownRows() * ROW_STEP + PAD - (ROW_STEP - ROW_H);
    x = 320 - w * 0.5f;
    y = 240 - h * 0.5f;
}

TrackList::Result TrackList::update(const ir_t& ir, bool up, bool down, bool a, bool b)
{
    if (!shown) return Result::None;
    const int n = (int)labels.size();
    float x, y, w, h;
    box(x, y, w, h);

    int over = -1;
    closeHot = false;
    if (ir.valid) {
        for (int i = 0; i < shownRows(); ++i) {
            const float ry = y + HEAD_H + i * ROW_STEP;
            if (ir.x >= x + PAD && ir.x < x + w - PAD && ir.y >= ry && ir.y < ry + ROW_H)
                over = top + i;
        }
        const float cx = x + w - 26, cy = y + HEAD_H * 0.5f;
        closeHot = fabsf(ir.x - cx) < 16 && fabsf(ir.y - cy) < 16;
    }
    /* one choice: the pointer takes it when it moves onto another row, the
     * D-pad moves it too, even with the pointer on the screen */
    if (over >= 0 && over != hover) { sel = over; dpad = false; }
    hover = over;
    if (up   && sel > 0)     { --sel; dpad = true; }
    if (down && sel + 1 < n) { ++sel; dpad = true; }
    if (sel < top) top = sel;
    if (sel >= top + ROWS) top = sel - ROWS + 1;

    /* A off the list closes it, unless the D-pad chose last */
    if (b || (a && (closeHot || (ir.valid && over < 0 && !dpad)))) {
        shown = false;
        return Result::Closed;
    }
    if (a && n > 0) {
        shown = false;
        return Result::Picked;
    }
    return Result::None;
}

void TrackList::draw() const
{
    if (!shown) return;
    const Ui::Palette& p = Ui::pal();
    const int n = (int)labels.size();
    float x, y, w, h;
    box(x, y, w, h);

    Ui::roundRect(Ui::screenLeft(), 0, Ui::screenWidth(), 480, 0, 0x00000070, 0x00000070);
    Ui::shadow(x, y + 4, w, h, RADIUS, 12.0f, 0x00000070);
    Ui::roundRect(x, y, w, h, RADIUS, p.cardTop, p.cardBottom);
    Ui::roundBorder(x, y, w, h, RADIUS, 1.5f, p.cardBorder);

    /* the title, bolder, then a line; the ✕ at the right */
    {
        const float cy = y + HEAD_H * 0.5f;
        std::string t = fit(title, 18, (int)w - 80);
        Ui::text(x + PAD + 4, cy - 12, t.c_str(), 18, p.text);
        Ui::text(x + PAD + 5, cy - 12, t.c_str(), 18, p.text);
        Ui::roundRect(x + PAD, y + HEAD_H - 7, w - 2 * PAD, 1.5f, 0.75f, Ui::alpha(p.cardBorder, 0.7f));
        const float g = closeHot ? Ui::HOVER_GROW : 1.0f, k = CLOSE_A * g, cx = x + w - 26;
        const u32 c = closeHot ? Ui::HOVER_TOP : p.text;
        stroke(cx - k, cy - k, cx + k, cy + k, 3.0f * g, c);
        stroke(cx + k, cy - k, cx - k, cy + k, 3.0f * g, c);
    }

    /* the rows: rectangular buttons, the choice orange; ✓ the one in use */
    const float sbw = n > ROWS ? 12 : 0;   /* room for the scrollbar */
    for (int i = 0; i < shownRows(); ++i) {
        const int k = top + i;
        const float rx = x + PAD, ry = y + HEAD_H + i * ROW_STEP, rw = w - 2 * PAD - sbw;
        const bool on = k == sel;
        Ui::button(rx, ry, rw, ROW_H, "", 16, on ? 1.0f : 0.0f, BTN_R);
        const u32 c = on ? 0xFFFFFFFF : p.text;
        std::string t = fit(labels[k], 16, (int)rw - 44);
        Ui::text(rx + 12, ry + 6, t.c_str(), 16, c);
        if (k == current)
            Ui::textRight(rx + rw - 12, ry + 6, "\xe2\x9c\x93", 16, on ? c : p.ok);
    }
    if (n > ROWS)
        Ui::scrollbar(x + w - PAD - 6, y + HEAD_H, ROWS * ROW_STEP - (ROW_STEP - ROW_H), top, ROWS, n);
}
