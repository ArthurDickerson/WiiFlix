/* LibraryView: the show page (a series' details page, one episode chosen)
 * and its "Select another episode" row. */
#include "LibraryView.h"
#include "LibraryDraw.h"
#include "JpegTexture.h"
#include "Ui.h"
#include "../input/Input.h"
#include "../core/SoundFX.h"
#include "../core/Text.h"
#include "../core/Lang.h"
#include <stdio.h>
#include <math.h>

using namespace LibDraw;

/* ---- The show page ----------------------------------------------------- */

void LibraryView::openShow(const std::string& seriesId, const std::string& seriesName,
                           const std::string& episodeId, State returnState)
{
    currentSeriesId    = seriesId;
    currentSeriesName  = seriesName;
    showReturnState = returnState;
    showStartEpisodeId = episodeId;
    state = State::ShowLoad;
}

void LibraryView::loadShow()
{
    freeDetail();
    if (showTex) { GRRLIB_FreeTexture(showTex); showTex = nullptr; }
    showMode = false;
    showEps.clear();
    showSeries = JellyfinItemDetail();

    bool ok = false;
    std::string err;
    std::vector<JellyfinEpisode> eps;
    const std::string want = showStartEpisodeId;   /* read once, whatever happens */
    showStartEpisodeId.clear();
    runWithLoading([&]() {
        ok = client.getItemDetail(serverUrl, auth, currentSeriesId, showSeries) &&
             client.getSeriesEpisodes(serverUrl, auth, currentSeriesId, eps);
        if (!ok) { err = client.lastError(); return; }
        std::string img;
        client.getItemImageBytes(serverUrl, auth, currentSeriesId, 240, 340, img);
        if (!img.empty()) showTex = loadJPEGTexture((const u8*)img.data(), (u32)img.size());
    });
    /* the specials (season 0) left out, unless there is nothing else */
    bool regular = false;
    for (const auto& e : eps) if (e.seasonNumber > 0) regular = true;
    for (const auto& e : eps) if (!regular || e.seasonNumber > 0) showEps.push_back(e);
    if (ok && showEps.empty()) { ok = false; err = N_("This series has no episodes."); }
    if (!ok) {
        if (showTex) { GRRLIB_FreeTexture(showTex); showTex = nullptr; }
        errMsg = err;
        state  = State::Error;
        return;
    }
    if (showSeries.name.empty()) showSeries.name = currentSeriesName;

    /* the episode shown: the one asked for; else the last one started if
     * it is not finished, else the next unwatched after it, else the first
     * unwatched, else the first */
    const int n = (int)showEps.size();
    int pick = -1;
    for (int i = 0; i < n; ++i) if (showEps[i].id == want) pick = i;
    if (pick < 0) {
        int last = -1;
        for (int i = 0; i < n; ++i)
            if (showEps[i].played || showEps[i].playbackPositionTicks > 0) last = i;
        if (last >= 0 && !showEps[last].played) pick = last;
        for (int i = last + 1; pick < 0 && i < n; ++i) if (!showEps[i].played) pick = i;
        for (int i = 0; pick < 0 && i < n; ++i)        if (!showEps[i].played) pick = i;
        if (pick < 0) pick = 0;
    }
    showMode  = true;
    showEpIdx = pick;
    SYS_Report("[Show] %s: %d episodes, episode %d shown\n", showSeries.name.c_str(), n, pick + 1);

    detailItemId        = showEps[pick].id;
    detailReturnState   = showReturnState;
    detailIsEpisodeHint = true;
    loadDetail();
    if (state != State::DetailReady) {   /* its error shown: the show is left */
        showMode = false;
        if (showTex) { GRRLIB_FreeTexture(showTex); showTex = nullptr; }
    }
}

void LibraryView::leaveDetail()
{
    freeDetail();
    if (showMode) {
        showMode = false;
        if (showTex) { GRRLIB_FreeTexture(showTex); showTex = nullptr; }
        showEps.clear();
        showSeries = JellyfinItemDetail();
    }
    state = detailReturnState;
}

/* ---- The episode row ---------------------------------------------------
 * The channel's: under the title, one long row of numbered tiles (every
 * episode, season after season) between two arrowheads, the chosen one in
 * an orange frame, a little bigger; a red bar on those started.  Under it,
 * joined to the chosen tile by a small point, a panel with its season and
 * episode, name, rating, year, length and overview. */
namespace {

const float MARGIN = 24;
const float BACK_R = 30, BACK_CY = 58;   /* as the details page's */
float backCx() { return Ui::screenRight() - 59; }

const float EP_TILE_W = 64, EP_TILE_H = 68, PITCH = 83;
const float ROW_Y  = 172;                       /* the tiles' top */
const float ARROW_W = 26, ARROW_H = 34;
float arrowCx(int dir) { return dir < 0 ? Ui::screenLeft() + 24 : Ui::screenRight() - 24; }
float areaL() { return Ui::screenLeft() + 46; }
float areaR() { return Ui::screenRight() - 46; }
/* tiles that fit between the arrows, and where the first one goes */
int   visibleTiles() { return (int)((areaR() - areaL() + (PITCH - EP_TILE_W)) / PITCH); }
float rowX0()
{
    const int v = visibleTiles();
    return (areaL() + areaR()) * 0.5f - (v * PITCH - (PITCH - EP_TILE_W)) * 0.5f;
}

const float PANEL_Y = 264, PANEL_H = 158;
const float PAD = 18;
const int   OV_SIZE = 15, OV_LINES = 4;
const float OV_STEP = 19;

const u32 TILE_TOP = 0xF6F6F6FF, TILE_BOT = 0xC8C8C8FF, TILE_EDGE = 0x8E8E8EFF;
const u32 BAR_RED  = 0x8B0A0AFF;

bool nearBox(const ir_t& ir, float cx, float cy, float hw, float hh)
{
    return ir.valid && fabsf(ir.x - cx) < hw && fabsf(ir.y - cy) < hh;
}

}  // namespace

bool LibraryView::updateEpisodePick(ir_t& ir, bool aPressed)
{
    const int n = (int)showEps.size();
    const int vis = visibleTiles();
    if (n == 0) { state = State::DetailReady; return false; }

    /* what the pointer is on: a tile, an arrow, the back button */
    int over = -1, arrow = 0;
    bool back = false;
    if (ir.valid) {
        const float x0 = rowX0();
        if (ir.y >= ROW_Y && ir.y < ROW_Y + EP_TILE_H && ir.x >= areaL() - 4 && ir.x < areaR() + 4) {
            const int i = (int)floorf((ir.x - x0) / PITCH + epScroll);
            const float tx = x0 + (i - epScroll) * PITCH;
            if (i >= 0 && i < n && ir.x >= tx && ir.x < tx + EP_TILE_W) over = i;
        }
        if (nearBox(ir, arrowCx(-1), ROW_Y + EP_TILE_H * 0.5f, ARROW_W * 0.5f + 8, ARROW_H * 0.5f + 6)) arrow = -1;
        if (nearBox(ir, arrowCx(+1), ROW_Y + EP_TILE_H * 0.5f, ARROW_W * 0.5f + 8, ARROW_H * 0.5f + 6)) arrow = +1;
        const float dx = ir.x - backCx(), dy = ir.y - BACK_CY;
        back = dx * dx + dy * dy < (BACK_R + 4) * (BACK_R + 4);
    }
    /* one choice: the pointer takes it when it moves onto another tile,
     * the D-pad moves it too */
    if (over >= 0 && over != epHover) { epSel = over; epDpad = false; }
    epHover = over;

    const int was = epSel;
    auto page = [&](int dir) {   /* a screenful on, the choice along */
        epFirst += dir * vis;
        epSel   += dir * vis;
    };
    if (Input::isLeftPressed()  && epSel > 0)     { --epSel; epDpad = true; }
    if (Input::isRightPressed() && epSel + 1 < n) { ++epSel; epDpad = true; }
    if (Input::isLPressed()) { page(-1); epDpad = true; }   /* - */
    if (Input::isRPressed()) { page(+1); epDpad = true; }   /* + */
    if (aPressed && arrow) { page(arrow); aPressed = false; }
    if (epSel < 0) epSel = 0;
    if (epSel >= n) epSel = n - 1;
    /* the row scrolls only when the choice goes past an edge */
    if (epSel < epFirst) epFirst = epSel;
    if (epSel >= epFirst + vis) epFirst = epSel - vis + 1;
    const int maxFirst = n > vis ? n - vis : 0;
    if (epFirst > maxFirst) epFirst = maxFirst;
    if (epFirst < 0) epFirst = 0;
    if (epSel != was) SoundFX::play(SoundFX::FX::Move);

    if (Input::isBackPressed() || (aPressed && back)) {
        SoundFX::play(SoundFX::FX::Back);
        state = State::DetailReady;
        return false;
    }
    /* A: the episode chosen, on the show page (with the pointer off the
     * tiles, only if the D-pad chose it) */
    if (aPressed && (!ir.valid || over >= 0 || epDpad)) {
        SoundFX::play(SoundFX::FX::Start);
        showEpIdx           = epSel;
        detailItemId        = showEps[epSel].id;
        detailIsEpisodeHint = true;
        state = State::DetailLoad;
    }
    return false;
}

void LibraryView::renderEpisodePick(ir_t& ir)
{
    const Ui::Palette& p = Ui::pal();
    const int n = (int)showEps.size();
    if (n == 0) return;
    epScroll = Ui::approach(epScroll, (float)epFirst, 0.3f);
    if (fabsf(epScroll - epFirst) < 0.01f) epScroll = (float)epFirst;

    /* the title and the back button, as on the show page */
    {
        const bool back = ir.valid && (ir.x - backCx()) * (ir.x - backCx()) +
                          (ir.y - BACK_CY) * (ir.y - BACK_CY) < (BACK_R + 4) * (BACK_R + 4);
        Ui::backButton(backCx(), BACK_CY, BACK_R, back);
        const int w = (int)(backCx() - BACK_R - 12 - MARGIN);
        titleText(2, MARGIN, 66, w, filterDejaVu(showSeries.name, 200), 20, p.text, true);
    }

    /* the arrows */
    const int vis = visibleTiles();
    const float cy = ROW_Y + EP_TILE_H * 0.5f;
    {
        bool hl = false, hr = false;
        if (ir.valid) {
            hl = nearBox(ir, arrowCx(-1), cy, ARROW_W * 0.5f + 8, ARROW_H * 0.5f + 6);
            hr = nearBox(ir, arrowCx(+1), cy, ARROW_W * 0.5f + 8, ARROW_H * 0.5f + 6);
        }
        Ui::arrowHead(arrowCx(-1), cy, -1, ARROW_W, ARROW_H, epFirst > 0, hl);
        Ui::arrowHead(arrowCx(+1), cy, +1, ARROW_W, ARROW_H, epFirst + vis < n, hr);
    }

    /* the panel first: the chosen tile's point and frame go over it */
    Ui::shadow(MARGIN, PANEL_Y + 3, 640 - 2 * MARGIN, PANEL_H, 4, 8.0f, 0x00000050);
    Ui::roundRect(MARGIN, PANEL_Y, 640 - 2 * MARGIN, PANEL_H, 4, p.cardTop, p.cardBottom);
    Ui::roundBorder(MARGIN, PANEL_Y, 640 - 2 * MARGIN, PANEL_H, 4, 1.5f, p.cardBorder);

    /* the tiles, clipped between the arrows; the chosen one last */
    const float x0 = rowX0();
    const int lo = (int)floorf(epScroll) - 1, hi = (int)ceilf(epScroll) + vis + 1;
    float selX = -1000.0f;
    Ui::clip(areaL() - 10, ROW_Y - 10, areaR() - areaL() + 20, EP_TILE_H + 20);
    for (int pass = 0; pass < 2; ++pass)
        for (int i = lo < 0 ? 0 : lo; i < n && i <= hi; ++i) {
            const bool sel = i == epSel;
            if (sel != (pass == 1)) continue;
            const JellyfinEpisode& e = showEps[i];
            float x = x0 + (i - epScroll) * PITCH, y = ROW_Y, w = EP_TILE_W, h = EP_TILE_H;
            if (sel) {
                selX = x + w * 0.5f;
                Ui::grow(x, y, w, h, Ui::HOVER_GROW);
                Ui::shadow(x - 3, y - 2, w + 6, h + 5, 5, 8.0f, 0xF7922E90);
            } else {
                Ui::shadow(x, y + 2, w, h, 3, 4.0f, 0x00000060);
            }
            Ui::roundRect(x, y, w, h, 3, TILE_TOP, TILE_BOT);
            if (sel) Ui::roundBorder(x, y, w, h, 3, 3.0f, Ui::HOVER_TOP);
            else     Ui::roundBorder(x, y, w, h, 3, 1.0f, TILE_EDGE);
            /* the number: grey once watched, dark and bolder until then */
            const bool started = !e.played && e.playbackPositionTicks > 0 && e.runtimeTicks > 0;
            char num[12];
            snprintf(num, sizeof(num), "%d", i + 1);
            const int size = (int)((i + 1 < 100 ? 28 : 24) * (sel ? Ui::HOVER_GROW : 1.0f));
            /* the same height on every tile, room for the bar under it */
            const float ty = y + h * 0.44f - size * 0.55f;
            if (e.played) {
                Ui::textCentered(x + w * 0.5f, ty, num, size, 0x8C8C8CFF);
            } else {
                Ui::textCentered(x + w * 0.5f, ty, num, size, 0x1E1E1EFF);
                Ui::textCentered(x + w * 0.5f + 1, ty, num, size, 0x1E1E1EFF);
            }
            /* started: how far, in red on white */
            if (started) {
                float f = (float)e.playbackPositionTicks / (float)e.runtimeTicks;
                if (f > 1.0f) f = 1.0f;
                const float bx = x + 7, by = y + h - 16, bw = w - 14, bh = 8;
                Ui::roundRect(bx, by, bw, bh, 0, 0xFFFFFFFF, 0xFFFFFFFF);
                Ui::roundRect(bx, by, bw * f, bh, 0, BAR_RED, BAR_RED);
            }
        }
    Ui::clipReset();

    /* the point joining the chosen tile to the panel */
    if (selX > areaL() - 4 && selX < areaR() + 4) {
        const float ty = ROW_Y + EP_TILE_H * Ui::HOVER_GROW + 2, by = PANEL_Y + 1, hw = 11;
        Ui::triangle(selX, ty, selX + hw, by, selX - hw, by, p.cardTop);
        Ui::roundRect(selX - hw + 1.5f, PANEL_Y - 0.5f, hw * 2 - 3, 2.5f, 0, p.cardTop, p.cardTop);
    }

    /* the panel's text: S4:E9 - name, episode 69 / 70; rating, year,
     * length; the overview */
    const JellyfinEpisode& e = showEps[epSel];
    const float px = MARGIN + PAD, pw = 640 - 2 * MARGIN - 2 * PAD;
    char right[48];
    snprintf(right, sizeof(right), tr("episode %d / %d"), epSel + 1, n);
    const int rw = Ui::textWidth(right, 15);
    Ui::textRight(px + pw, PANEL_Y + 14, right, 15, p.textDim);
    {
        char se[24];
        /* TRANSLATORS: a season and an episode number, before the episode's name */
        snprintf(se, sizeof(se), tr("S%d:E%d"), e.seasonNumber, e.indexNumber);
        std::string head = std::string(se);
        if (!e.name.empty()) head += " - " + filterDejaVu(e.name, 200);
        head = fitText(font, head, 17, (int)(pw - rw - 16));
        Text::print(px, PANEL_Y + 12, font, head.c_str(), 17, p.text);
        Text::print(px + 1, PANEL_Y + 12, font, head.c_str(), 17, p.text);   /* bolder */
    }
    {
        float x = px;
        const float y = PANEL_Y + 40;
        const std::string& rating = e.officialRating.empty() ? showSeries.officialRating : e.officialRating;
        if (!rating.empty()) {
            const float bw = Ui::textWidth(rating.c_str(), 13) + 12;
            Ui::roundBorder(x, y - 1, bw, 20, 3, 1.5f, p.text);
            Ui::text(x + 6, y + 1, rating.c_str(), 13, p.text);
            x += bw + 16;
        }
        char buf[32];
        if (e.year) {
            snprintf(buf, sizeof(buf), "%d", e.year);
            Ui::text(x, y, buf, 15, p.text);
            x += Ui::textWidth(buf, 15) + 16;
        }
        if (e.runtimeTicks > 0) {
            const int mins = (int)(e.runtimeTicks / 600000000LL);
            Ui::text(x, y, Lang::runTime(mins > 0 ? mins : 1).c_str(), 15, p.text);
        }
    }
    if (epLinesFor != epSel) {   /* wrapped once a choice */
        epLines    = wrapText(e.overview, OV_SIZE, pw, OV_LINES);
        epLinesFor = epSel;
    }
    for (int i = 0; i < (int)epLines.size(); ++i)
        Ui::text(px, PANEL_Y + 68 + i * OV_STEP, epLines[i].c_str(), OV_SIZE,
                 Ui::mix(p.text, p.textDim, 0.15f));
}
