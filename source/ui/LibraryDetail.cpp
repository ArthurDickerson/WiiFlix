/* LibraryView: Item detail page and the resume prompt. */
#include "LibraryView.h"
#include "LibraryDraw.h"
#include "Ui.h"
#include "../input/Input.h"
#include "../core/Text.h"
#include "../core/Lang.h"
#include "../core/Utils.h"
#include <stdio.h>

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

/* the track rows there are, top down: -1 Version, 0 Audio, 1 Subtitles,
 * 2 Scenes; row i's box */
std::vector<int> trackRows(const JellyfinItemDetail& d)
{
    std::vector<int> rows;
    if (!d.versions.empty())        rows.push_back(-1);
    if (!d.audioStreams.empty())    rows.push_back(0);
    if (!d.subtitleStreams.empty()) rows.push_back(1);
    if (!d.chapters.empty())        rows.push_back(2);
    return rows;
}
const float ROW_STEP = 26, ROW_H = 22;
void rowBox(const DetailLayout& L, int i, float& x, float& y, float& w, float& h)
{
    x = L.rowsX - 6; y = L.playY + i * ROW_STEP; w = L.rowsW + 6; h = ROW_H;
}
/* the rectangular buttons' corners */
const float BTN_R = 3;

const float OV_SIZE = 14, OV_STEP = 18;   /* the overview's lines */
const float CREDITS_Y = 354;              /* Cast, Director, genres */

}  // namespace

bool LibraryView::updateDetail(ir_t& ir, bool aPressed) {
    /* the whole overview over the page: Up / Down a line, B or - closes */
    if (overviewPanel) {
        const int maxTop = (int)overviewAll.size() > OV_PANEL_ROWS ? (int)overviewAll.size() - OV_PANEL_ROWS : 0;
        if (Input::isBackPressed() || Input::isLPressed() || aPressed) overviewPanel = false;
        else if (Input::isUpPressed()   && overviewTop > 0)      overviewTop--;
        else if (Input::isDownPressed() && overviewTop < maxTop) overviewTop++;
        else if (Input::isLeftPressed())  overviewTop = overviewTop > OV_PANEL_ROWS ? overviewTop - OV_PANEL_ROWS : 0;
        else if (Input::isRightPressed()) overviewTop = overviewTop + OV_PANEL_ROWS < maxTop ? overviewTop + OV_PANEL_ROWS : maxTop;
        return false;
    }
    /* the pointer on the back or favourite button */
    if (aPressed && overBack(ir)) {
        freeDetail();
        state = detailReturnState;
        return false;
    }
    if (aPressed) {
        const DetailLayout L = detailLayout(detailWideThumb());
        if (inBox(ir, L.bx, L.favY, L.bw, L.favH)) {
            toggleFavorite(detailItemId, detail.isFavorite);
            return false;
        }
        const std::vector<int> rows = trackRows(detail);
        for (int i = 0; i < (int)rows.size(); ++i) {
            float x, y, w, h;
            rowBox(L, i, x, y, w, h);
            if (!inBox(ir, x, y, w, h)) continue;
            detailFocusRow = rows[i];
            if (rows[i] == 2) { state = State::ScenesLoad; return false; }
            if (rows[i] < 0)
                selectVersion((detailVersionSel + 1) % (int)detail.versions.size());
            else if (rows[i] == 0 && ++detailAudioSel >= (int)detail.audioStreams.size())
                detailAudioSel = 0;
            else if (rows[i] == 1 && ++detailSubSel >= (int)detail.subtitleStreams.size())
                detailSubSel = -1;
            return false;
        }
    }
    /* -, or A with the pointer on it: the overview the page cuts short */
    const bool onOverview = ir.valid && ir.x >= MARGIN && ir.x < 640 - MARGIN &&
                            ir.y >= detailOvY0 && ir.y < detailOvY1;
    if (overviewCut && (Input::isLPressed() || (aPressed && onOverview))) {
        SYS_Report("[Detail] the whole overview: %d lines\n", (int)overviewAll.size());
        overviewPanel = true;
        overviewTop = 0;
        return false;
    }
    /* A on the Scenes row: its chapters */
    if (aPressed && detailFocusRow == 2 && !detail.chapters.empty()) {
        state = State::ScenesLoad;
        return false;
    }
    // A: play
    if (aPressed) {
        if (detail.playbackPositionTicks > 0) {
            // There is a saved position — ask the user Continue / Start Over
            resumeSel = 0;
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
    } else if (Input::isUpPressed() || Input::isDownPressed()) {
        /* the rows there are: Version (several files), Audio, Subtitles,
         * Scenes (chapters); Audio when none */
        std::vector<int> rows;
        if (!detail.versions.empty())        rows.push_back(-1);
        if (!detail.audioStreams.empty())    rows.push_back(0);
        if (!detail.subtitleStreams.empty()) rows.push_back(1);
        if (!detail.chapters.empty())        rows.push_back(2);
        int at = -1;
        for (int i = 0; i < (int)rows.size(); ++i) if (rows[i] == detailFocusRow) at = i;
        if (rows.empty()) detailFocusRow = 0;
        else if (at < 0) detailFocusRow = rows[0];
        else detailFocusRow = rows[Input::isUpPressed() ? (at > 0 ? at - 1 : 0)
                                                        : (at + 1 < (int)rows.size() ? at + 1 : at)];
    } else if (detailFocusRow < 0 && (Input::isLeftPressed() || Input::isRightPressed())) {
        const int n = (int)detail.versions.size();
        selectVersion((detailVersionSel + (Input::isLeftPressed() ? n - 1 : 1)) % n);
    } else if (Input::isLeftPressed()) {
        if (detailFocusRow == 0 && !detail.audioStreams.empty()) {
            if (--detailAudioSel < 0) detailAudioSel = (int)detail.audioStreams.size() - 1;
        } else if (detailFocusRow == 1 && !detail.subtitleStreams.empty()) {
            if (--detailSubSel < -1) detailSubSel = (int)detail.subtitleStreams.size() - 1;
        }
    } else if (Input::isRightPressed()) {
        if (detailFocusRow == 0 && !detail.audioStreams.empty()) {
            if (++detailAudioSel >= (int)detail.audioStreams.size()) detailAudioSel = 0;
        } else if (detailFocusRow == 1) {
            if (++detailSubSel >= (int)detail.subtitleStreams.size()) detailSubSel = -1;
        }
    }
    return false;
}

bool LibraryView::updateResumePrompt(ir_t& ir, bool aPressed) {
    // IR hover: set resumeSel to whichever button the cursor is over
    if (ir.valid) {
        const int DW2 = 340, DH2 = 120;
        const int DX2 = (640 - DW2) / 2;
        const int DY2 = (480 - DH2) / 2;
        const int BW2 = 130, BH2 = 30, BGAP2 = 16;
        const int bTotalW2 = BW2 * 2 + BGAP2;
        const int bStartX2 = DX2 + (DW2 - bTotalW2) / 2;
        const int bY2      = DY2 + DH2 - BH2 - 14;
        for (int i = 0; i < 2; ++i) {
            int bx = bStartX2 + i * (BW2 + BGAP2);
            if (ir.x >= bx && ir.x < bx + BW2 &&
                ir.y >= bY2 && ir.y < bY2 + BH2) {
                resumeSel = i;
                break;
            }
        }
    }
    // Left/Right toggles between Continue (0) and Start Over (1)
    if (Input::isLeftPressed() || Input::isRightPressed())
        resumeSel ^= 1;
    if (Input::isBackPressed()) {
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

    // Dialog box (DW/DH/button geometry shared with update())
    const int DW = 340, DH = 120;
    const int DX = (640 - DW) / 2;
    const int DY = (480 - DH) / 2;
    Ui::card(DX, DY, DW, DH + 34, 18, 0.0f);

    Ui::textCentered(320, DY + 14, tr("Resume playback?"), 17, p.text);
    {
        int secs = (int)(detail.playbackPositionTicks / 10000000LL);
        int rh = secs / 3600, rm = (secs % 3600) / 60, rs = secs % 60;
        char hint[48];
        /* TRANSLATORS: under "Resume playback?": where it stopped, h:mm:ss or m:ss */
        if (rh > 0) snprintf(hint, sizeof(hint), tr("at %d:%02d:%02d"), rh, rm, rs);
        else        snprintf(hint, sizeof(hint), tr("at %d:%02d"), rm, rs);
        Ui::textCentered(320, DY + 38, hint, 13, p.accentDark);
    }

    // Two buttons: Continue | From Start
    const char* btnLabels[2] = { N_("Continue"), N_("From Start") };
    const int BW = 130, BH = 30, BGAP = 16;
    int bTotalW = BW * 2 + BGAP;
    int bStartX = DX + (DW - bTotalW) / 2;
    int bY      = DY + DH - BH - 14;
    for (int i = 0; i < 2; ++i)
        Ui::button(bStartX + i * (BW + BGAP), bY, BW, BH, btnLabels[i], 15,
                   resumeSel == i ? Ui::pulse() : 0.0f);

    // Hints inside the dialog
    const Ui::Hint h[] = { { "LR", N_("Select") }, { "A", N_("Confirm") }, { "B", N_("Cancel") } };
    float hw = 0;
    for (const auto& hi : h) hw += Ui::hintWidth(hi);
    float hx = 320 - (hw - 14) * 0.5f;
    Ui::roundRect(DX + 16, DY + DH - 2, DW - 32, 1.5f, 0.75f, Ui::alpha(p.cardBorder, 0.6f));
    for (const auto& hi : h) hx += Ui::hint(hx, DY + DH + 6, hi);
}

// ---------------------------------------------------------------
// drawDetailView(): the old video channels' title page.  Under the red
// band: the title, a small poster with Play and the favourite button
// beside it (the tracks further right), then a line of stars, rating,
// year and running time, the overview, and Cast / Director / genres.
// ---------------------------------------------------------------
void LibraryView::drawDetailView(ir_t& ir) {
    const Ui::Palette& p = Ui::pal();
    const DetailLayout L = detailLayout(detailWideThumb());
    const int pageW = 640 - 2 * (int)MARGIN;

    Ui::backButton(backCx(), BACK_CY, BACK_R, overBack(ir));

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

    // ---- Poster, Play, favourite ----
    const bool posterHover = inBox(ir, L.px, L.py, L.pw * WiiUtils::wsScaleX(), L.ph);
    drawThumb(detailTex, L.px, L.py, L.pw, L.ph, posterHover ? 0.8f : 0.0f, progressOf(detail),
              detail.name.c_str(), false);
    const bool playHover = inBox(ir, L.bx, L.playY, L.bw, L.playH);
    const bool favHover  = inBox(ir, L.bx, L.favY, L.bw, L.favH);
    /* Play is what A does: orange under the pointer, and while it is away */
    Ui::button(L.bx, L.playY, L.bw, L.playH, N_("Play"), 19,
               playHover || !ir.valid ? 1.0f : 0.0f, BTN_R);
    Ui::button(L.bx, L.favY, L.bw, L.favH,
               detail.isFavorite ? N_("Remove from Queue") : N_("Add to Queue"), 14,
               favHover ? 1.0f : 0.0f, BTN_R);

    // ---- Audio / Subtitle / Version / Scenes, right of the buttons ----
    const bool hasVersions = !detail.versions.empty();
    const bool hasScenes   = !detail.chapters.empty();
    const bool hasAudio    = !detail.audioStreams.empty();
    const bool hasSub      = !detail.subtitleStreams.empty();
    float rowsBottom = L.playY;
    if (hasAudio || hasSub || hasVersions || hasScenes) {
        // Validate UTF-8 and truncate at a safe codepoint boundary so the
        // renderer never receives a broken multi-byte sequence.
        auto safeTitle = [](const char* s, int maxCodepoints) -> std::string {
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
        };

        const int valX = (int)L.rowsX + 78, valW = (int)(L.rowsX + L.rowsW) - valX - 4;
        const std::vector<int> rows = trackRows(detail);
        for (int i = 0; i < (int)rows.size(); ++i) {
            const int row = rows[i];
            float bx, by, bw, bh;
            rowBox(L, i, bx, by, bw, bh);
            /* white buttons: orange under the pointer, or the D-pad's row
             * (Left / Right change it) while the pointer is away */
            const bool focused = ir.valid ? inBox(ir, bx, by, bw, bh) : detailFocusRow == row;
            Ui::button(bx, by, bw, bh, "", 13, focused ? 1.0f : 0.0f, BTN_R);
            const int ry = (int)(by + (bh - 13) * 0.5f) - 1;
            const u32 labelCol = focused ? p.textOnAccent : p.textDim;
            const u32 valueCol = focused ? p.textOnAccent : p.text;
            if (row == 2) {   /* the Scenes: A opens them */
                char buf[64];
                snprintf(buf, sizeof(buf), tr("%d chapters"), (int)detail.chapters.size());
                std::string v = std::string(buf) + "  \xe2\x80\xba";
                Ui::text(L.rowsX + 4, ry, tr("Scenes"), 13, labelCol);
                Text::print(valX, ry, font, v.c_str(), 13, valueCol);
            } else {
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
                std::string t = fitText(font, safeTitle(rawTitle, 36), 13, valW - 24);
                char buf[96];
                snprintf(buf, sizeof(buf), "\xe2\x80\xb9 %s \xe2\x80\xba", t.c_str());
                Text::print(valX, ry, font, buf, 13, valueCol);
            }
        }
        rowsBottom = L.playY + rows.size() * ROW_STEP;
    }

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
        const bool hover = overviewCut && ir.valid && ir.x >= MARGIN && ir.x < MARGIN + pageW &&
                           ir.y >= detailOvY0 && ir.y < detailOvY1;
        for (const auto& line : detailLines) {
            Ui::text(MARGIN, y, line.c_str(), (int)OV_SIZE, hover ? p.accentDark : p.text);
            y += OV_STEP;
        }
        if (overviewCut) {   /* the rest of it: - (or A on the text) */
            const Ui::Hint more = { "-", N_("Read more") };
            Ui::hint(MARGIN + pageW - Ui::hintWidth(more) + 14, y - 1, more);
            y += OV_STEP;
        }
        detailOvY1 = y;
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

    // ---- Footer ----
    const Ui::Hint l[] = { { "A", detailFocusRow == 2 && hasScenes ? N_("Scenes") : N_("Play") }, { "B", N_("Back") },
                           { "1", N_("Queue") },   /* add / remove: the button says which */
                           { "+!", detail.played ? N_("Unwatched") : N_("Watched") },
                           { "2", N_("Extras") } };   /* short: 5 hints + Focus, Change */
    const Ui::Hint r[] = { { "UD", N_("Focus") }, { "LR", N_("Change") } };
    const bool full = detail.specialFeatures > 0;   /* no room left for Focus */
    Ui::footer(l, full ? 5 : 4, full ? r + 1 : r, (hasAudio || hasSub || hasVersions || hasScenes) ? (full ? 1 : 2) : 0);
    if (overviewPanel) renderOverviewPanel();
}

/* The whole overview in a panel over the page */
void LibraryView::renderOverviewPanel() {
    const Ui::Palette& p = Ui::pal();
    GRRLIB_Rectangle(Ui::screenLeft(), 0, Ui::screenWidth(), 480, p.dim, 1);
    /* the title on up to three lines: the whole name of a long episode */
    const std::vector<std::string> title = wrapText(filterDejaVu(detail.name, 200), 18, OV_PANEL_W, 3);
    const int TH = 22 * (title.empty() ? 1 : (int)title.size());
    const int PW = OV_PANEL_W + 48, PH = 42 + TH + OV_PANEL_ROWS * 20 + 36;
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
    /* hints at the bottom of the panel */
    const Ui::Hint h[] = { { "UD", N_("Scroll") }, { "B", N_("Close") } };
    const int nh = n > OV_PANEL_ROWS ? 2 : 1;
    float hw = 0;
    for (int i = 2 - nh; i < 2; i++) hw += Ui::hintWidth(h[i]);
    float hx = 320 - (hw - 14) * 0.5f;
    for (int i = 2 - nh; i < 2; i++) hx += Ui::hint(hx, PY + PH - 28, h[i]);
}
