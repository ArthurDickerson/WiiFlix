/* LibraryView: Library tabs with rows of cards: movie and TV suggestions, coming up. */
#include "LibraryView.h"
#include "LibraryDraw.h"
#include "Ui.h"
#include "../input/Input.h"
#include "../core/Utils.h"
#include "../core/Lang.h"

using namespace LibDraw;

// One horizontally scrolling row of poster cards with its end chevrons.
/* The pointer picks the card it is over (it only switched rows before:
 * A then opened the card chosen with the D-pad, the first one by default) */
int LibraryView::suggestionHover(const ir_t& ir, int rowY, int off, int n) const {
    if (!ir.valid || ir.y < rowY || ir.y >= rowY + SG_CH) return -1;
    for (int si = 0; si < SUGG_VISIBLE && off + si < n; si++) {
        int cx = SG_X0 + si * (SG_CW + SG_GAP);
        if (ir.x >= cx && ir.x < cx + SG_CW) return off + si;
    }
    return -1;
}

void LibraryView::drawSuggestionRow(ir_t& ir, int rowY, const std::vector<JellyfinItem>& list,
                                    GRRLIB_texImg* const* texs, int off, int selIdx, bool rowActive,
                                    bool preferSeries, bool showProgress) {
    const Ui::Palette& p = Ui::pal();
    int nItems = (int)list.size();
    for (int pass = 0; pass < 2; pass++) {
        for (int si = 0; si < SUGG_VISIBLE; si++) {
            int i = off + si;
            if (i >= nItems) break;
            bool sel = rowActive && i == selIdx;
            if (sel != (pass == 1)) continue;
            int  cx  = SG_X0 + si * (SG_CW + SG_GAP);
            bool hov = ir.valid && ir.x >= cx && ir.x < cx + SG_CW
                                && ir.y >= rowY && ir.y < rowY + SG_CH;
            const JellyfinItem& item = list[i];
            const std::string& nm = (preferSeries && !item.seriesName.empty()) ? item.seriesName : item.name;
            drawThumb(texs[i], cx, rowY, SG_CW, SG_CH, focusOf(sel, hov),
                      showProgress ? progressOf(item) : -1.0f, nm.c_str());
            int visW = (int)(SG_CW * WiiUtils::wsScaleX() + 0.5f);
            titleText(1, cx + 1, rowY + SG_CH + 4, visW, filterDejaVu(nm, 200), 12, sel ? p.text : p.textDim, sel);
        }
    }
    int ay = rowY + SG_CH / 2;
    drawRowChevron(SG_X0 - 9, ay, true, off > 0);
    drawRowChevron(SG_X0 + SUGG_VISIBLE * (SG_CW + SG_GAP) - SG_GAP + 6, ay, false,
                   off + SUGG_VISIBLE < nItems);
}

bool LibraryView::updateMovieSuggestions(ir_t& ir, bool aPressed) {
    if (Input::isBackPressed()) {
        freeMovieSuggestions();
        state = State::LibsReady;
        return false;
    }
    // Tab switch with -/+
    if (Input::isLPressed()) { goToTab((movieTab + 3) % 4); return false; }
    if (Input::isRPressed()) { goToTab((movieTab + 1) % 4); return false; }
    {
        int nc = (int)movieContItems.size();
        int nr = (int)movieRecentItems.size();

        if (Input::isUpPressed()   && movieSuggestRow == 1) { movieSuggestRow = 0; irMode = false; }
        if (Input::isDownPressed() && movieSuggestRow == 0 && nr > 0) { movieSuggestRow = 1; irMode = false; }

        int& colSel = (movieSuggestRow == 0) ? movieSuggestContSel : movieSuggestRecSel;
        int& colOff = (movieSuggestRow == 0) ? movieSuggestContOff : movieSuggestRecOff;
        int  sz     = (movieSuggestRow == 0) ? nc : nr;

        if (Input::isLeftPressed() && colSel > 0) {
            colSel--; irMode = false;
            if (colSel < colOff) colOff = colSel;
        }
        if (Input::isRightPressed() && colSel < sz - 1) {
            colSel++; irMode = false;
            if (colSel >= colOff + SUGG_VISIBLE) colOff = colSel - SUGG_VISIBLE + 1;
        }

        // IR hover: only switches the active ROW when the cursor *moves into*
        // a row zone — prevents a stationary pointer from overriding d-pad
        // row navigation every frame.
        const int SG_X0_H    = 15, SG_CW_H = POSTER_W, SG_CH_H = 160;
        const int SG_GAP_H   = 20;
        const int SG_ROW0_YH = 65, SG_ROW1_YH = 270;
        const int SG_ROW_W   = SUGG_VISIBLE * (SG_CW_H + SG_GAP_H) - SG_GAP_H;
        {
            static int lastIrRow = -1;
            int hoverRow = -1;
            if (irMode && ir.valid && !Input::isUpPressed() && !Input::isDownPressed()
                         && !Input::isLeftPressed() && !Input::isRightPressed()) {
                if (ir.x >= SG_X0_H && ir.x < SG_X0_H + SG_ROW_W) {
                    if (ir.y >= SG_ROW0_YH && ir.y < SG_ROW0_YH + SG_CH_H) hoverRow = 0;
                    else if (ir.y >= SG_ROW1_YH && ir.y < SG_ROW1_YH + SG_CH_H) hoverRow = 1;
                }
            }
            if (hoverRow != lastIrRow) {
                if (hoverRow >= 0) { movieSuggestRow = hoverRow; irMode = true; }
                lastIrRow = hoverRow;
            }
            if (irMode) {
                int h0 = suggestionHover(ir, SG_ROW0_YH, movieSuggestContOff, (int)movieContItems.size());
                int h1 = suggestionHover(ir, SG_ROW1_YH, movieSuggestRecOff, (int)movieRecentItems.size());
                if (h0 >= 0)      { movieSuggestRow = 0; movieSuggestContSel = h0; }
                else if (h1 >= 0) { movieSuggestRow = 1; movieSuggestRecSel  = h1; }
            }
        }

        if (aPressed) {
            auto& selItems = (movieSuggestRow == 0) ? movieContItems : movieRecentItems;
            int   sel      = (movieSuggestRow == 0) ? movieSuggestContSel : movieSuggestRecSel;
            if (!selItems.empty() && sel < (int)selItems.size()) {
                detailItemId        = selItems[sel].id;
                detailReturnState   = State::MovieSuggestionsReady;
                detailIsEpisodeHint = false;
                state = State::DetailLoad;
            }
        }
    }
    return false;
}

bool LibraryView::updateTVSuggestions(ir_t& ir, bool aPressed) {
    if (Input::isBackPressed()) {
        freeTVSuggestions();
        state = State::LibsReady;
        return false;
    }
    // Tab switch with -/+
    if (Input::isLPressed()) { goToTab((tvTab + 2) % 3); return false; }
    if (Input::isRPressed()) { goToTab((tvTab + 1) % 3); return false; }
    {
        int nc = (int)tvContItems.size();
        int nr = (int)tvRecentItems.size();

        if (Input::isUpPressed()   && tvSuggestRow == 1) { tvSuggestRow = 0; irMode = false; }
        if (Input::isDownPressed() && tvSuggestRow == 0 && nr > 0) { tvSuggestRow = 1; irMode = false; }

        int& colSel = (tvSuggestRow == 0) ? tvSuggestContSel : tvSuggestRecSel;
        int& colOff = (tvSuggestRow == 0) ? tvSuggestContOff : tvSuggestRecOff;
        int  sz     = (tvSuggestRow == 0) ? nc : nr;

        if (Input::isLeftPressed() && colSel > 0) {
            colSel--; irMode = false;
            if (colSel < colOff) colOff = colSel;
        }
        if (Input::isRightPressed() && colSel < sz - 1) {
            colSel++; irMode = false;
            if (colSel >= colOff + SUGG_VISIBLE) colOff = colSel - SUGG_VISIBLE + 1;
        }

        // IR row hover
        const int SG_X0_H    = 15, SG_CW_H = POSTER_W, SG_CH_H = 160;
        const int SG_GAP_H   = 20;
        const int SG_ROW0_YH = 65, SG_ROW1_YH = 270;
        const int SG_ROW_W   = SUGG_VISIBLE * (SG_CW_H + SG_GAP_H) - SG_GAP_H;
        {
            static int lastIrRowTV = -1;
            int hoverRow = -1;
            if (irMode && ir.valid && !Input::isUpPressed() && !Input::isDownPressed()
                         && !Input::isLeftPressed() && !Input::isRightPressed()) {
                if (ir.x >= SG_X0_H && ir.x < SG_X0_H + SG_ROW_W) {
                    if (ir.y >= SG_ROW0_YH && ir.y < SG_ROW0_YH + SG_CH_H) hoverRow = 0;
                    else if (ir.y >= SG_ROW1_YH && ir.y < SG_ROW1_YH + SG_CH_H) hoverRow = 1;
                }
            }
            if (hoverRow != lastIrRowTV) {
                if (hoverRow >= 0) { tvSuggestRow = hoverRow; irMode = true; }
                lastIrRowTV = hoverRow;
            }
            if (irMode) {
                int h0 = suggestionHover(ir, SG_ROW0_YH, tvSuggestContOff, (int)tvContItems.size());
                int h1 = suggestionHover(ir, SG_ROW1_YH, tvSuggestRecOff, (int)tvRecentItems.size());
                if (h0 >= 0)      { tvSuggestRow = 0; tvSuggestContSel = h0; }
                else if (h1 >= 0) { tvSuggestRow = 1; tvSuggestRecSel  = h1; }
            }
        }

        if (aPressed) {
            if (tvSuggestRow == 0) {
                // Continue watching: episode → detail view
                if (!tvContItems.empty() && tvSuggestContSel < nc) {
                    detailItemId        = tvContItems[tvSuggestContSel].id;
                    detailReturnState   = State::TVSuggestionsReady;
                    detailIsEpisodeHint = true;
                    state = State::DetailLoad;
                }
            } else {
                // Recently added series → season list
                if (!tvRecentItems.empty() && tvSuggestRecSel < nr) {
                    currentSeriesId    = tvRecentItems[tvSuggestRecSel].id;
                    currentSeriesName  = tvRecentItems[tvSuggestRecSel].name;
                    seasonsCallerState = State::TVSuggestionsReady;
                    seasons.clear(); seasonSel = 0; seasonTop = 0;
                    state = State::SeasonsLoad;
                }
            }
        }
    }
    return false;
}

bool LibraryView::updateTVUpcoming(ir_t& ir, bool aPressed) {
    if (Input::isBackPressed()) {
        freeTVUpcoming();
        state = State::LibsReady;
        return false;
    }
    // Tab switch with -/+
    if (Input::isLPressed()) { goToTab((tvTab + 2) % 3); return false; }
    if (Input::isRPressed()) { goToTab((tvTab + 1) % 3); return false; }
    {
        int nu = (int)tvUpcomingItems.size();
        if (Input::isLeftPressed() && tvUpcomingSel > 0) {
            tvUpcomingSel--; irMode = false;
            if (tvUpcomingSel < tvUpcomingOff) tvUpcomingOff = tvUpcomingSel;
        }
        if (Input::isRightPressed() && tvUpcomingSel < nu - 1) {
            tvUpcomingSel++; irMode = false;
            if (tvUpcomingSel >= tvUpcomingOff + SUGG_VISIBLE)
                tvUpcomingOff = tvUpcomingSel - SUGG_VISIBLE + 1;
        }
        if (irMode) {   /* the card under the pointer (row drawn at y 80) */
            int h = suggestionHover(ir, 80, tvUpcomingOff, nu);
            if (h >= 0) tvUpcomingSel = h;
        }
        if (aPressed && nu > 0 && tvUpcomingSel < nu) {
            detailItemId        = tvUpcomingItems[tvUpcomingSel].id;
            detailReturnState   = State::TVUpcomingReady;
            detailIsEpisodeHint = (tvUpcomingItems[tvUpcomingSel].type == "Episode");
            state = State::DetailLoad;
        }
    }
    return false;
}

// Movie Suggestions (continue watching + recently added)
void LibraryView::renderMovieSuggestions(ir_t& ir) {
    const Ui::Palette& p = Ui::pal();
    drawLibHeader(currentLibName, kMovieTabs, 4, movieTab);
    const int SG_ROW0_Y = 65, SG_ROW1_Y = 270;
    int nc = (int)movieContItems.size();
    int nr = (int)movieRecentItems.size();

    sectionLabel(SG_X0, 50, tr("IN PROGRESS"), nc > 0);
    if (nc > 0)
        drawSuggestionRow(ir, SG_ROW0_Y, movieContItems, movieContTex, movieSuggestContOff,
                              movieSuggestContSel, movieSuggestRow == 0, false, true);
    else
        Ui::text(SG_X0 + 20, SG_ROW0_Y + 60, tr("No movie in progress"), 14, p.textDim);

    sectionLabel(SG_X0, 252, tr("RECENTLY ADDED"), nr > 0);
    if (nr > 0)
        drawSuggestionRow(ir, SG_ROW1_Y, movieRecentItems, movieRecentTex, movieSuggestRecOff,
                              movieSuggestRecSel, movieSuggestRow == 1, false, true);
    else
        Ui::text(SG_X0 + 20, SG_ROW1_Y + 60, tr("No recent movies"), 14, p.textDim);

    const Ui::Hint l[] = { { "A", N_("Detail") }, { "B", N_("Back") } };
    const Ui::Hint r[] = { { "-/+", N_("Tab") } };
    Ui::footer(l, 2, r, 1);
}

// TV Suggestions (continue watching episodes + recently added series)
void LibraryView::renderTVSuggestions(ir_t& ir) {
    const Ui::Palette& p = Ui::pal();
    drawLibHeader(currentLibName, kTvTabs, 3, tvTab);
    const int SG_ROW0_Y = 65, SG_ROW1_Y = 270;
    int nc = (int)tvContItems.size();
    int nr = (int)tvRecentItems.size();

    sectionLabel(SG_X0, 50, tr("IN PROGRESS"), nc > 0);
    if (nc > 0)
        drawSuggestionRow(ir, SG_ROW0_Y, tvContItems, tvContTex, tvSuggestContOff,
                              tvSuggestContSel, tvSuggestRow == 0, true, true);
    else
        Ui::text(SG_X0 + 20, SG_ROW0_Y + 60, tr("No episode in progress"), 14, p.textDim);

    sectionLabel(SG_X0, 252, tr("RECENT SERIES"), nr > 0);
    if (nr > 0)
        drawSuggestionRow(ir, SG_ROW1_Y, tvRecentItems, tvRecentTex, tvSuggestRecOff,
                              tvSuggestRecSel, tvSuggestRow == 1, true, true);
    else
        Ui::text(SG_X0 + 20, SG_ROW1_Y + 60, tr("No recent series"), 14, p.textDim);

    const Ui::Hint l[] = { { "A", N_("Select") }, { "B", N_("Back") } };
    const Ui::Hint r[] = { { "-/+", N_("Tab") } };
    Ui::footer(l, 2, r, 1);
}

// TV Upcoming (unaired episodes)
void LibraryView::renderTVUpcoming(ir_t& ir) {
    const Ui::Palette& p = Ui::pal();
    drawLibHeader(currentLibName, kTvTabs, 3, tvTab);
    int nu = (int)tvUpcomingItems.size();
    if (nu == 0) {
        Ui::card(170, 160, 300, 90, 18, 0.0f);
        Ui::textCentered(320, 182, tr("No content."), 20, p.text);
        Ui::textCentered(320, 214, tr("Make sure metadata download is enabled."), 13, p.textDim);
    } else {
        sectionLabel(SG_X0, 55, tr("COMING UP"), true);
        drawSuggestionRow(ir, 80, tvUpcomingItems, tvUpcomingTex, tvUpcomingOff,
                              tvUpcomingSel, true, true, false);
    }
    const Ui::Hint l[] = { { "A", N_("Detail") }, { "B", N_("Back") } };
    const Ui::Hint r[] = { { "-/+", N_("Tab") } };
    Ui::footer(l, 2, r, 1);
}

