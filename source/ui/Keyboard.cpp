#include "Keyboard.h"
#include "Ui.h"
#include "../input/Input.h"
#include "../core/SoundFX.h"
#include "../core/Lang.h"

#include <math.h>
#include <string.h>
#include <ogc/lwp_watchdog.h>

/* ---- The pages' typing keys: rows 0-2 ten each, row 3 eight (after Shift) */
static const char* const LETTERS[4][10] = {
    { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
    { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" },
    { "a", "s", "d", "f", "g", "h", "j", "k", "l", "-" },
    { "z", "x", "c", "v", "b", "n", "m", "'" },
};
static const char* const SYMBOLS[4][10] = {
    { "!", "?", "@", "#", "$", "%", "&", "*", "(", ")" },
    { "-", "_", "=", "+", "/", "\\", "|", "~", "^", "`" },
    { "[", "]", "{", "}", "<", ">", ";", ":", "\"", "'" },
    { ".", ",", "@", "/", ":", "-", "_", "?" },
};
static const char* const ACCENTS[4][10] = {
    { "\xc3\xa1", "\xc3\xa0", "\xc3\xa2", "\xc3\xa4", "\xc3\xa3", "\xc3\xa5", "\xc3\xa6", "\xc3\xa7", "\xc3\xa9", "\xc3\xa8" },
    { "\xc3\xaa", "\xc3\xab", "\xc3\xad", "\xc3\xac", "\xc3\xae", "\xc3\xaf", "\xc3\xb1", "\xc3\xb3", "\xc3\xb2", "\xc3\xb4" },
    { "\xc3\xb6", "\xc3\xb5", "\xc3\xb8", "\xc3\xba", "\xc3\xb9", "\xc3\xbb", "\xc3\xbc", "\xc3\xbd", "\xc3\xbf", "\xc3\x9f" },
    { "\xc2\xbf", "\xc2\xa1", "\xc2\xab", "\xc2\xbb", "\xc2\xa3", "\xc2\xa5", "\xc2\xb0", "\xc2\xa2" },
};

/* Row 4: the dark keys, as (first column, span) */
static const struct { int col, span, sp; } BOTTOM[6] = {
    { 0, 2, 2 /* K_SYM */ }, { 2, 2, 3 /* K_ACC */ }, { 4, 2, 4 /* K_SPACE */ },
    { 6, 1, 5 /* K_LEFT */ }, { 7, 1, 6 /* K_RIGHT */ }, { 8, 2, 7 /* K_DEL */ },
};

/* ---- Layout (the screenshot's proportions) ----------------------------- */
static const float PITCH_X = 57.5f, KEY_W = 52.0f;
static const float X0      = 320 - (9 * PITCH_X + KEY_W) * 0.5f;
static const float FIELD_Y = 66, FIELD_H = 40;
static const float KEYS_Y  = 116, PITCH_Y = 56, KEY_H = 48;
static const float ACT_Y   = KEYS_Y + 5 * PITCH_Y - 4, ACT_H = 46;
static const float CANCEL_X = X0 + 200, ENTER_X = X0 + 387, ACT_W = 163;
static const float KEY_R   = 3;

/* ---- Colours ----------------------------------------------------------- */
static const u32 BACKDROP   = 0x7A7472FF;
static const u32 FIELD_BG   = 0xE2E1E3FF;
static const u32 LIGHT_TOP  = 0xF6F6F6FF, LIGHT_BOT = 0xC8C8C8FF, LIGHT_EDGE = 0x8E8E8EFF;
static const u32 DARK_TOP   = 0x626262FF, DARK_BOT  = 0x2E2E2EFF, DARK_EDGE  = 0x222222FF;
static const u32 INK        = 0x3A3A3AFF, GLYPH = 0xF2F2F2FF;

static unsigned long long nowMs() { return ticks_to_millisecs(gettime()); }

/* A stroke of thickness t from (x0, y0) to (x1, y1). */
static void stroke(float x0, float y0, float x1, float y1, float t, u32 c)
{
    const float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len <= 0.0f) return;
    const float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
    Ui::triangle(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
    Ui::triangle(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}

/* UTF-8: the start of the character before / after byte i */
static size_t prevChar(const std::string& s, size_t i)
{
    if (i == 0) return 0;
    --i;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) --i;
    return i;
}
static size_t nextChar(const std::string& s, size_t i)
{
    if (i >= s.size()) return s.size();
    ++i;
    while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) ++i;
    return i;
}

void Keyboard::reset()
{
    page = 0; shift = 0;
    selRow = 1; selCol = 0;
    caret = (size_t)-1;
    hoverRow = hoverCol = -1;
    dpad = false;
    flashMs = 0;
}

Keyboard::Key Keyboard::keyAt(int row, int col) const
{
    Key k{ row, col, 1, nullptr, K_NONE };
    if (row < 3 || (row == 3 && col >= 2)) {
        const char* const (*pg)[10] = page == 1 ? SYMBOLS : page == 2 ? ACCENTS : LETTERS;
        k.str = row < 3 ? pg[row][col] : pg[3][col - 2];
        return k;
    }
    if (row == 3) { k.col = 0; k.span = 2; k.sp = K_SHIFT; return k; }
    if (row == 4) {
        for (const auto& b : BOTTOM)
            if (col >= b.col && col < b.col + b.span) {
                k.col = b.col; k.span = b.span; k.sp = (Special)b.sp;
                return k;
            }
    }
    /* row 5: Cancel under the left half, Enter under the right */
    if (col < 6) { k.col = 0; k.span = 6; k.sp = K_CANCEL; }
    else         { k.col = 6; k.span = 4; k.sp = K_ENTER; }
    return k;
}

void Keyboard::keyRect(const Key& k, float& x, float& y, float& w, float& h) const
{
    if (k.row == 5) {
        x = k.sp == K_CANCEL ? CANCEL_X : ENTER_X;
        y = ACT_Y; w = ACT_W; h = ACT_H;
        return;
    }
    x = X0 + k.col * PITCH_X;
    y = KEYS_Y + k.row * PITCH_Y;
    w = (k.span - 1) * PITCH_X + KEY_W;
    h = KEY_H;
}

bool Keyboard::keyUnder(const ir_t& ir, Key& out) const
{
    if (!ir.valid) return false;
    for (int row = 0; row < ROWS; ++row)
        for (int col = 0; col < COLS; ) {
            Key k = keyAt(row, col);
            col = k.col + k.span;
            float x, y, w, h;
            keyRect(k, x, y, w, h);
            if (ir.x >= x && ir.x < x + w && ir.y >= y && ir.y < y + h) { out = k; return true; }
        }
    return false;
}

std::string Keyboard::label(const Key& k) const
{
    std::string s = k.str ? k.str : "";
    if (!shift || s.empty()) return s;
    unsigned char c0 = (unsigned char)s[0];
    if (s.size() == 1 && c0 >= 'a' && c0 <= 'z') s[0] = (char)(c0 - 32);
    /* Latin-1 letters à..þ: their capitals are 0x20 before (not ÷, ÿ) */
    else if (s.size() == 2 && c0 == 0xC3) {
        unsigned char c1 = (unsigned char)s[1];
        if (c1 >= 0xA0 && c1 <= 0xBE && c1 != 0xB7) s[1] = (char)(c1 - 0x20);
    }
    return s;
}

Keyboard::Result Keyboard::press(const Key& k, std::string& text, size_t maxLen)
{
    flashRow = k.row; flashCol = k.col; flashMs = nowMs();
    switch (k.sp) {
    case K_SHIFT: {
        unsigned long long t = nowMs();
        if (shift == 1 && t - lastShiftMs < 450) shift = 2;   /* double press: lock */
        else shift = shift ? 0 : 1;
        lastShiftMs = t;
        SoundFX::play(SoundFX::FX::PressKey);
        return Result::None;
    }
    case K_SYM:
        page = page == 1 ? 0 : 1;
        SoundFX::play(SoundFX::FX::PressKey);
        return Result::None;
    case K_ACC:
        page = page == 2 ? 0 : 2;
        SoundFX::play(SoundFX::FX::PressKey);
        return Result::None;
    case K_SPACE:
        if (text.size() < maxLen) { text.insert(caret, 1, ' '); ++caret; }
        SoundFX::play(SoundFX::FX::PressKey);
        return Result::None;
    case K_LEFT:
        caret = prevChar(text, caret);
        SoundFX::play(SoundFX::FX::PressKey);
        return Result::None;
    case K_RIGHT:
        caret = nextChar(text, caret);
        SoundFX::play(SoundFX::FX::PressKey);
        return Result::None;
    case K_DEL:
        if (caret > 0) {
            size_t from = prevChar(text, caret);
            text.erase(from, caret - from);
            caret = from;
        }
        SoundFX::play(SoundFX::FX::Backspace);
        return Result::None;
    case K_CANCEL:
        SoundFX::play(SoundFX::FX::Back);
        return Result::Cancel;
    case K_ENTER:
        return Result::Enter;
    default: break;
    }
    const std::string s = label(k);
    if (s.empty() || text.size() + s.size() > maxLen) return Result::None;
    text.insert(caret, s);
    caret += s.size();
    if (shift == 1 && page != 1) shift = 0;   /* one-shot */
    SoundFX::play(SoundFX::FX::PressKey);
    return Result::None;
}

Keyboard::Result Keyboard::update(const ir_t& ir, std::string& text, size_t maxLen)
{
    /* the text may have changed under us (a new field, a new search) */
    if (caret > text.size()) caret = text.size();

    /* Pointer: takes the choice when it moves onto another key */
    Key pk{ -1, -1, 0, nullptr, K_NONE };
    const bool overKey = keyUnder(ir, pk);
    if (overKey && (pk.row != hoverRow || pk.col != hoverCol)) {
        selRow = pk.row; selCol = pk.col; dpad = false;
    }
    hoverRow = overKey ? pk.row : -1;
    hoverCol = overKey ? pk.col : -1;

    /* D-pad (the column kept going up and down, as far as it goes) */
    const int was = selRow * 16 + keyAt(selRow, selCol).col;
    if (Input::isUpPressed())   selRow = (selRow + ROWS - 1) % ROWS;
    if (Input::isDownPressed()) selRow = (selRow + 1) % ROWS;
    if (Input::isLeftPressed()) {
        Key k = keyAt(selRow, selCol);
        selCol = (k.col + COLS - 1) % COLS;
    }
    if (Input::isRightPressed()) {
        Key k = keyAt(selRow, selCol);
        selCol = (k.col + k.span) % COLS;
    }
    if (selRow * 16 + keyAt(selRow, selCol).col != was) dpad = true;

    /* A: the key chosen; with the pointer off the keys, only if the D-pad
     * chose it */
    if (Input::isAJustPressed() && (!ir.valid || overKey || dpad))
        return press(keyAt(selRow, selCol), text, maxLen);
    if (Input::isLPressed()) press(keyAt(3, 0), text, maxLen);   /* -: Shift */
    if (Input::isActionPressed()) return Result::Enter;          /* + (GameCube Z) */
    if (Input::isBackPressed()) {
        if (text.empty()) return Result::Cancel;
        if (caret > 0) {
            size_t from = prevChar(text, caret);
            text.erase(from, caret - from);
            caret = from;
        }
        SoundFX::play(SoundFX::FX::Backspace);
    }
    return Result::None;
}

/* ---- Drawing ----------------------------------------------------------- */

void Keyboard::drawKey(const Key& k, bool on) const
{
    float x, y, w, h;
    keyRect(k, x, y, w, h);
    /* typed: the key dips for a moment */
    const unsigned long long t = nowMs();
    const float flash = (k.row == flashRow && k.col == flashCol && t - flashMs < 140)
                      ? 1.0f - (t - flashMs) / 140.0f : 0.0f;
    if (flash > 0) { x += 1.5f * flash; y += 2.0f * flash; w -= 3 * flash; h -= 3 * flash; }
    if (on) Ui::grow(x, y, w, h, Ui::HOVER_GROW);

    const bool dark = k.sp != K_NONE && k.sp != K_CANCEL && k.sp != K_ENTER;
    Ui::shadow(x, y + 2, w, h, KEY_R, 4.0f, 0x00000060);
    if (on) {
        Ui::roundRect(x, y, w, h, KEY_R, Ui::HOVER_TOP, Ui::HOVER_BOTTOM);
        Ui::roundBorder(x, y, w, h, KEY_R, 1.5f, Ui::HOVER_EDGE);
    } else if (dark) {
        Ui::roundRect(x, y, w, h, KEY_R, DARK_TOP, DARK_BOT);
        Ui::roundBorder(x, y, w, h, KEY_R, 1.0f, DARK_EDGE);
    } else {
        Ui::roundRect(x, y, w, h, KEY_R, LIGHT_TOP, LIGHT_BOT);
        Ui::roundBorder(x, y, w, h, KEY_R, 1.0f, LIGHT_EDGE);
    }

    const float cx = x + w * 0.5f, cy = y + h * 0.5f, g = on ? Ui::HOVER_GROW : 1.0f;
    /* a page or Shift in use: its glyph orange (white on the orange key) */
    const bool latched = (k.sp == K_SHIFT && shift) || (k.sp == K_SYM && page == 1) ||
                         (k.sp == K_ACC && page == 2);
    const u32 gc = latched && !on ? Ui::HOVER_TOP : GLYPH;

    switch (k.sp) {
    case K_NONE: {
        const std::string s = label(k);
        Ui::textCentered(cx, cy - 12 * g, s.c_str(), (int)(22 * g), INK);
        break;
    }
    case K_CANCEL: case K_ENTER: {
        const char* s = k.sp == K_CANCEL ? tr(N_("Cancel")) : tr(enterLabel);
        const int size = (int)(22 * g);
        Ui::textCentered(cx, cy - 12 * g, s, size, INK);
        Ui::textCentered(cx + 1, cy - 12 * g, s, size, INK);   /* bolder */
        break;
    }
    case K_SHIFT: {
        /* an up arrow: its outline, filled once on, a bar under it locked */
        const float tw = 13 * g, sw = 6 * g, top = cy - 13 * g, mid = cy - 1 * g, bot = cy + 10 * g;
        if (shift) {
            Ui::triangle(cx, top, cx + tw, mid, cx - tw, mid, gc);
            Ui::roundRect(cx - sw, mid - 0.5f, sw * 2, bot - mid + 0.5f, 0, gc);
            if (shift == 2) Ui::roundRect(cx - sw, bot + 3 * g, sw * 2, 2.5f * g, 0, gc);
        } else {
            const float t = 2.2f * g;
            stroke(cx, top, cx + tw, mid, t, gc);
            stroke(cx + tw, mid, cx + sw, mid, t, gc);
            stroke(cx + sw, mid, cx + sw, bot, t, gc);
            stroke(cx + sw, bot, cx - sw, bot, t, gc);
            stroke(cx - sw, bot, cx - sw, mid, t, gc);
            stroke(cx - sw, mid, cx - tw, mid, t, gc);
            stroke(cx - tw, mid, cx, top, t, gc);
        }
        break;
    }
    case K_SYM: case K_ACC: {
        const char* s = (k.sp == K_SYM ? page == 1 : page == 2) ? "abc"
                      : k.sp == K_SYM ? "!#$" : "\xc3\xa1\xc3\xa9\xc3\xad";
        Ui::textCentered(cx, cy - 12 * g, s, (int)(22 * g), gc);
        break;
    }
    case K_SPACE: {   /* ␣ */
        const float hw = 18 * g, by = cy + 4 * g, t = 2.5f * g;
        Ui::roundRect(cx - hw, by - t * 0.5f, hw * 2, t, 0, gc);
        Ui::roundRect(cx - hw - t * 0.5f, by - 7 * g, t, 7 * g + t * 0.5f, 0, gc);
        Ui::roundRect(cx + hw - t * 0.5f, by - 7 * g, t, 7 * g + t * 0.5f, 0, gc);
        break;
    }
    case K_LEFT:
        Ui::triangle(cx - 7 * g, cy, cx + 6 * g, cy - 8 * g, cx + 6 * g, cy + 8 * g, gc);
        break;
    case K_RIGHT:
        Ui::triangle(cx + 7 * g, cy, cx - 6 * g, cy + 8 * g, cx - 6 * g, cy - 8 * g, gc);
        break;
    case K_DEL: {   /* a white tag pointing left with a dark ✕ */
        const float l = cx - 22 * g, b0 = cx - 11 * g, r = cx + 20 * g, hh = 11 * g;
        Ui::triangle(l, cy, b0, cy - hh, b0, cy + hh, gc);
        Ui::roundRect(b0 - 0.5f, cy - hh, r - b0 + 0.5f, hh * 2, 2, gc);
        const u32 xc = on ? Ui::HOVER_BOTTOM : DARK_BOT;
        const float xm = cx + 4 * g, a = 5 * g;
        stroke(xm - a, cy - a, xm + a, cy + a, 3.0f * g, xc);
        stroke(xm + a, cy - a, xm - a, cy + a, 3.0f * g, xc);
        break;
    }
    default: break;
    }
}

void Keyboard::render(const ir_t& ir, const std::string& text, bool masked,
                      const char* placeholder) const
{
    (void)ir;
    /* the dark grey under the band */
    const float top = Ui::headerBand();
    Ui::roundRect(Ui::screenLeft(), top, Ui::screenWidth(), 480 - top, 0, BACKDROP, BACKDROP);

    /* the field: the text, the caret kept in sight */
    {
        const float fw = 9 * PITCH_X + KEY_W, pad = 12, avail = fw - 2 * pad;
        Ui::roundRect(X0, FIELD_Y, fw, FIELD_H, 0, FIELD_BG, FIELD_BG);
        std::string shown;
        size_t at = caret > text.size() ? text.size() : caret, shownAt = 0;
        if (masked) {   /* one * a character */
            for (size_t i = 0; i < text.size(); i = nextChar(text, i)) {
                if (i < at) ++shownAt;
                shown += '*';
            }
        } else {
            shown = text; shownAt = at;
        }
        size_t from = 0;
        while (from < shownAt &&
               Ui::textWidth(shown.substr(from, shownAt - from).c_str(), 18) > avail - 4)
            from = nextChar(shown, from);
        const float ty = FIELD_Y + (FIELD_H - 20) * 0.5f;
        if (text.empty() && placeholder)
            Ui::text(X0 + pad, ty, placeholder, 18, 0x8A8A8AFF);
        Ui::clip(X0 + pad, FIELD_Y, avail, FIELD_H);
        Ui::text(X0 + pad, ty, shown.c_str() + from, 18, INK);
        Ui::clipReset();
        if ((nowMs() / 500) % 2 == 0) {
            const float cx = X0 + pad + Ui::textWidth(shown.substr(from, shownAt - from).c_str(), 18);
            Ui::roundRect(cx, FIELD_Y + 9, 2, FIELD_H - 18, 0, INK, INK);
        }
    }

    /* the keys; the chosen one (the pointer's or the D-pad's) last, on top */
    const Key sk = keyAt(selRow, selCol);
    for (int row = 0; row < ROWS; ++row)
        for (int col = 0; col < COLS; ) {
            Key k = keyAt(row, col);
            col = k.col + k.span;
            if (k.row == sk.row && k.col == sk.col) continue;
            drawKey(k, false);
        }
    drawKey(sk, true);
}
