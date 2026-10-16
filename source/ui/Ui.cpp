#include "Ui.h"
#include "../core/Text.h"
#include "../core/Lang.h"
#include "../core/Utils.h"

#include <gccore.h>
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <ogc/lwp_watchdog.h>

/* GRRLIB's 2D model-view matrix (defined in GRRLIB_core.c, not in its headers) */
extern Mtx GXmodelView2D;

namespace Ui {

/* ---- Palettes ----------------------------------------------------------- */

static const Palette LIGHT = {
    /* bgTop, bgBottom, stripe */        0xF4F5F6FF, 0xDCDFE3FF, 0x0000000A,
    /* cardTop, cardBottom, cardBorder */0xFFFFFFFF, 0xEEF0F2FF, 0xB4B9BFFF,
    /* text, textDim, textOnAccent */    0x4C4F54FF, 0x8B9097FF, 0xFFFFFFFF,
    /* accent, accentDark, glow */       0x34BEEDFF, 0x0A93C6FF, 0x34BEED90,
    /* shadow */                         0x00000030,
    /* barTop, barBottom, barBorder */   0xF0F1F3FF, 0xCDD1D6FF, 0xA7ADB4FF,
    /* field, fieldBorder */             0xFFFFFFFF, 0xC3C8CEFF,
    /* danger, ok */                     0xE0524DFF, 0x3DAF5AFF,
    /* dim */                            0x000000A0,
};

static const Palette DARK = {
    /* bgTop, bgBottom, stripe */        0x1D2736FF, 0x0D131CFF, 0xFFFFFF07,
    /* cardTop, cardBottom, cardBorder */0x2E3A4CFF, 0x232D3BFF, 0x43526AFF,
    /* text, textDim, textOnAccent */    0xEDF1F6FF, 0x8E9DB0FF, 0xFFFFFFFF,
    /* accent, accentDark, glow */       0x34BEEDFF, 0x5CCFF4FF, 0x34BEED80,
    /* shadow */                         0x00000070,
    /* barTop, barBottom, barBorder */   0x2A3546FF, 0x1A222FFF, 0x3B485CFF,
    /* field, fieldBorder */             0x18202BFF, 0x3B485CFF,
    /* danger, ok */                     0xF0605AFF, 0x4CC46AFF,
    /* dim */                            0x000000B0,
};

/* WiiFlix: the old Wii video channel's light grey, under its red band */
static const Palette FLIX = {
    /* bgTop, bgBottom, stripe */        0xEAEAEAFF, 0xD2D2D2FF, 0x00000000,
    /* cardTop, cardBottom, cardBorder */0xF8F8F8FF, 0xE3E3E3FF, 0xB2B2B2FF,
    /* text, textDim, textOnAccent */    0x2B2B2BFF, 0x6E6E6EFF, 0xFFFFFFFF,
    /* accent, accentDark, glow */       0xE50914FF, 0xE50914FF, 0xE5091460,
    /* shadow */                         0x00000038,
    /* barTop, barBottom, barBorder */   0xEFEFEFFF, 0xCDCDCDFF, 0xA6A6A6FF,
    /* field, fieldBorder */             0xFFFFFFFF, 0xB6B6B6FF,
    /* danger, ok */                     0xE50914FF, 0x3DAF5AFF,
    /* dim */                            0x000000A0,
};

static const Palette LUHU = {
    /* bgTop, bgBottom, stripe */        0x161616FF, 0x000000FF, 0xFFFFFF05,
    /* cardTop, cardBottom, cardBorder */0x55595EFF, 0x34373BFF, 0xA9AEB4FF,
    /* text, textDim, textOnAccent */    0xF2F3F4FF, 0xA4A9AFFF, 0x0E1A06FF,
    /* accent, accentDark, glow */       0x7DC242FF, 0xA4DD6FFF, 0x7DC24270,
    /* shadow */                         0x00000090,
    /* barTop, barBottom, barBorder */   0x4A4E53FF, 0x26282BFF, 0x8E9399FF,
    /* field, fieldBorder */             0x101010FF, 0x6E7379FF,
    /* danger, ok */                     0xF0605AFF, 0x7DC242FF,
    /* dim */                            0x000000C0,
};

static const Palette SUSHI = {
    /* bgTop, bgBottom, stripe */        0x2E2E2EFF, 0x1A1A1AFF, 0xFFFFFF05,
    /* cardTop, cardBottom, cardBorder */0x424242FF, 0x333333FF, 0x5C5C5CFF,
    /* text, textDim, textOnAccent */    0xF4F4F4FF, 0xABABABFF, 0xFFFFFFFF,
    /* accent, accentDark, glow */       0xF47521FF, 0xFF9C57FF, 0xF4752170,
    /* shadow */                         0x00000080,
    /* barTop, barBottom, barBorder */   0x3A3A3AFF, 0x242424FF, 0x585858FF,
    /* field, fieldBorder */             0x1E1E1EFF, 0x585858FF,
    /* danger, ok */                     0xF0605AFF, 0x4CC46AFF,
    /* dim */                            0x000000B8,
};

/* WiiFlix has one look (no Theme setting): the light Flix palette */
static const Theme     s_theme = Theme::Flix;
LibraryStyle libraryStyle()                  { return LibraryStyle::ListCover; }
static GRRLIB_ttfFont* s_font  = nullptr;

Theme       theme()             { return s_theme; }
static const Palette& themePal()
{
    switch (s_theme) {
    case Theme::Dark: return DARK;
    case Theme::Flix: return FLIX;
    case Theme::Luhu: return LUHU;
    case Theme::Sushi: return SUSHI;
    default:          return LIGHT;
    }
}
/* Solid colours: each gradient becomes its middle colour, without the
 * pinstripes (the bands a gradient shows on a 480i TV, issue 22) */
static bool s_solid = false;
void setSolidColors(bool on) { s_solid = on; }
static bool s_clock12 = false;
void setClock12h(bool on) { s_clock12 = on; }
bool clock12h()           { return s_clock12; }
bool solidColors()           { return s_solid; }
const Palette& pal()
{
    if (!s_solid) return themePal();
    static Palette flat;
    static int     flatFor = -1;
    if (flatFor != (int)s_theme) {
        flat = themePal();
        flat.bgTop  = flat.bgBottom   = mix(flat.bgTop,   flat.bgBottom,   0.5f);
        flat.cardTop = flat.cardBottom = mix(flat.cardTop, flat.cardBottom, 0.5f);
        flat.barTop = flat.barBottom  = mix(flat.barTop,  flat.barBottom,  0.5f);
        flat.stripe = 0;
        flatFor = (int)s_theme;
    }
    return flat;
}
void setFont(GRRLIB_ttfFont* f) { s_font = f; }
GRRLIB_ttfFont* font()          { return s_font; }

/* ---- Colour helpers ------------------------------------------------------ */

u32 mix(u32 a, u32 b, float t)
{
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    u32 out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        int ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
        out |= (u32)(ca + (cb - ca) * t + 0.5f) << sh;
    }
    return out;
}

u32 alpha(u32 c, float k)
{
    if (k >= 1.0f) return c;
    if (k <= 0.0f) return c & 0xFFFFFF00;
    return (c & 0xFFFFFF00) | (u32)((c & 0xFF) * k + 0.5f);
}

float approach(float cur, float target, float speed)
{
    float d = target - cur;
    if (d > -0.002f && d < 0.002f) return target;
    return cur + d * speed;
}

/* ---- Geometry ------------------------------------------------------------ */

static const int CORNER_SEGS = 7;
static const int MAX_PTS     = 4 * (CORNER_SEGS + 1);

struct Outline {
    int   n;
    float x[MAX_PTS], y[MAX_PTS];     /* perimeter, clockwise from the left of the top-left arc */
    float nx[MAX_PTS], ny[MAX_PTS];   /* outward normals */
};

static void outline(Outline& o, float x, float y, float w, float h, float r)
{
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    if (r < 0.0f) r = 0.0f;
    const float cx[4] = { x + r, x + w - r, x + w - r, x + r };
    const float cy[4] = { y + r, y + r,     y + h - r, y + h - r };
    o.n = 0;
    for (int c = 0; c < 4; ++c) {
        float a0 = (float)M_PI * (1.0f + 0.5f * c);
        for (int i = 0; i <= CORNER_SEGS; ++i) {
            float a = a0 + (float)M_PI * 0.5f * i / CORNER_SEGS;
            float ca = cosf(a), sa = sinf(a);
            o.x[o.n]  = cx[c] + r * ca;
            o.y[o.n]  = cy[c] + r * sa;
            o.nx[o.n] = ca;
            o.ny[o.n] = sa;
            ++o.n;
        }
    }
}

static inline void vtx(float x, float y, u32 c)
{
    GX_Position3f32(x, y, 0.0f);
    GX_Color1u32(c);
}

static void beginShapes()
{
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
    GX_LoadPosMtxImm(GXmodelView2D, GX_PNMTX0);
}

static inline u32 gradAt(float y, float y0, float h, u32 top, u32 bottom)
{
    return top == bottom ? top : mix(top, bottom, h > 0.0f ? (y - y0) / h : 0.0f);
}

/* Band from the outline to the outline pushed out by `width`, colour
 * inner -> outer (used for anti-aliasing feathers, shadows and glows). */
static void band(const Outline& o, float inset, float width, u32 inner, u32 outer,
                 float y0, float h, u32 top, u32 bottom, bool gradInner)
{
    GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, (o.n + 1) * 2);
    for (int k = 0; k <= o.n; ++k) {
        int i = k % o.n;
        float ix = o.x[i] - o.nx[i] * inset, iy = o.y[i] - o.ny[i] * inset;
        u32 ci = gradInner ? gradAt(iy, y0, h, top, bottom) : inner;
        u32 co = gradInner ? (ci & 0xFFFFFF00) : outer;
        vtx(ix, iy, ci);
        vtx(o.x[i] + o.nx[i] * width, o.y[i] + o.ny[i] * width, co);
    }
    GX_End();
}

void roundRect(float x, float y, float w, float h, float r, u32 top, u32 bottom)
{
    if (w <= 0 || h <= 0) return;
    Outline o;
    outline(o, x, y, w, h, r);
    beginShapes();
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, o.n + 2);
    vtx(x + w * 0.5f, y + h * 0.5f, gradAt(y + h * 0.5f, y, h, top, bottom));
    for (int k = 0; k <= o.n; ++k) {
        int i = k % o.n;
        vtx(o.x[i], o.y[i], gradAt(o.y[i], y, h, top, bottom));
    }
    GX_End();
    band(o, 0.0f, 1.0f, 0, 0, y, h, top, bottom, true);   /* anti-aliased edge */
}

void roundBorder(float x, float y, float w, float h, float r, float t, u32 color)
{
    if (w <= 0 || h <= 0) return;
    Outline o;
    outline(o, x, y, w, h, r);
    beginShapes();
    band(o, t, 0.0f, color, color, 0, 0, 0, 0, false);              /* solid ring  */
    band(o, 0.0f, 1.0f, color, color & 0xFFFFFF00, 0, 0, 0, 0, false);  /* outer AA */
    band(o, t + 1.0f, -t, color & 0xFFFFFF00, color, 0, 0, 0, 0, false); /* inner AA */
}

void shadow(float x, float y, float w, float h, float r, float blur, u32 color)
{
    Outline o;
    outline(o, x, y, w, h, r);
    beginShapes();
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, o.n + 2);
    vtx(x + w * 0.5f, y + h * 0.5f, color);
    for (int k = 0; k <= o.n; ++k) { int i = k % o.n; vtx(o.x[i], o.y[i], color); }
    GX_End();
    band(o, 0.0f, blur, color, color & 0xFFFFFF00, 0, 0, 0, 0, false);
}

void circle(float cx, float cy, float rad, u32 color)
{
    roundRect(cx - rad, cy - rad, rad * 2, rad * 2, rad, color);
}

void texRound(GRRLIB_texImg* tex, float x, float y, float w, float h, float r, u32 tint)
{
    texRound(tex, x, y, w, h, r, 0.0f, 0.0f, 1.0f, 1.0f, tint);
}

void texCover(GRRLIB_texImg* tex, float x, float y, float w, float h, float r,
              float aspect, u32 tint)
{
    if (!tex || tex->w == 0 || tex->h == 0) return;
    float ta = (float)tex->w / tex->h;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (ta > aspect) { float k = aspect / ta; u0 = (1 - k) * 0.5f; u1 = u0 + k; }
    else             { float k = ta / aspect; v0 = (1 - k) * 0.5f; v1 = v0 + k; }
    texRound(tex, x, y, w, h, r, u0, v0, u1, v1, tint);
}

void texRound(GRRLIB_texImg* tex, float x, float y, float w, float h, float r,
              float u0, float v0, float u1, float v1, u32 tint)
{
    if (!tex || !tex->data || w <= 0 || h <= 0) return;
    Outline o;
    outline(o, x, y, w, h, r);
    GXTexObj obj;
    GX_InitTexObj(&obj, tex->data, tex->w, tex->h, tex->format, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&obj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GX_LoadTexObj(&obj, GX_TEXMAP0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_LoadPosMtxImm(GXmodelView2D, GX_PNMTX0);
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, o.n + 2);
    const float du = u1 - u0, dv = v1 - v0;
    GX_Position3f32(x + w * 0.5f, y + h * 0.5f, 0.0f); GX_Color1u32(tint);
    GX_TexCoord2f32(u0 + du * 0.5f, v0 + dv * 0.5f);
    for (int k = 0; k <= o.n; ++k) {
        int i = k % o.n;
        GX_Position3f32(o.x[i], o.y[i], 0.0f);
        GX_Color1u32(tint);
        GX_TexCoord2f32(u0 + du * (o.x[i] - x) / w, v0 + dv * (o.y[i] - y) / h);
    }
    GX_End();
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
}

void triangle(float x0, float y0, float x1, float y1, float x2, float y2, u32 c)
{
    beginShapes();
    GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
    vtx(x0, y0, c); vtx(x1, y1, c); vtx(x2, y2, c);
    GX_End();
}

/* ---- Text --------------------------------------------------------------- */

int  textWidth(const char* s, int size) { return (int)Text::width(s_font, s, size); }
void text(float x, float y, const char* s, int size, u32 c) { Text::print((int)x, (int)y, s_font, s, size, c); }
void textCentered(float cx, float y, const char* s, int size, u32 c)
{
    Text::print((int)(cx - textWidth(s, size) * 0.5f), (int)y, s_font, s, size, c);
}
void textRight(float xr, float y, const char* s, int size, u32 c)
{
    Text::print((int)(xr - textWidth(s, size)), (int)y, s_font, s, size, c);
}

/* ---- Safe area ------------------------------------------------------------- */

static int   s_safeL = 0, s_safeT = 0, s_safeR = 0, s_safeB = 0;
static float s_sx = 1.0f, s_sy = 1.0f;
static float s_off = 0.0f;           /* extra drawing width on each side (16:9) */

void initScreen(bool widescreen)
{
    s_off = widescreen ? (640.0f * 4.0f / 3.0f - 640.0f) * 0.5f : 0.0f;   /* 106.67 */
    /* GRRLIB's projection maps 0..640 to the frame; widen it so one unit is
     * square on the TV.  Everything (GRRLIB, Text, Ui, video) uses it. */
    Mtx44 m;
    guOrtho(m, 0.0f, 480.0f, -s_off, 640.0f + s_off, 0.0f, 1000.0f);
    GX_LoadProjectionMtx(m, GX_ORTHOGRAPHIC);
    /* glyphs rasterised at frame resolution (see Text::setFrameMapping) */
    Text::setFrameMapping(-s_off, 640.0f / (640.0f + 2.0f * s_off));
}

float screenLeft()  { return -s_off; }
float screenRight() { return 640.0f + s_off; }

void pointerToScreen(ir_t& ir)
{
    if (!ir.valid || s_off == 0.0f) return;
    ir.x = -s_off + ir.x * (640.0f + 2.0f * s_off) / 640.0f;
}

void setSafeArea(int l, int t, int r, int b)
{
    auto clampi = [](int v, int hi) { return v < 0 ? 0 : (v > hi ? hi : v); };
    s_safeL = clampi(l, SAFE_MAX_X); s_safeR = clampi(r, SAFE_MAX_X);
    s_safeT = clampi(t, SAFE_MAX_Y); s_safeB = clampi(b, SAFE_MAX_Y);
    s_sx = (640.0f - s_safeL - s_safeR) / 640.0f;
    s_sy = (480.0f - s_safeT - s_safeB) / 480.0f;
    /* GRRLIB's orthographic projection covers 640x480; the viewport decides
     * where that lands in the frame.  Everything outside stays black (the
     * frame is cleared on every copy to the TV). */
    GX_SetViewport((f32)s_safeL, (f32)s_safeT, 640.0f * s_sx, 480.0f * s_sy, 0.0f, 1.0f);
}

void safeArea(int& l, int& t, int& r, int& b) { l = s_safeL; t = s_safeT; r = s_safeR; b = s_safeB; }

void clip(float x, float y, float w, float h)
{
    /* drawing units -> frame pixels: aspect widening, then the safe area */
    const float k = 640.0f / (640.0f + 2.0f * s_off);
    GRRLIB_ClipDrawing((int)(s_safeL + (x + s_off) * k * s_sx), (int)(s_safeT + y * s_sy),
                       (int)(w * k * s_sx + 0.5f), (int)(h * s_sy + 0.5f));
}

void clipReset() { GRRLIB_ClipReset(); }

/* ---- Components --------------------------------------------------------- */

void background(bool withBand)
{
    const Palette& p = pal();
    beginShapes();
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    const float L = screenLeft(), R = screenRight();
    vtx(L, 0, p.bgTop); vtx(R, 0, p.bgTop); vtx(R, 480, p.bgBottom); vtx(L, 480, p.bgBottom);
    GX_End();
    /* Fine horizontal pinstripes, like the Wii Menu */
    if (p.stripe & 0xFF) {
        GX_Begin(GX_QUADS, GX_VTXFMT0, 4 * 120);
        for (int y = 0; y < 480; y += 4) {
            vtx(L, y, p.stripe); vtx(R, y, p.stripe); vtx(R, y + 2, p.stripe); vtx(L, y + 2, p.stripe);
        }
        GX_End();
    }
    if (withBand) drawHeaderBand();
}

float headerBand() { return s_theme == Theme::Flix ? 58.0f : 0.0f; }   /* the channel's band, measured */

/* the logo at the band's left: every screen's */
static GRRLIB_texImg* s_logo = nullptr;
static const float LOGO_X = 18, LOGO_H = 36;
void setLogo(GRRLIB_texImg* logo) { s_logo = logo; }
static float logoRight()
{
    if (!s_logo || s_logo->h == 0) return screenLeft();
    return screenLeft() + LOGO_X + LOGO_H * s_logo->w / s_logo->h * WiiUtils::wsScaleX();
}
float bandLeft()
{
    const float x = logoRight() + 14;
    return x > 20 ? x : 20;   /* 16:9: the logo is out in the side room */
}

void drawHeaderBand()
{
    const float h = headerBand();
    if (h <= 0) return;
    /* the old video channels' red title band: one flat red, a soft shadow
     * under it */
    const float L = screenLeft(), R = screenRight();
    beginShapes();
    GX_Begin(GX_QUADS, GX_VTXFMT0, 8);
    vtx(L, 0, RED); vtx(R, 0, RED); vtx(R, h, RED); vtx(L, h, RED);
    vtx(L, h, 0x00000040); vtx(R, h, 0x00000040); vtx(R, h + 3, 0x00000000); vtx(L, h + 3, 0x00000000);
    GX_End();
    if (s_logo) {
        const float w = LOGO_H * s_logo->w / s_logo->h * WiiUtils::wsScaleX();
        GRRLIB_DrawImg(screenLeft() + LOGO_X, (h - LOGO_H) * 0.5f, s_logo, 0,
                       w / s_logo->w, LOGO_H / s_logo->h, 0xFFFFFFFF);
    }
}

void card(float x, float y, float w, float h, float r, float focus)
{
    const Palette& p = pal();
    shadow(x + 1, y + 3, w - 2, h - 2, r, 6.0f, p.shadow);
    if (focus > 0.0f) shadow(x - 2, y - 2, w + 4, h + 4, r + 2, 9.0f, alpha(p.glow, focus));
    roundRect(x, y, w, h, r, p.cardTop, p.cardBottom);
    roundBorder(x, y, w, h, r, focus > 0.0f ? 2.0f + focus : 2.0f, mix(p.cardBorder, p.accent, focus));
}

void grow(float& x, float& y, float& w, float& h, float by)
{
    x -= w * (by - 1.0f) * 0.5f;
    y -= h * (by - 1.0f) * 0.5f;
    w *= by;
    h *= by;
}

void button(float x, float y, float w, float h, const char* label, int size, float focus, float radius)
{
    const Palette& p = pal();
    label = tr(label);   /* buttons, hints, tabs and keys say the interface's words */
    /* Flix: the button under the pointer (or the D-pad) grows a little and
     * turns orange, its label white, like the old video channels' Play */
    const bool orange = theme() == Theme::Flix && focus > 0.0f;
    if (orange) grow(x, y, w, h, 1.0f + (HOVER_GROW - 1.0f) * focus);
    const float r = radius < 0.0f ? h * 0.5f : radius;
    card(x, y, w, h, r, orange ? 0.0f : focus);
    if (orange) {
        shadow(x - 2, y, w + 4, h + 3, r + 2, 8.0f, alpha(0xF7922E80, focus));
        roundRect(x, y, w, h, r, alpha(HOVER_TOP, focus), alpha(HOVER_BOTTOM, focus));
        roundBorder(x, y, w, h, r, 1.5f, alpha(HOVER_EDGE, focus));
    }
    /* glossy top highlight */
    const float gi = radius < 0.0f ? h * 0.3f : 3.0f;   /* its inset from the ends */
    roundRect(x + gi, y + 3, w - gi * 2, h * 0.36f, radius < 0.0f ? h * 0.18f : (r < 3 ? r : 3),
              alpha(0xFFFFFFFF, theme() == Theme::Light || theme() == Theme::Flix ? 0.55f * (1.0f - 0.6f * focus) : 0.06f),
              alpha(0xFFFFFFFF, 0.0f));
    textCentered(x + w * 0.5f, y + (h - size) * 0.5f - 1, label, size,
                 orange ? mix(p.text, 0xFFFFFFFF, focus) : mix(p.text, p.accentDark, focus));
}

void header(const char* title, const char* subtitle)
{
    const Palette& p = pal();
    text(headerBand() > 0 ? bandLeft() : 28, headerBand() > 0 ? (headerBand() - 22) * 0.5f : 18, title, 22,
         headerBand() > 0 ? 0xFFFFFFFF : p.text);
    if (subtitle) textRight(612, headerBand() > 0 ? (headerBand() - 15) * 0.5f : 24, subtitle, 15,
                            headerBand() > 0 ? 0xFFFFFFC0 : p.textDim);
    if (headerBand() <= 0) roundRect(28, 52, 584, 2, 1, alpha(p.cardBorder, 0.8f));
}

/* D-pad hint: the same round badge as the buttons, with two arrowheads. */
static void dpadGlyph(float cx, float cy, bool vertical, u32 c)
{
    const Palette& p = pal();
    circle(cx, cy, 9, p.cardBorder);
    circle(cx, cy, 7.5f, p.cardTop);
    const float a = 5.5f, b = 1.2f, hw = 3.4f;     /* tip, base, half width */
    if (vertical) {
        triangle(cx, cy - a, cx + hw, cy - b, cx - hw, cy - b, c);
        triangle(cx, cy + a, cx - hw, cy + b, cx + hw, cy + b, c);
    } else {
        triangle(cx - a, cy, cx - b, cy - hw, cx - b, cy + hw, c);
        triangle(cx + a, cy, cx + b, cy + hw, cx + b, cy - hw, c);
    }
}

/* Width of the glyph drawn for a hint button (0 = label only). */
static ButtonStyle s_buttonStyle = ButtonStyle::WiiRemote;
void        setButtonStyle(ButtonStyle s) { s_buttonStyle = s; }
ButtonStyle buttonStyle()                 { return s_buttonStyle; }

const char* buttonName(const char* b)
{
    /* "+!": + as an action of its own (Input::isActionPressed), Z on a
     * GameCube controller (its R, the page turner, is a trigger) */
    if (strcmp(b, "+!") == 0) return s_buttonStyle == ButtonStyle::GameCube ? "Z" : "+";
    if (s_buttonStyle == ButtonStyle::WiiRemote) return b;
    static const struct { const char* wii; const char* classic; const char* gc; } MAP[] = {
        { "1",    "Y",    "Y" },
        { "2",    "X",    "X" },
        { "-",    "-",    "L" },
        { "+",    "+",    "R" },
        { "-/+",  "-/+",  "L/R" },
        { "HOME", "HOME", "START" },
    };
    for (const auto& m : MAP)
        if (strcmp(b, m.wii) == 0)
            return s_buttonStyle == ButtonStyle::Classic ? m.classic : m.gc;
    return b;
}

/* GameCube A and B are green and red; everything else is a grey button */
static void buttonDisc(float cx, float cy, const char* b, u32& textColor)
{
    const Palette& p = pal();
    u32 fill = p.cardTop;
    textColor = p.text;
    if (s_buttonStyle == ButtonStyle::GameCube && (b[0] == 'A' || b[0] == 'B') && !b[1]) {
        fill = b[0] == 'A' ? 0x2FB56AFF : 0xD8463CFF;
        textColor = 0xFFFFFFFF;
    }
    /* the Wii Remote's B is a trigger: a square, as in the Wii's own menus */
    if (s_buttonStyle == ButtonStyle::WiiRemote && b[0] == 'B' && !b[1]) {
        roundRect(cx - 9, cy - 9, 18, 18, 4.5f, p.cardBorder);
        roundRect(cx - 7.5f, cy - 7.5f, 15, 15, 3.5f, fill);
        return;
    }
    circle(cx, cy, 9, p.cardBorder);
    circle(cx, cy, 7.5f, fill);
}

static float glyphWidth(const char* b)
{
    if (!b[0]) return 0;
    if (strcmp(b, "UD") == 0 || strcmp(b, "LR") == 0) return 18;
    if (strcmp(b, "-/+") == 0 || strcmp(b, "L/R") == 0) return 40;
    if (strlen(b) > 1) return textWidth(b, 10) + 16;   /* HOME ... */
    return 18;
}

/* Minus / plus drawn as bars: a 12 px "-" character is a single thin row of
 * pixels that vanishes on the TV (and when the picture is scaled). */
static void minusGlyph(float cx, float cy, u32 c) { roundRect(cx - 4.5f, cy - 1.25f, 9, 2.5f, 1.25f, c); }
static void plusGlyph(float cx, float cy, u32 c)
{
    roundRect(cx - 4.5f, cy - 1.25f, 9, 2.5f, 1.25f, c);
    roundRect(cx - 1.25f, cy - 4.5f, 2.5f, 9, 1.25f, c);
}

float hint(float x, float y, const Hint& h)
{
    const Palette& p = pal();
    const char* b = buttonName(h.button);
    float cy = y + 9, w = glyphWidth(b);
    u32 tc = p.text;
    if (w == 0) {
        /* plain caption */
    } else if (strcmp(b, "UD") == 0 || strcmp(b, "LR") == 0) {
        dpadGlyph(x + 9, cy, b[0] == 'U', p.text);
    } else if (strcmp(b, "-/+") == 0 || strcmp(b, "L/R") == 0) {
        /* two small buttons side by side */
        for (int i = 0; i < 2; ++i) {
            float bx = x + 9 + i * 22;
            buttonDisc(bx, cy, "", tc);
            if (b[0] == 'L') textCentered(bx, cy - 7, i == 0 ? "L" : "R", 12, tc);
            else if (i == 0) minusGlyph(bx, cy, tc);
            else             plusGlyph(bx, cy, tc);
        }
    } else if (strlen(b) > 1) {
        roundRect(x, cy - 8, w, 16, 8, p.cardTop, p.cardBottom);
        roundBorder(x, cy - 8, w, 16, 8, 1.5f, p.cardBorder);
        textCentered(x + w * 0.5f, cy - 6, b, 10, p.textDim);
    } else {
        buttonDisc(x + 9, cy, b, tc);
        if      (b[0] == '-') minusGlyph(x + 9, cy, tc);
        else if (b[0] == '+') plusGlyph(x + 9, cy, tc);
        else textCentered(x + 9, cy - 7, b, 12, tc);
    }
    if (w > 0) w += 5;
    text(x + w, y + 2, tr(h.label), 14, p.textDim);
    return w + textWidth(tr(h.label), 14) + 14;
}

float hintWidth(const Hint& h)
{
    float g = glyphWidth(buttonName(h.button));
    return (g > 0 ? g + 5 : 0) + textWidth(tr(h.label), 14) + 14;
}

/* WiiFlix: no controls bar along the bottom (the old video channels had
 * none), so these draw nothing; the calls stay where each screen says what
 * its buttons do */
void bottomBar(const Hint*, int, const Hint*, int) {}

void footer(const Hint*, int, const Hint*, int, const char*) {}

static struct { float x[8], w[8], y, h; int n; u64 at; } s_tabBar;

int tabAt(float x, float y)
{
    if (ticks_to_millisecs(gettime()) - s_tabBar.at > 100) return -1;
    if (y < s_tabBar.y || y >= s_tabBar.y + s_tabBar.h) return -1;
    for (int i = 0; i < s_tabBar.n; ++i)
        if (x >= s_tabBar.x[i] && x < s_tabBar.x[i] + s_tabBar.w[i]) return i;
    return -1;
}

float tabs(float cx, float y, const char* const* names, int n, int sel, int size)
{
    const Palette& p = pal();
    const float pad = 13, h = size + 14;
    float w[8], total = 0;
    if (n > 8) n = 8;
    for (int i = 0; i < n; ++i) { w[i] = textWidth(tr(names[i]), size) + pad * 2; total += w[i]; }
    float x0 = cx - total * 0.5f;
    shadow(x0, y + 1, total, h, h * 0.5f, 4.0f, p.shadow);
    roundRect(x0, y, total, h, h * 0.5f, p.field, mix(p.field, p.cardBottom, 0.5f));
    roundBorder(x0, y, total, h, h * 0.5f, 1.5f, p.fieldBorder);
    float x = x0;
    for (int i = 0; i < n; ++i) {
        if (i == sel) {
            roundRect(x + 2, y + 2, w[i] - 4, h - 4, (h - 4) * 0.5f,
                      mix(p.accent, 0xFFFFFFFF, 0.25f), p.accentDark);
            textCentered(x + w[i] * 0.5f, y + (h - size) * 0.5f - 1, tr(names[i]), size, p.textOnAccent);
        } else {
            textCentered(x + w[i] * 0.5f, y + (h - size) * 0.5f - 1, tr(names[i]), size, p.textDim);
        }
        s_tabBar.x[i] = x; s_tabBar.w[i] = w[i];
        x += w[i];
    }
    s_tabBar.y = y; s_tabBar.h = h; s_tabBar.n = n; s_tabBar.at = ticks_to_millisecs(gettime());
    return x0;
}

/* Round button with a triangle pointing dir (0 up, 1 down, 2 left, 3 right). */
static void roundArrow(float cx, float cy, float r0, int dir, bool enabled, bool hover)
{
    const Palette& p = pal();
    float k   = enabled ? 1.0f : 0.35f;
    bool  hot = hover && enabled;
    float rad = hot ? r0 * HOVER_GROW : r0;
    shadow(cx - rad, cy - rad + 2, rad * 2, rad * 2, rad, 5.0f, alpha(p.shadow, k));
    if (hot) {
        shadow(cx - rad - 2, cy - rad - 1, rad * 2 + 4, rad * 2 + 4, rad + 2, 8.0f, 0xF7922E80);
        roundRect(cx - rad, cy - rad, rad * 2, rad * 2, rad, HOVER_TOP, HOVER_BOTTOM);
        roundBorder(cx - rad, cy - rad, rad * 2, rad * 2, rad, 1.5f, HOVER_EDGE);
    } else {
        roundRect(cx - rad, cy - rad, rad * 2, rad * 2, rad, alpha(p.cardTop, k), alpha(p.cardBottom, k));
        roundBorder(cx - rad, cy - rad, rad * 2, rad * 2, rad, 1.5f, alpha(p.cardBorder, k));
    }
    u32 c = hot ? 0xFFFFFFFF : enabled ? p.accent : alpha(p.textDim, 0.5f);
    r0 = rad;
    float a = r0 * 0.47f, w = r0 * 0.37f;     /* triangle half-length / half-width */
    switch (dir) {
    case 0: triangle(cx, cy - a, cx + a, cy + w, cx - a, cy + w, c); break;
    case 1: triangle(cx - a, cy - w, cx + a, cy - w, cx, cy + a, c); break;
    case 2: triangle(cx - a, cy, cx + w, cy - a, cx + w, cy + a, c); break;
    default:triangle(cx + a, cy, cx - w, cy + a, cx - w, cy - a, c); break;
    }
}

void arrowButton(float cx, float cy, bool up, bool enabled, bool hover)
{
    roundArrow(cx, cy, 15, up ? 0 : 1, enabled, hover);
}

void pageArrow(float cx, float cy, bool left, bool enabled, bool hover)
{
    roundArrow(cx, cy, 21, left ? 2 : 3, enabled, hover);
}

/* a fan of triangles from the centre to the ten points */
static void starShape(float cx, float cy, float r, u32 col)
{
    float px[10], py[10];
    for (int i = 0; i < 10; ++i) {
        const float ang = -1.5707963f + i * 0.6283185f;      /* first point up */
        const float rr  = i & 1 ? r * 0.42f : r;
        px[i] = cx + cosf(ang) * rr;
        py[i] = cy + sinf(ang) * rr;
    }
    for (int i = 0; i < 10; ++i)
        triangle(cx, cy, px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], col);
}

void star(float cx, float cy, float r, float fill, u32 color, u32 empty)
{
    if (fill >= 0.99f) { starShape(cx, cy, r, color); return; }
    starShape(cx, cy, r, empty);
    if (fill <= 0.01f) return;
    clip(cx - r, cy - r, r * 2 * fill, r * 2);
    starShape(cx, cy, r, color);
    clipReset();
}

float stars(float x, float cy, float r, float rating10, u32 color, u32 empty)
{
    const float halves = floorf(rating10 + 0.5f) * 0.5f;     /* 0..5 by halves */
    const float gap = r * 0.35f;
    for (int i = 0; i < 5; ++i) {
        float f = halves - i;
        star(x + r + i * (r * 2 + gap), cy, r, f < 0 ? 0 : f > 1 ? 1 : f, color, empty);
    }
    return 5 * r * 2 + 4 * gap;
}

void arrowHead(float cx, float cy, int dir, float w, float h, bool enabled, bool hot)
{
    const Palette& p = pal();
    hot = hot && enabled;
    const float g = hot ? HOVER_GROW : 1.0f, s = (float)dir;
    const float hw = w * 0.5f * g, hh = h * 0.5f * g;
    /* the old channels' arrowhead: a shallow notch, 18% of its length */
    const float tipX = cx + s * hw, backX = cx - s * hw, notchX = backX + s * (2 * hw) * 0.18f;
    /* its upper half, then its lower half (lit from above: a shade darker) */
    auto head = [&](float dy, float grow, u32 upper, u32 lower) {
        triangle(tipX + s * grow, cy + dy, backX - s * grow, cy - hh + dy - grow, notchX, cy + dy, upper);
        triangle(tipX + s * grow, cy + dy, notchX, cy + dy, backX - s * grow, cy + hh + dy + grow, lower);
    };
    const float k = enabled ? 1.0f : 0.35f;
    const u32 sh = alpha(0x00000050, k);
    head(3.5f, 0.5f, sh, sh);                                                   /* soft shadow below */
    const u32 edge = alpha(hot ? HOVER_EDGE : p.cardBorder, k);
    head(0, 1.2f, edge, edge);                                                  /* outline */
    head(0, 0, alpha(hot ? HOVER_TOP : p.cardTop, k),
               alpha(hot ? HOVER_BOTTOM : mix(p.cardBottom, p.cardBorder, 0.35f), k));
}

void backButton(float cx, float cy, float r, bool hover)
{
    const Palette& p = pal();
    if (hover) r *= HOVER_GROW;
    shadow(cx - r, cy - r + 3, r * 2, r * 2, r, 6.0f, p.shadow);
    if (hover) {
        roundRect(cx - r, cy - r, r * 2, r * 2, r, HOVER_TOP, HOVER_BOTTOM);
        roundBorder(cx - r, cy - r, r * 2, r * 2, r, 2.0f, HOVER_EDGE);
    } else {
        circle(cx, cy, r, 0xF2F2F2FF);
        roundBorder(cx - r, cy - r, r * 2, r * 2, r, 2.0f, 0xB0B0B0FF);
    }
    /* the arrow, as the old video channels drew it: a thick ring going
     * clockwise from the top round the right and the bottom to the left
     * side, where its head points up (the open quarter at the top left) */
    const u32 ink = hover ? 0xFFFFFFFF : 0x2B2B2BFF;
    const float ar = r * 0.48f, th = r * 0.15f;          /* the ring's middle, half its width */
    const float a0 = -1.31f, a1 = 3.32f;                 /* -75 deg .. 190 deg, y down: clockwise */
    const int   N  = 36;
    for (int i = 0; i < N; ++i) {
        const float u = a0 + (a1 - a0) * i / N, v = a0 + (a1 - a0) * (i + 1) / N;
        const float ox0 = cx + cosf(u) * (ar + th), oy0 = cy + sinf(u) * (ar + th);
        const float ix0 = cx + cosf(u) * (ar - th), iy0 = cy + sinf(u) * (ar - th);
        const float ox1 = cx + cosf(v) * (ar + th), oy1 = cy + sinf(v) * (ar + th);
        const float ix1 = cx + cosf(v) * (ar - th), iy1 = cy + sinf(v) * (ar - th);
        triangle(ox0, oy0, ox1, oy1, ix1, iy1, ink);
        triangle(ox0, oy0, ix1, iy1, ix0, iy0, ink);
    }
    circle(cx + cosf(a0) * ar, cy + sinf(a0) * ar, th, ink);   /* the tail's round end */
    /* the head, at a1: its base across the ring, its tip ahead, clockwise */
    const float hx = cx + cosf(a1) * ar, hy = cy + sinf(a1) * ar;
    const float tx = -sinf(a1), ty = cosf(a1);                 /* along the ring, clockwise */
    const float nx = cosf(a1),  ny = sinf(a1);                 /* outward                   */
    const float hw = th * 2.1f, hl = th * 2.3f;                /* half its base, its length */
    triangle(hx + nx * hw, hy + ny * hw, hx - nx * hw, hy - ny * hw, hx + tx * hl, hy + ty * hl, ink);
}

void progress(float x, float y, float w, float h, float frac)
{
    const Palette& p = pal();
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    roundRect(x, y, w, h, h * 0.5f, 0x00000080);
    if (frac > 0) roundRect(x, y, w * frac < h ? h : w * frac, h, h * 0.5f,
                            mix(p.accent, 0xFFFFFFFF, 0.2f), p.accent);
}

void scrollbar(float x, float y, float h, int first, int visible, int total)
{
    const Palette& p = pal();
    if (total <= visible) return;
    float barH = h * visible / total;
    if (barH < 20) barH = 20;
    int maxTop = total - visible;
    float barY = y + (maxTop > 0 ? (h - barH) * first / maxTop : 0);
    roundRect(x, y, 6, h, 3, alpha(p.cardBorder, 0.5f));
    roundRect(x, barY, 6, barH, 3, p.accent, p.accentDark);
}

void key(float x, float y, float w, float h, const char* label,
         bool sel, bool hover, bool special, bool latched)
{
    const Palette& p = pal();
    label = tr(label);
    if (sel || hover) grow(x, y, w, h, HOVER_GROW);
    if (sel || hover) {   /* the key chosen, or under the pointer: orange */
        shadow(x - 2, y - 1, w + 4, h + 3, 9, 7.0f, 0xF7922E80);
        roundRect(x, y, w, h, 7, HOVER_TOP, HOVER_BOTTOM);
        roundBorder(x, y, w, h, 7, 1.5f, HOVER_EDGE);
    } else if (latched) {
        roundRect(x, y, w, h, 7, 0xF7B54AFF, 0xE08E1BFF);
    } else {
        u32 top = special ? mix(p.cardBottom, p.cardBorder, 0.35f) : p.cardTop;
        roundRect(x, y, w, h, 7, top, p.cardBottom);
        roundBorder(x, y, w, h, 7, 1.0f, alpha(p.cardBorder, 0.8f));
    }
    /* short ASCII words (SYM, ABC) get a smaller size than single glyphs */
    int sz = 15;
    if (strlen(label) > 1 && (unsigned char)label[0] < 0x80) sz = w > 60 ? 13 : 11;
    u32 tc = (sel || hover || latched) ? p.textOnAccent : (special ? p.textDim : p.text);
    textCentered(x + w * 0.5f, y + (h - sz) * 0.5f - 1, label, sz, tc);
}

void field(float x, float y, float w, float h, const char* value, int size, bool focused)
{
    const Palette& p = pal();
    /* rectangular; chosen (typed in, or under the pointer): an orange edge */
    if (focused) shadow(x - 2, y - 1, w + 4, h + 3, 5, 8.0f, 0xF7922E80);
    else         shadow(x, y + 1, w, h, 3, 4.0f, p.shadow);
    roundRect(x, y, w, h, 3, p.field);
    roundBorder(x, y, w, h, 3, focused ? 2.5f : 1.5f, focused ? HOVER_TOP : p.fieldBorder);
    if (value) text(x + 12, y + (h - size) * 0.5f - 1, value, size, p.text);
}

static Mtx s_savedView;
static int s_offsetDepth = 0;

void pushOffset(float dx, float dy)
{
    if (s_offsetDepth++ == 0) guMtxCopy(GXmodelView2D, s_savedView);
    guMtxTransApply(GXmodelView2D, GXmodelView2D, dx, dy, 0.0f);
    GX_LoadPosMtxImm(GXmodelView2D, GX_PNMTX0);
}

void popOffset()
{
    if (s_offsetDepth > 0 && --s_offsetDepth == 0) {
        guMtxCopy(s_savedView, GXmodelView2D);
        GX_LoadPosMtxImm(GXmodelView2D, GX_PNMTX0);
    }
}

float pulse()
{
    float t = (float)(ticks_to_millisecs(gettime()) % 1600) / 1600.0f;
    return 0.8f + 0.2f * sinf(t * 2.0f * (float)M_PI);
}

void avatar(float cx, float cy, float r, const char* name, float focus)
{
    const Palette& p = pal();
    /* first character (whole UTF-8 sequence), ASCII upper-cased */
    char initial[5] = { '?', 0, 0, 0, 0 };
    if (name && name[0]) {
        unsigned char c = (unsigned char)name[0];
        int len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        for (int i = 0; i < len && name[i]; ++i) initial[i] = name[i];
        initial[len] = 0;
        if (initial[0] >= 'a' && initial[0] <= 'z') initial[0] -= 32;
    }
    if (focus > 0.0f) shadow(cx - r - 2, cy - r - 2, r * 2 + 4, r * 2 + 4, r + 2, 8.0f, alpha(p.glow, focus));
    else              shadow(cx - r, cy - r + 2, r * 2, r * 2, r, 4.0f, p.shadow);
    roundRect(cx - r, cy - r, r * 2, r * 2, r,
              mix(p.accent, 0xFFFFFFFF, 0.25f + 0.15f * focus), mix(p.accent, 0x000000FF, 0.15f));
    roundBorder(cx - r, cy - r, r * 2, r * 2, r, 2.0f, 0xFFFFFFFF);
    int size = (int)(r * 1.1f);
    textCentered(cx, cy - size * 0.55f, initial, size, 0xFFFFFFFF);
}

void spinner(GRRLIB_texImg* ring, float cx, float cy)
{
    (void)ring;   /* WiiFlix: the old video channels' five blocks instead */
    /* five steel-blue blocks in a row; a swell runs through them left to
     * right (the one it is on bigger and brighter, its neighbours a little),
     * then starts again from the left */
    const float CYCLE = 1000.0f, BW = 7.0f, BH = 14.0f, STEP = 10.0f;
    const float at = (float)(ticks_to_millisecs(gettime()) % (u64)CYCLE) / CYCLE * 6.0f - 0.5f;
    float k[5];
    for (int i = 0; i < 5; ++i) {
        const float d = fabsf(i - at);
        k[i] = d < 1.0f ? 1.0f - d : 0.0f;
    }
    /* the swollen one last, over its neighbours */
    int order[5] = { 0, 1, 2, 3, 4 };
    for (int a = 0; a < 5; ++a)
        for (int b = a + 1; b < 5; ++b)
            if (k[order[b]] < k[order[a]]) { int t = order[a]; order[a] = order[b]; order[b] = t; }
    for (int n = 0; n < 5; ++n) {
        const int i = order[n];
        const float w = BW * (1.0f + 0.9f * k[i]), h = BH * (1.0f + 0.6f * k[i]);
        const float x = cx + (i - 2) * STEP - w * 0.5f, y = cy - h * 0.5f;
        shadow(x, y + 1, w, h, 2, 3.0f, 0x00000050);
        roundRect(x, y, w, h, 2, mix(0x9DB8D8FF, 0xDCEBFAFF, k[i]), mix(0x6684AAFF, 0xA9C6E6FF, k[i]));
        roundBorder(x, y, w, h, 2, 1.0f, 0x3E557480);
    }
}

/* ---- Notice: a message from the server ---------------------------------- */

static std::string s_noticeHeader, s_noticeText;
static u64         s_noticeFrom = 0, s_noticeUntil = 0;

void showNotice(const std::string& header, const std::string& text, int ms)
{
    s_noticeHeader = header;
    s_noticeText   = text;
    s_noticeFrom   = ticks_to_millisecs(gettime());
    s_noticeUntil  = s_noticeFrom + (u64)(ms > 0 ? ms : 5000);
}

void drawNotice()
{
    if (!s_noticeUntil) return;
    u64 now = ticks_to_millisecs(gettime());
    if (now >= s_noticeUntil) { s_noticeUntil = 0; return; }
    /* slides in from the top, fades out over its last 300 ms */
    float in  = (now - s_noticeFrom) / 250.0f;
    float out = (s_noticeUntil - now) / 300.0f;
    float k   = in < 1.0f ? in : 1.0f;
    if (out < k) k = out < 0.0f ? 0.0f : out;

    const Palette& p = pal();
    const int HS = 17, TS = 15, LH = 20, MAXW = 460, PAD = 18;
    /* the text in lines that fit (words; '\n' breaks), four at most */
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= s_noticeText.size() && lines.size() < 4) {
        size_t e = s_noticeText.find('\n', pos);
        std::string para = s_noticeText.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        pos = e == std::string::npos ? s_noticeText.size() + 1 : e + 1;
        std::string cur;
        size_t w = 0;
        while (w <= para.size() && lines.size() < 4) {
            size_t sp = para.find(' ', w);
            std::string word = para.substr(w, sp == std::string::npos ? std::string::npos : sp - w);
            w = sp == std::string::npos ? para.size() + 1 : sp + 1;
            std::string trial = cur.empty() ? word : cur + " " + word;
            if (!cur.empty() && textWidth(trial.c_str(), TS) > MAXW - 2 * PAD) {
                lines.push_back(cur);
                cur = word;
            } else {
                cur = trial;
            }
        }
        if (!cur.empty() && lines.size() < 4) lines.push_back(cur);
    }
    /* TRANSLATORS: heading of a message sent from the server's dashboard without one */
    const std::string& head = s_noticeHeader.empty() ? std::string(tr("Message")) : s_noticeHeader;
    int wNeed = textWidth(head.c_str(), HS);
    for (const auto& l : lines) { int lw = textWidth(l.c_str(), TS); if (lw > wNeed) wNeed = lw; }
    float w = wNeed + 2 * PAD + 6;
    if (w > MAXW) w = MAXW;
    if (w < 220) w = 220;
    float h = PAD + HS + 6 + lines.size() * LH + PAD - 4;
    float x = 320 - w / 2, y = 18 - (1.0f - k) * (h + 30);

    shadow(x, y + 3, w, h, 14, 8.0f, alpha(p.shadow, k));
    roundRect(x, y, w, h, 14, alpha(p.cardTop, k), alpha(p.cardBottom, k));
    roundBorder(x, y, w, h, 14, 1.5f, alpha(p.cardBorder, k));
    roundRect(x + 10, y + 12, 4, h - 24, 2, alpha(p.accent, k));
    text(x + PAD + 6, y + PAD - 4, head.c_str(), HS, alpha(p.accent, k));
    for (size_t i = 0; i < lines.size(); ++i)
        text(x + PAD + 6, y + PAD + HS + 4 + i * LH, lines[i].c_str(), TS, alpha(p.text, k));
}

} // namespace Ui
