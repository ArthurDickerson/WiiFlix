/* LibraryView: the details page (a film's, an episode's, a show's). */
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

/* ---- The page's layout (drawn and hit-tested alike) --------------------
 * The channel's: the title under the band, a small poster with four
 * buttons beside it in two rows (Resume or Play | Play from beginning,
 * Add to Queue | Select another episode), then a line of stars, rating,
 * year and length with the Subtitle | Audio pill at its end, the overview,
 * Cast / Director / genres. */
namespace {

const float MARGIN = 24;
/* round back button, on the band's lower edge, top right */
const float BACK_R = 30, BACK_CY = 58;   /* the channel's, measured: on the band's edge */
float backCx() { return Ui::screenRight() - 59; }

struct DetailLayout {
    float px, py, pw, ph;          /* poster (pw before the 16:9 widening) */
    float c1x, c2x, cw;            /* the buttons' two columns            */
    float r1y, r2y, bh;            /* and two rows                        */
};

DetailLayout detailLayout(bool wide)
{
    DetailLayout L;
    L.px = MARGIN; L.py = 96;
    L.pw = wide ? 128 : 84;  L.ph = wide ? 72 : 124;
    const float GAP = 14;
    L.c1x = L.px + L.pw * WiiUtils::wsScaleX() + 16;
    L.cw  = (640 - MARGIN - L.c1x - GAP) * 0.5f;
    L.c2x = L.c1x + L.cw + GAP;
    L.r1y = 100; L.r2y = 160; L.bh = 46;
    return L;
}
const float INFO_Y = 236;                 /* stars, rating, year, length */
const float PILL_H = 26, PILL_Y = INFO_Y + 9 - PILL_H * 0.5f;

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

/* The Version | Subtitle | Audio pill: the parts there are (-1 Version,
 * 1 Subtitles, 0 Audio), right-aligned at the line's end */
struct PillPart { int which; float x, w; const char* label; };
std::vector<PillPart> pillParts(const JellyfinItemDetail& d)
{
    std::vector<PillPart> parts;
    if (!d.versions.empty())        parts.push_back({ -1, 0, 0, N_("Version") });
    if (!d.subtitleStreams.empty()) parts.push_back({  1, 0, 0, N_("Subtitle") });
    if (!d.audioStreams.empty())    parts.push_back({  0, 0, 0, N_("Audio") });
    float x = 640 - MARGIN;
    for (int i = (int)parts.size() - 1; i >= 0; --i) {
        parts[i].w = Ui::textWidth(tr(parts[i].label), 15) + 26;
        x -= parts[i].w;
        parts[i].x = x;
    }
    return parts;
}

/* the rectangular buttons' corners */
const float BTN_R = 3;

/* what the D-pad can be on (LibraryView::detailFocus): Resume / Play, Add
 * to Queue, the back button, Read more (an overview cut short), Play from
 * beginning, Select another episode, the pill's part F_PILL + i */
const int F_PLAY = 0, F_QUEUE = 1, F_BACK = 2, F_MORE = 3, F_START = 4, F_EPS = 5, F_PILL = 10;

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
const float CREDITS_Y = 362;              /* Cast, Director, genres */

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
    const std::vector<PillPart> parts = pillParts(detail);
    const int nParts = (int)parts.size();
    const bool hasStart = detail.playbackPositionTicks > 0 && !detail.played;   /* Play from beginning */
    const bool hasEps   = showMode;                                             /* Select another episode */

    /* a track's list open (ui/TrackList): it takes the input */
    if (trackMenu != TRACK_MENU_NONE) {
        switch (trackList.update(ir, Input::isUpPressed(), Input::isDownPressed(), aPressed,
                                 Input::isBackPressed())) {
        case TrackList::Result::Closed:
            SoundFX::play(SoundFX::FX::Back);
            trackMenu = TRACK_MENU_NONE;
            break;
        case TrackList::Result::Picked: {
            SoundFX::play(SoundFX::FX::Start);
            const int k = trackList.chosen();
            if (trackMenu < 0)       selectVersion(k);
            else if (trackMenu == 0) detailAudioSel = k;
            else                     detailSubSel   = k - 1;   /* row 0: Off */
            trackMenu = TRACK_MENU_NONE;
            break;
        }
        default: break;
        }
        return false;
    }

    /* One focus, lit, which A acts on: the pointer moves it onto the button
     * it comes onto (only then, so the D-pad can take it away while the
     * pointer rests there), the D-pad moves it whether the pointer is on
     * the screen or not. */
    int hover = -1;
    if (ir.valid) {
        if (overBack(ir))                                         hover = F_BACK;
        else if (inBox(ir, L.c1x, L.r1y, L.cw, L.bh))             hover = F_PLAY;
        else if (hasStart && inBox(ir, L.c2x, L.r1y, L.cw, L.bh)) hover = F_START;
        else if (inBox(ir, L.c1x, L.r2y, L.cw, L.bh))             hover = F_QUEUE;
        else if (hasEps && inBox(ir, L.c2x, L.r2y, L.cw, L.bh))   hover = F_EPS;
        else if (overviewCut && inBox(ir, moreX, moreY, moreW, moreH)) hover = F_MORE;
        for (int i = 0; i < nParts; ++i)
            if (inBox(ir, parts[i].x, PILL_Y, parts[i].w, PILL_H)) hover = F_PILL + i;
    }
    if (hover >= 0 && hover != detailHover) { detailFocus = hover; detailDpad = false; }
    detailHover = hover;
    /* the buttons there are changed (a track list, a film without a resume
     * point): back to Play */
    if ((detailFocus == F_START && !hasStart) || (detailFocus == F_EPS && !hasEps) ||
        (detailFocus == F_MORE && !overviewCut) || detailFocus >= F_PILL + nParts)
        detailFocus = F_PLAY;
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
    /* A with the pointer off the buttons (on the cover, say) does nothing,
     * unless the D-pad moved the focus last */
    if (aPressed && ir.valid && hover < 0 && !detailDpad) aPressed = false;

    if ((aPressed && target == F_BACK) || Input::isBackPressed()) {
        leaveDetail();
        return false;
    }
    if (aPressed && target == F_QUEUE) {
        /* a show: the series goes in the queue */
        if (showMode) toggleFavorite(currentSeriesId, showSeries.isFavorite);
        else          toggleFavorite(detailItemId, detail.isFavorite);
        return false;
    }
    if (aPressed && target >= F_PILL && target - F_PILL < nParts) {
        openTrackMenu(parts[target - F_PILL].which);
        return false;
    }
    if (aPressed && target == F_EPS) {
        SoundFX::play(SoundFX::FX::Open);
        epSel      = showEpIdx;
        epFirst    = epSel > 2 ? epSel - 2 : 0;
        epScroll   = (float)epFirst;
        epHover    = -1;
        epDpad     = false;
        epLinesFor = -1;
        state = State::EpisodePick;
        return false;
    }
    /* Resume (where it stopped) or Play; Play from beginning */
    if (aPressed && target == F_PLAY) {
        preparePlay(hasStart ? detail.playbackPositionTicks : 0LL);
        return true;
    }
    if (aPressed && target == F_START) {
        preparePlay(0LL);
        return true;
    }
    if (Input::isActionPressed()) {   /* +: watched; that drops the resume point */
        togglePlayed(detailItemId, detail.played);
        if (detail.played) detail.playbackPositionTicks = 0;
        if (showMode && showEpIdx < (int)showEps.size()) {   /* the episode row's */
            showEps[showEpIdx].played = detail.played;
            if (detail.played) showEps[showEpIdx].playbackPositionTicks = 0;
        }
    }

    /* the D-pad walks the buttons: two rows of two beside the poster, the
     * pill under the right-hand ones, Read more under it, the back button
     * above */
    const int was = detailFocus;
    const int pi = detailFocus >= F_PILL ? detailFocus - F_PILL : -1;   /* the pill's part */
    const int pillFirst = nParts ? F_PILL : -1, pillLast = nParts ? F_PILL + nParts - 1 : -1;
    auto set = [&](int f) { if (f >= 0) detailFocus = f; };
    if (Input::isUpPressed()) {
        switch (detailFocus) {
        case F_PLAY: case F_START: set(F_BACK); break;
        case F_QUEUE: set(F_PLAY); break;
        case F_EPS:   set(hasStart ? F_START : F_PLAY); break;
        case F_MORE:  set(nParts ? pillLast : F_QUEUE); break;
        default: if (pi >= 0) set(hasEps ? F_EPS : F_QUEUE); break;
        }
    } else if (Input::isDownPressed()) {
        switch (detailFocus) {
        case F_BACK:  set(hasStart ? F_START : F_PLAY); break;
        case F_PLAY:  set(F_QUEUE); break;
        case F_START: set(hasEps ? F_EPS : (nParts ? pillLast : F_QUEUE)); break;
        case F_QUEUE: case F_EPS:
            if (nParts) set(pillLast); else if (overviewCut) set(F_MORE);
            break;
        default: if (pi >= 0 && overviewCut) set(F_MORE); break;
        }
    } else if (Input::isRightPressed()) {
        switch (detailFocus) {
        case F_PLAY:  if (hasStart) set(F_START); break;
        case F_QUEUE: if (hasEps) set(F_EPS); else set(pillFirst); break;
        default: if (pi >= 0 && pi + 1 < nParts) set(detailFocus + 1); break;
        }
    } else if (Input::isLeftPressed()) {
        switch (detailFocus) {
        case F_START: set(F_PLAY); break;
        case F_EPS:   set(F_QUEUE); break;
        case F_BACK:  set(hasStart ? F_START : F_PLAY); break;
        default: if (pi > 0) set(detailFocus - 1); else if (pi == 0) set(hasEps ? F_EPS : F_QUEUE); break;
        }
    }
    if (detailFocus != was) { SoundFX::play(SoundFX::FX::Move); detailDpad = true; }
    return false;
}

/* A on a part of the pill: its list, the current one chosen */
void LibraryView::openTrackMenu(int which) {
    trackMenu = which;
    std::vector<std::string> rows;
    if (which < 0) {
        for (const auto& v : detail.versions) rows.push_back(safeTitle(v.name.c_str(), 60));
    } else if (which == 0) {
        for (const auto& a : detail.audioStreams) rows.push_back(safeTitle(a.displayTitle.c_str(), 60));
    } else {
        rows.push_back(tr("Off"));
        for (const auto& t : detail.subtitleStreams) rows.push_back(safeTitle(t.displayTitle.c_str(), 60));
    }
    const int current = which < 0 ? detailVersionSel : which == 0 ? detailAudioSel : detailSubSel + 1;
    trackList.open(which < 0 ? tr("Version") : which == 0 ? tr("Audio track") : tr("Subtitles"),
                   rows, current);
    SoundFX::play(SoundFX::FX::Open);
}

// ---------------------------------------------------------------
// drawDetailView(): the old video channels' title page (see the layout
// above).  The D-pad walks the buttons (detailFocus), as the pointer does.
// ---------------------------------------------------------------
void LibraryView::drawDetailView(ir_t& ir) {
    const Ui::Palette& p = Ui::pal();
    const DetailLayout L = detailLayout(detailWideThumb());
    const int pageW = 640 - 2 * (int)MARGIN;
    const bool hasStart = detail.playbackPositionTicks > 0 && !detail.played;
    const bool favorite = showMode ? showSeries.isFavorite : detail.isFavorite;

    Ui::backButton(backCx(), BACK_CY, BACK_R, detailFocus == F_BACK);

    // ---- Title: the film's, the episode's, or on a show page the series' ----
    {
        /* Japanese in it: drawn with the Japanese font (Text's fallback) */
        const std::string title = filterDejaVu(showMode ? showSeries.name : detail.name, 200);
        const int w = (int)(backCx() - BACK_R - 12 - MARGIN) - (favorite ? 28 : 0);
        titleText(2, MARGIN, 66, w, title, 20, p.text, true);
        if (favorite) {   /* in the queue (a favourite): a heart after the title */
            int tw = (int)Text::width(font, title.c_str(), 20);
            Text::print(MARGIN + (tw < w ? tw : w) + 8, 66, font, "\xe2\x99\xa5", 20, p.accent);
        }
    }

    // ---- Poster and the four buttons ----
    /* orange: the focus only (the pointer's or the D-pad's, one at a time) */
    auto lit = [&](int f) { return detailFocus == f ? 1.0f : 0.0f; };
    if (showMode)
        drawThumb(showTex, L.px, L.py, L.pw, L.ph, 0.0f, -1.0f, showSeries.name.c_str(), false);
    else
        drawThumb(detailTex, L.px, L.py, L.pw, L.ph, 0.0f, progressOf(detail),
                  detail.name.c_str(), false);
    char play[64];
    if (showMode)
        /* TRANSLATORS: the show page's first button; the episode's number in the whole series */
        snprintf(play, sizeof(play), hasStart ? tr("Resume episode %d") : tr("Play episode %d"), showEpIdx + 1);
    else
        snprintf(play, sizeof(play), "%s", hasStart ? tr("Resume") : tr("Play"));
    Ui::button(L.c1x, L.r1y, L.cw, L.bh, play, 17, lit(F_PLAY), BTN_R);
    if (hasStart)
        Ui::button(L.c2x, L.r1y, L.cw, L.bh, N_("Play from beginning"), 17, lit(F_START), BTN_R);
    Ui::button(L.c1x, L.r2y, L.cw, L.bh, favorite ? N_("Remove from Queue") : N_("Add to Queue"), 17,
               lit(F_QUEUE), BTN_R);
    if (showMode)
        Ui::button(L.c2x, L.r2y, L.cw, L.bh, N_("Select another episode"), 17, lit(F_EPS), BTN_R);

    // ---- Stars, rating, year, length (watched); the pill at the end ----
    float y = INFO_Y;
    {
        float x = MARGIN;
        const float cy = y + 9;
        const float stars = detail.communityRating > 0.0f ? detail.communityRating
                          : showMode ? showSeries.communityRating : 0.0f;
        const std::string& rating = !detail.officialRating.empty() || !showMode
                                  ? detail.officialRating : showSeries.officialRating;
        if (stars > 0.0f)
            x += Ui::stars(x, cy, 9, stars, p.accent, Ui::alpha(p.textDim, 0.3f)) + 16;
        if (!rating.empty()) {
            const float bw = Ui::textWidth(rating.c_str(), 13) + 12;
            Ui::roundBorder(x, cy - 10, bw, 20, 3, 1.5f, p.text);
            Ui::text(x + 6, cy - 8, rating.c_str(), 13, p.text);
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
        }
    }
    /* the pill: white, the part chosen (or whose list is open) orange */
    const std::vector<PillPart> parts = pillParts(detail);
    if (!parts.empty()) {
        const float x0 = parts.front().x, pw = parts.back().x + parts.back().w - x0, r = PILL_H * 0.5f;
        Ui::shadow(x0, PILL_Y + 2, pw, PILL_H, r, 4.0f, 0x00000050);
        Ui::roundRect(x0, PILL_Y, pw, PILL_H, r, 0xFFFFFFFF, 0xD4D4D4FF);
        Ui::roundBorder(x0, PILL_Y, pw, PILL_H, r, 1.0f, 0x8E8E8EFF);
        for (int i = 0; i < (int)parts.size(); ++i) {
            const PillPart& pp = parts[i];
            const bool on = detailFocus == F_PILL + i || trackMenu == pp.which;
            if (on) {   /* the whole pill in orange, only this part of it shown */
                Ui::clip(pp.x, PILL_Y - 2, pp.w, PILL_H + 4);
                Ui::roundRect(x0, PILL_Y, pw, PILL_H, r, Ui::HOVER_TOP, Ui::HOVER_BOTTOM);
                Ui::roundBorder(x0, PILL_Y, pw, PILL_H, r, 1.5f, Ui::HOVER_EDGE);
                Ui::clipReset();
            }
            if (i > 0)
                Ui::roundRect(pp.x - 0.75f, PILL_Y + 5, 1.5f, PILL_H - 10, 0, 0x8E8E8EFF, 0x8E8E8EFF);
            Ui::textCentered(pp.x + pp.w * 0.5f, PILL_Y + (PILL_H - 15) * 0.5f - 1, tr(pp.label), 15,
                             on ? 0xFFFFFFFF : 0x3A3A3AFF);
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

    // ---- Cast, Director, genres (a show's: the series') ----
    {
        std::string cast, director;
        for (const auto& pp : detail.people) {
            if (pp.name.empty()) continue;
            if (pp.role != "Director" && pp.role != "Actor" && !pp.role.empty()) continue;
            std::string& to = pp.role == "Director" ? director : cast;
            if (!to.empty()) to += ", ";
            to += pp.name;
        }
        const std::vector<std::string>& gl = showMode && !showSeries.genres.empty() ? showSeries.genres : detail.genres;
        std::string genres;
        for (size_t i = 0; i < gl.size(); i++) {
            if (i) genres += ", ";
            genres += gl[i];
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
    if (trackMenu != TRACK_MENU_NONE) trackList.draw();
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
