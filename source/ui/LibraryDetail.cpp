/* LibraryView: Item detail page and the resume prompt. */
#include "LibraryView.h"
#include "LibraryDraw.h"
#include "Ui.h"
#include "../input/Input.h"
#include "../core/SoundFX.h"
#include "../core/Text.h"
#include "../core/Lang.h"
#include "../core/Utils.h"
#include <stdio.h>
#include <math.h>

using namespace LibDraw;

/* ---- The title page's layout (drawn and hit-tested alike) ---------- */
namespace {

const float MARGIN = 24;
/* round back button, on the band's lower edge, top right */
const float BACK_R = 20, BACK_CY = 44;
float backCx() { return Ui::screenRight() - 34; }

struct DetailLayout {
    float px, py, pw, ph;          /* poster (pw before the 16:9 widening) */
    float bx, bw;                  /* the two buttons' column              */
    float playY, playH, favY, favH;
    float rowsX, rowsW;            /* Version / Audio / Subtitles / Scenes */
};

DetailLayout detailLayout(bool wide)
{
    DetailLayout L;
    L.px = MARGIN; L.py = 84;
    L.pw = wide ? 128 : 84;  L.ph = wide ? 72 : 124;
    L.bx = L.px + L.pw * WiiUtils::wsScaleX() + 16;
    L.bw = 344 - L.bx;
    L.playY = 88;  L.playH = 40;
    L.favY  = 138; L.favH  = 32;
    L.rowsX = 362; L.rowsW = 640 - MARGIN - L.rowsX;
    return L;
}

bool inBox(const ir_t& ir, float x, float y, float w, float h)
{
    return ir.valid && ir.x >= x && ir.x < x + w && ir.y >= y && ir.y < y + h;
}

bool overBack(const ir_t& ir)
{
    if (!ir.valid) return false;
    const float dx = ir.x - backCx(), dy = ir.y - BACK_CY;
    return dx * dx + dy * dy < (BACK_R + 4) * (BACK_R + 4);
}

/* the track rows there are, top down: -1 Version, 0 Audio, 1 Subtitles;
 * row i's box */
std::vector<int> trackRows(const JellyfinItemDetail& d)
{
    std::vector<int> rows;
    if (!d.versions.empty())        rows.push_back(-1);
    if (!d.audioStreams.empty())    rows.push_back(0);
    if (!d.subtitleStreams.empty()) rows.push_back(1);
    return rows;
}
const float ROW_STEP = 26, ROW_H = 22;
void rowBox(const DetailLayout& L, int i, float& x, float& y, float& w, float& h)
{
    x = L.rowsX - 6; y = L.playY + i * ROW_STEP; w = L.rowsW + 6; h = ROW_H;
}
/* the rectangular buttons' corners */
const float BTN_R = 3;

/* what the D-pad can be on (LibraryView::detailFocus): Play, Add to Queue,
 * the back button, Read more (an overview cut short), track row F_ROW + i */
const int F_PLAY = 0, F_QUEUE = 1, F_BACK = 2, F_MORE = 3, F_ROW = 10;

/* a track's list: rows of MENU_ROW_H, TRACK_MENU_ROWS at most shown */
const int   TRACK_MENU_ROWS = 7;
const float MENU_STEP = 28, MENU_ROW_H = 24;
/* it hangs under its button, kept above the bottom of the screen */
void menuBox(const DetailLayout& L, const std::vector<int>& rows, int which, int n,
             float& x, float& y, float& w, float& h)
{
    int at = 0;
    for (int i = 0; i < (int)rows.size(); ++i) if (rows[i] == which) at = i;
    float bx, by, bw, bh;
    rowBox(L, at, bx, by, bw, bh);
    const int shown = n < TRACK_MENU_ROWS ? n : TRACK_MENU_ROWS;
    x = bx; w = bw;
    h = 12 + shown * MENU_STEP - (MENU_STEP - MENU_ROW_H);
    y = by + bh + 4;
    if (y + h > 460) y = 460 - h;
}

/* Validate UTF-8 and truncate at a safe codepoint boundary so the
 * renderer never receives a broken multi-byte sequence. */
std::string safeTitle(const char* s, int maxCodepoints)
{
    if (!s) return "-";
    std::string out;
    const unsigned char* p = (const unsigned char*)s;
    int count = 0;
    while (*p && count < maxCodepoints) {
        int seqLen;
        if      (*p < 0x80) seqLen = 1;
        else if (*p < 0xE0) seqLen = 2;
        else if (*p < 0xF0) seqLen = 3;
        else                seqLen = 4;
        bool valid = true;
        for (int i = 1; i < seqLen; i++) {
            if ((p[i] & 0xC0) != 0x80) { valid = false; break; }
        }
        if (!valid) { p++; continue; }
        for (int i = 0; i < seqLen; i++) out += (char)p[i];
        p += seqLen;
        ++count;
    }
    if (*p) out += "...";
    if (out.empty()) out = "?";
    return out;
}

const float OV_SIZE = 14, OV_STEP = 18;   /* the overview's lines */
const float CREDITS_Y = 354;              /* Cast, Director, genres */

}  // namespace

bool LibraryView::updateDetail(ir_t& ir, bool aPressed) {
    /* the whole overview over the page: Up / Down a line, B or - closes */
    if (overviewPanel) {
        const int maxTop = (int)overviewAll.size() > OV_PANEL_ROWS ? (int)overviewAll.size() - OV_PANEL_ROWS : 0;
        /* B, -, A (on its ✕ or anywhere) close it */
        if (Input::isBackPressed() || Input::isLPressed() || aPressed) overviewPanel = false;
        else if (Input::isUpPressed()   && overviewTop > 0)      overviewTop--;
        else if (Input::isDownPressed() && overviewTop < maxTop) overviewTop++;
        else if (Input::isLeftPressed())  overviewTop = overviewTop > OV_PANEL_ROWS ? overviewTop - OV_PANEL_ROWS : 0;
        else if (Input::isRightPressed()) overviewTop = overviewTop + OV_PANEL_ROWS < maxTop ? overviewTop + OV_PANEL_ROWS : maxTop;
        return false;
    }
    const DetailLayout L = detailLayout(detailWideThumb());
    const std::vector<int> rows = trackRows(detail);

    /* a track's list open: Up / Down (or the pointer) pick, A takes it, B
     * (or A off the list) closes it */
    if (trackMenu != TRACK_MENU_NONE) {
        const int n = trackMenuCount();
        float mx, my, mw, mh;
        menuBox(L, rows, trackMenu, n, mx, my, mw, mh);
        int hover = -1;
        for (int i = 0; i < TRACK_MENU_ROWS && trackMenuTop + i < n; ++i)
            if (inBox(ir, mx + 6, my + 6 + i * MENU_STEP, mw - 12, MENU_ROW_H)) hover = trackMenuTop + i;
        /* one choice: the pointer takes it when it moves onto another row,
         * the D-pad moves it too, even with the pointer on the screen */
        if (hover >= 0 && hover != trackMenuHover) { trackMenuSel = hover; trackMenuDpad = false; }
        trackMenuHover = hover;
        if (Input::isUpPressed()   && trackMenuSel > 0)     { --trackMenuSel; trackMenuDpad = true; }
        if (Input::isDownPressed() && trackMenuSel + 1 < n) { ++trackMenuSel; trackMenuDpad = true; }
        if (trackMenuSel < trackMenuTop) trackMenuTop = trackMenuSel;
        if (trackMenuSel >= trackMenuTop + TRACK_MENU_ROWS) trackMenuTop = trackMenuSel - TRACK_MENU_ROWS + 1;
        /* A off the list closes it, unless the D-pad chose last */
        if (Input::isBackPressed() || (aPressed && ir.valid && hover < 0 && !trackMenuDpad)) {
            SoundFX::play(SoundFX::FX::Back);
            trackMenu = TRACK_MENU_NONE;
        } else if (aPressed) {
            SoundFX::play(SoundFX::FX::Start);
            if (trackMenu < 0)       selectVersion(trackMenuSel);
            else if (trackMenu == 0) detailAudioSel = trackMenuSel;
            else                     detailSubSel   = trackMenuSel - 1;   /* row 0: Off */
            trackMenu = TRACK_MENU_NONE;
        }
        return false;
    }

    /* One focus, lit, which A acts on: the pointer moves it onto the button
     * it comes onto (only then, so the D-pad can take it away while the
     * pointer rests there), the D-pad moves it whether the pointer is on
     * the screen or not. */
    int hover = -1;
    if (ir.valid) {
        if (overBack(ir))                                 hover = F_BACK;
        else if (inBox(ir, L.bx, L.playY, L.bw, L.playH)) hover = F_PLAY;
        else if (overviewCut && inBox(ir, moreX, moreY, moreW, moreH)) hover = F_MORE;
        else if (inBox(ir, L.bx, L.favY, L.bw, L.favH))   hover = F_QUEUE;
        for (int i = 0; i < (int)rows.size(); ++i) {
            float x, y, w, h;
            rowBox(L, i, x, y, w, h);
            if (inBox(ir, x, y, w, h)) hover = F_ROW + i;
        }
    }
    if (hover >= 0 && hover != detailHover) detailFocus = hover;
    detailHover = hover;
    const int target = detailFocus;

    /* -, or A with the pointer on it: the overview the page cuts short */
    const bool onOverview = ir.valid && ir.x >= MARGIN && ir.x < 640 - MARGIN &&
                            ir.y >= detailOvY0 && ir.y < detailOvY1;
    if (overviewCut && (Input::isLPressed() || (aPressed && (onOverview || target == F_MORE)))) {
        SYS_Report("[Detail] the whole overview: %d lines\n", (int)overviewAll.size());
        overviewPanel = true;
        overviewTop = 0;
        return false;
    }

    if (aPressed && target == F_BACK) {
        freeDetail();
        state = detailReturnState;
        return false;
    }
    if (aPressed && target == F_QUEUE) {
        toggleFavorite(detailItemId, detail.isFavorite);
        return false;
    }
    if (aPressed && target >= F_ROW && target - F_ROW < (int)rows.size()) {
        openTrackMenu(rows[target - F_ROW]);
        return false;
    }
    // A: play
    if (aPressed && target == F_PLAY) {
        if (detail.playbackPositionTicks > 0) {
            // There is a saved position — ask the user Continue / Start Over
            resumeSel = 0;
            resumeHover = -1;
            state = State::ResumePrompt;
            return false;
        }
        // No saved position: play from the beginning immediately
        preparePlay(0LL);
        return true;
    }
    if (Input::is1Pressed()) toggleFavorite(detailItemId, detail.isFavorite);
    if (Input::isActionPressed()) {   /* +: watched; that drops the resume point */
        togglePlayed(detailItemId, detail.played);
        if (detail.played) detail.playbackPositionTicks = 0;
        for (auto& e : episodes)   /* the episode list shown on B */
            if (e.id == detailItemId) {
                e.played = detail.played;
                if (e.played) e.playbackPositionTicks = 0;
            }
    }
    if (Input::is2Pressed() && detail.specialFeatures > 0) {
        specialsFilmReturn = detailReturnState;
        openSpecials(detailItemId, detail.name, State::DetailReady);
        return false;
    }
    if (Input::isBackPressed()) {
        freeDetail();
        state = detailReturnState;
        return false;
    }

    /* the D-pad walks the buttons: Play and Add to Queue on the left, the
     * tracks on the right, the back button above them */
    const int nRows = (int)rows.size();
    const int was = detailFocus;
    if (detailFocus >= F_ROW + nRows) detailFocus = F_PLAY;   /* the tracks changed */
    const bool row = detailFocus >= F_ROW;
    const int  r   = detailFocus - F_ROW;
    if (detailFocus == F_MORE && !overviewCut) detailFocus = F_QUEUE;
    if (Input::isUpPressed()) {
        if (detailFocus == F_MORE)       detailFocus = F_QUEUE;
        else if (detailFocus == F_QUEUE) detailFocus = F_PLAY;
        else if (detailFocus == F_PLAY)  detailFocus = F_BACK;
        else if (row)                    detailFocus = r > 0 ? detailFocus - 1 : F_BACK;
    } else if (Input::isDownPressed()) {
        if (detailFocus == F_BACK)       detailFocus = nRows ? F_ROW : F_PLAY;
        else if (detailFocus == F_PLAY)  detailFocus = F_QUEUE;
        else if (detailFocus == F_QUEUE && overviewCut) detailFocus = F_MORE;
        else if (row && r + 1 < nRows)   detailFocus = detailFocus + 1;
        else if (row && overviewCut)     detailFocus = F_MORE;
    } else if (Input::isRightPressed()) {
        if (detailFocus == F_PLAY && nRows)  detailFocus = F_ROW;
        if (detailFocus == F_QUEUE && nRows) detailFocus = F_ROW + (nRows > 1 ? 1 : 0);
    } else if (Input::isLeftPressed()) {
        if (row)                         detailFocus = r == 0 ? F_PLAY : F_QUEUE;
        else if (detailFocus == F_BACK)  detailFocus = F_PLAY;
    }
    if (detailFocus != was) SoundFX::play(SoundFX::FX::Move);
    return false;
}

/* A on a track's button: its list, the current one chosen */
void LibraryView::openTrackMenu(int which) {
    trackMenu = which;
    trackMenuSel = which < 0 ? detailVersionSel : which == 0 ? detailAudioSel : detailSubSel + 1;
    if (trackMenuSel < 0) trackMenuSel = 0;
    trackMenuTop = trackMenuSel >= TRACK_MENU_ROWS ? trackMenuSel - TRACK_MENU_ROWS + 1 : 0;
    trackMenuHover = -1;
    trackMenuDpad  = false;
    SoundFX::play(SoundFX::FX::Open);
}

int LibraryView::trackMenuCount() const {
    if (trackMenu < 0)  return (int)detail.versions.size();
    if (trackMenu == 0) return (int)detail.audioStreams.size();
    return (int)detail.subtitleStreams.size() + 1;   /* Off first */
}

std::string LibraryView::trackMenuLabel(int i) const {
    if (trackMenu < 0)  return detail.versions[i].name;
    if (trackMenu == 0) return detail.audioStreams[i].displayTitle;
    return i == 0 ? std::string(tr("Off")) : detail.subtitleStreams[i - 1].displayTitle;
}

/* The resume question's layout (drawn and hit-tested alike) */
namespace {
const int RP_W = 340, RP_H = 120;
const int RP_X = (640 - RP_W) / 2, RP_Y = (480 - RP_H) / 2;
const int RP_BW = 130, RP_BH = 32, RP_GAP = 16;
const int RP_BX = RP_X + (RP_W - (RP_BW * 2 + RP_GAP)) / 2;
const int RP_BY = RP_Y + RP_H - RP_BH - 18;
int rpButtonX(int i) { return RP_BX + i * (RP_BW + RP_GAP); }
}  // namespace

/* Continue / From Start: one choice, lit, which A takes.  The pointer
 * moves it onto the button it comes onto, the D-pad (Left / Right) moves
 * it whether the pointer is on the screen or not; B goes back to the
 * page. */
bool LibraryView::updateResumePrompt(ir_t& ir, bool aPressed) {
    int hover = -1;
    if (ir.valid)
        for (int i = 0; i < 2; ++i)
            if (ir.x >= rpButtonX(i) && ir.x < rpButtonX(i) + RP_BW && ir.y >= RP_BY && ir.y < RP_BY + RP_BH)
                hover = i;
    if (hover >= 0 && hover != resumeHover) resumeSel = hover;
    resumeHover = hover;
    const int was = resumeSel;
    if (Input::isLeftPressed())  resumeSel = 0;
    if (Input::isRightPressed()) resumeSel = 1;
    if (resumeSel != was) SoundFX::play(SoundFX::FX::Move);
    if (Input::isBackPressed()) {
        SoundFX::play(SoundFX::FX::Back);
        state = State::DetailReady;
        return false;
    }
    if (aPressed) {
        preparePlay(resumeSel == 0 ? detail.playbackPositionTicks : 0LL);
        return true;
    }
    return false;
}

// Resume prompt overlay
void LibraryView::renderResumePrompt() {
    const Ui::Palette& p = Ui::pal();
    GRRLIB_Rectangle(Ui::screenLeft(), 0, Ui::screenWidth(), 480, p.dim, 1);
    Ui::card(RP_X, RP_Y, RP_W, RP_H, 18, 0.0f);

    Ui::textCentered(320, RP_Y + 16, tr("Resume playback?"), 17, p.text);
    {
        int secs = (int)(detail.playbackPositionTicks / 10000000LL);
        int rh = secs / 3600, rm = (secs % 3600) / 60, rs = secs % 60;
        char hint[48];
        /* TRANSLATORS: under "Resume playback?": where it stopped, h:mm:ss or m:ss */
        if (rh > 0) snprintf(hint, sizeof(hint), tr("at %d:%02d:%02d"), rh, rm, rs);
        else        snprintf(hint, sizeof(hint), tr("at %d:%02d"), rm, rs);
        Ui::textCentered(320, RP_Y + 40, hint, 13, p.textDim);
    }

    // Two rectangular buttons: Continue | From Start, the choice orange
    const char* btnLabels[2] = { N_("Continue"), N_("From Start") };
    for (int i = 0; i < 2; ++i)
        Ui::button(rpButtonX(i), RP_BY, RP_BW, RP_BH, btnLabels[i], 15,
                   resumeSel == i ? 1.0f : 0.0f, BTN_R);
}

// ---------------------------------------------------------------
// drawDetailView(): the old video channels' title page.  Under the red
// band: the title, a small poster with Play and Add to Queue beside it
// (the tracks further right, each opening a list), then a line of stars,
// rating, year and running time, the overview, and Cast / Director /
// genres.  The D-pad walks the buttons (detailFocus), as the pointer does.
// ---------------------------------------------------------------
void LibraryView::drawDetailView(ir_t& ir) {
    const Ui::Palette& p = Ui::pal();
    const DetailLayout L = detailLayout(detailWideThumb());
    const int pageW = 640 - 2 * (int)MARGIN;

    Ui::backButton(backCx(), BACK_CY, BACK_R, detailFocus == F_BACK);

    // ---- Title ----
    {
        /* Japanese in it: drawn with the Japanese font (Text's fallback) */
        const std::string title = filterDejaVu(detail.name, 200);
        const int w = (int)(backCx() - BACK_R - 12 - MARGIN) - (detail.isFavorite ? 28 : 0);
        titleText(2, MARGIN, 52, w, title, 20, p.text, true);
        if (detail.isFavorite) {   /* in the queue (a favourite): a heart after the title (1) */
            int tw = (int)Text::width(font, title.c_str(), 20);
            Text::print(MARGIN + (tw < w ? tw : w) + 8, 52, font, "\xe2\x99\xa5", 20, p.accent);
        }
    }

    // ---- Poster, Play, Add to Queue ----
    /* orange: the focus only (the pointer's or the D-pad's, one at a time) */
    auto lit = [&](int f) { return detailFocus == f ? 1.0f : 0.0f; };
    drawThumb(detailTex, L.px, L.py, L.pw, L.ph, 0.0f, progressOf(detail),
              detail.name.c_str(), false);
    Ui::button(L.bx, L.playY, L.bw, L.playH, N_("Play"), 19,
               lit(F_PLAY), BTN_R);
    Ui::button(L.bx, L.favY, L.bw, L.favH,
               detail.isFavorite ? N_("Remove from Queue") : N_("Add to Queue"), 14,
               lit(F_QUEUE), BTN_R);

    // ---- Version / Audio / Subtitles, right of the buttons: A opens a list ----
    const std::vector<int> rows = trackRows(detail);
    const int valX = (int)L.rowsX + 78, valW = (int)(L.rowsX + L.rowsW) - valX - 4;
    for (int i = 0; i < (int)rows.size(); ++i) {
        const int row = rows[i];
        float bx, by, bw, bh;
        rowBox(L, i, bx, by, bw, bh);
        const bool focused = trackMenu == row || detailFocus == F_ROW + i;
        Ui::button(bx, by, bw, bh, "", 13, focused ? 1.0f : 0.0f, BTN_R);
        const int ry = (int)(by + (bh - 13) * 0.5f) - 1;
        const u32 labelCol = focused ? p.textOnAccent : p.textDim;
        const u32 valueCol = focused ? p.textOnAccent : p.text;
        const char* label = row < 0 ? N_("Version") : row == 0 ? N_("Audio") : N_("Subtitles");
        const char* rawTitle;
        if (row < 0)
            rawTitle = detail.versions[detailVersionSel].name.c_str();
        else if (row == 0)
            rawTitle = detailAudioSel < (int)detail.audioStreams.size()
                ? detail.audioStreams[detailAudioSel].displayTitle.c_str() : "-";
        else
            rawTitle = (detailSubSel == -1)
                ? tr("Off")
                : (detailSubSel < (int)detail.subtitleStreams.size()
                    ? detail.subtitleStreams[detailSubSel].displayTitle.c_str() : "-");
        Ui::text(L.rowsX + 4, ry, tr(label), 13, labelCol);
        std::string t = fitText(font, safeTitle(rawTitle, 36), 13, valW - 14);
        Text::print(valX, ry, font, t.c_str(), 13, valueCol);
        /* a small down arrow: it opens a list */
        const float ax = bx + bw - 12, ay = by + bh * 0.5f;
        Ui::triangle(ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3, valueCol);
    }
    const float rowsBottom = rows.empty() ? L.playY : L.playY + rows.size() * ROW_STEP;

    // ---- Stars, rating, year, running time (and watched, resume) ----
    float y = L.py + L.ph;
    if (L.favY + L.favH > y) y = L.favY + L.favH;
    if (rowsBottom > y)      y = rowsBottom;
    y += 12;
    {
        float x = MARGIN;
        const float cy = y + 9;
        if (detail.communityRating > 0.0f)
            x += Ui::stars(x, cy, 9, detail.communityRating, p.accent, Ui::alpha(p.textDim, 0.3f)) + 16;
        if (!detail.officialRating.empty()) {
            const float bw = Ui::textWidth(detail.officialRating.c_str(), 13) + 12;
            Ui::roundBorder(x, cy - 10, bw, 20, 3, 1.5f, p.text);
            Ui::text(x + 6, cy - 8, detail.officialRating.c_str(), 13, p.text);
            x += bw + 16;
        }
        char buf[48];
        if (detail.year) {
            snprintf(buf, sizeof(buf), "%d", detail.year);
            Ui::text(x, y, buf, 15, p.text);
            x += Ui::textWidth(buf, 15) + 16;
        }
        if (detail.runtimeTicks > 0) {
            int secs = (int)(detail.runtimeTicks / 10000000LL);
            /* TRANSLATORS: a running time in seconds (a trailer, a clip) */
            if (secs >= 60) snprintf(buf, sizeof(buf), "%s", Lang::runTime(secs / 60).c_str());
            else            snprintf(buf, sizeof(buf), tr("%ds"), secs);
            Ui::text(x, y, buf, 15, p.text);
            x += Ui::textWidth(buf, 15) + 16;
        }
        if (detail.played) {
            snprintf(buf, sizeof(buf), "\xe2\x9c\x93 %s", tr("Watched"));
            Text::print(x, y + 1, font, buf, 14, p.ok);
            x += Text::width(font, buf, 14) + 16;
        }
        if (detail.playbackPositionTicks > 0 && detail.runtimeTicks > 0) {
            int secs = (int)(detail.playbackPositionTicks / 10000000LL);
            int h    = secs / 3600, m = (secs % 3600) / 60;
            int pct  = (int)(detail.playbackPositionTicks * 100LL / detail.runtimeTicks);
            /* TRANSLATORS: hours and minutes into the video, then how much of it is watched */
            if (h > 0) snprintf(buf, sizeof(buf), tr("Resume at %dh%02d (%d%%)"), h, m, pct);
            else       snprintf(buf, sizeof(buf), tr("Resume at %dmin (%d%%)"), m, pct);
            Ui::text(x, y + 1, buf, 14, p.accentDark);
        }
    }
    y += 28;

    // ---- Overview (pre-computed lines, no per-frame width measuring) ----
    if (!detailLines.empty()) {
        detailOvY0 = y;
        for (const auto& line : detailLines) {
            Ui::text(MARGIN, y, line.c_str(), (int)OV_SIZE, p.text);
            y += OV_STEP;
        }
        detailOvY1 = y;
        if (overviewCut) {   /* the rest of it: Read more (or A on the text, or -) */
            moreW = (float)Ui::textWidth(tr("Read more"), 14) + 28; moreH = 22;
            moreX = MARGIN + pageW - moreW; moreY = y + 1;
            Ui::button(moreX, moreY, moreW, moreH, N_("Read more"), 14,
                       lit(F_MORE), BTN_R);
        } else {
            moreY = -100;
        }
    }

    // ---- Cast, Director, genres ----
    {
        std::string cast, director;
        for (const auto& pp : detail.people) {
            if (pp.name.empty()) continue;
            if (pp.role != "Director" && pp.role != "Actor" && !pp.role.empty()) continue;
            std::string& to = pp.role == "Director" ? director : cast;
            if (!to.empty()) to += ", ";
            to += pp.name;
        }
        std::string genres;
        for (size_t i = 0; i < detail.genres.size(); i++) {
            if (i) genres += ", ";
            genres += detail.genres[i];
        }
        float cy = CREDITS_Y;
        auto line = [&](const char* label, const std::string& v) {
            if (v.empty()) return;
            float x = MARGIN;
            if (label) {
                std::string l = std::string(tr(label)) + " ";
                Ui::text(x, cy, l.c_str(), 14, p.textDim);
                x += Ui::textWidth(l.c_str(), 14);
            }
            std::string s = fitText(font, v, 14, (int)(MARGIN + pageW - x));
            Text::print(x, cy, font, s.c_str(), 14, p.text);
            Text::print(x + 1, cy, font, s.c_str(), 14, p.text);   /* bolder */
            cy += 18;
        };
        /* TRANSLATORS: before the actors' names on a title's page */
        line(N_("Cast:"), cast);
        /* TRANSLATORS: before the director's name on a title's page */
        line(N_("Director:"), director);
        line(nullptr, genres);
    }

    // ---- a track's list, over the page ----
    if (trackMenu != TRACK_MENU_NONE) {
        const int n = trackMenuCount();
        float mx, my, mw, mh;
        menuBox(L, rows, trackMenu, n, mx, my, mw, mh);
        Ui::shadow(mx, my + 3, mw, mh, 6, 8.0f, 0x00000060);
        Ui::roundRect(mx, my, mw, mh, 4, p.cardTop, p.cardBottom);
        Ui::roundBorder(mx, my, mw, mh, 4, 1.5f, p.cardBorder);
        const int current = trackMenu < 0 ? detailVersionSel : trackMenu == 0 ? detailAudioSel : detailSubSel + 1;
        const float sbw = n > TRACK_MENU_ROWS ? 10 : 0;   /* room for the scrollbar */
        for (int i = 0; i < TRACK_MENU_ROWS && trackMenuTop + i < n; ++i) {
            const int k = trackMenuTop + i;
            const float ix = mx + 6, iy = my + 6 + i * MENU_STEP, iw = mw - 12 - sbw;
            const bool on = k == trackMenuSel;
            Ui::button(ix, iy, iw, MENU_ROW_H, "", 13, on ? 1.0f : 0.0f, BTN_R);
            const u32 c = on ? p.textOnAccent : p.text;
            std::string t = fitText(font, safeTitle(trackMenuLabel(k).c_str(), 40), 13, (int)iw - 34);
            Text::print(ix + 10, iy + 4, font, t.c_str(), 13, c);
            if (k == current)   /* the one in use */
                Text::print(ix + iw - 20, iy + 4, font, "\xe2\x9c\x93", 13, on ? c : p.ok);
        }
        if (n > TRACK_MENU_ROWS)
            Ui::scrollbar(mx + mw - 10, my + 6, mh - 12, trackMenuTop, TRACK_MENU_ROWS, n);
    }
    if (overviewPanel) renderOverviewPanel(ir);
}

/* The whole overview in a panel over the page */
void LibraryView::renderOverviewPanel(const ir_t& ir) {
    const Ui::Palette& p = Ui::pal();
    GRRLIB_Rectangle(Ui::screenLeft(), 0, Ui::screenWidth(), 480, p.dim, 1);
    /* the title on up to three lines (the whole name of a long episode),
     * clear of the ✕ */
    const std::vector<std::string> title = wrapText(filterDejaVu(detail.name, 200), 18, OV_PANEL_W - 36, 3);
    const int TH = 22 * (title.empty() ? 1 : (int)title.size());
    const int PW = OV_PANEL_W + 48, PH = 42 + TH + OV_PANEL_ROWS * 20 + 12;
    const int PX = (640 - PW) / 2, PY = (450 - PH) / 2;
    Ui::card(PX, PY, PW, PH, 18, 0.0f);
    for (size_t i = 0; i < title.size(); i++)
        Ui::text(PX + 24, PY + 16 + (int)i * 22, title[i].c_str(), 18, p.text);
    const int LY = PY + 36 + TH;   /* the text, under the title's line */
    Ui::roundRect(PX + 24, LY - 12, OV_PANEL_W, 1.5f, 0.75f, Ui::alpha(p.cardBorder, 0.6f));
    const int n = (int)overviewAll.size();
    for (int i = 0; i < OV_PANEL_ROWS && overviewTop + i < n; i++)
        Ui::text(PX + 24, LY + i * 20, overviewAll[overviewTop + i].c_str(), OV_PANEL_SIZE,
                 Ui::mix(p.text, p.textDim, 0.15f));
    if (n > OV_PANEL_ROWS)
        Ui::scrollbar(PX + PW - 14, LY, OV_PANEL_ROWS * 20 - 4, overviewTop, OV_PANEL_ROWS, n);
    /* ✕ at the top right: orange, a little bigger under the pointer */
    ovCloseX = PX + PW - 26; ovCloseY = PY + 24;
    const bool hot = ir.valid && (ir.x - ovCloseX) * (ir.x - ovCloseX) + (ir.y - ovCloseY) * (ir.y - ovCloseY) < 20 * 20;
    const float a = 8 * (hot ? Ui::HOVER_GROW : 1.0f), t = 3.2f * (hot ? Ui::HOVER_GROW : 1.0f);
    const u32 c = hot ? Ui::HOVER_TOP : p.text;
    auto stroke = [&](float x0, float y0, float x1, float y1) {
        const float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
        const float nx = -dy / l * t * 0.5f, ny = dx / l * t * 0.5f;
        Ui::triangle(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
        Ui::triangle(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
    };
    stroke(ovCloseX - a, ovCloseY - a, ovCloseX + a, ovCloseY + a);
    stroke(ovCloseX + a, ovCloseY - a, ovCloseX - a, ovCloseY + a);
}
