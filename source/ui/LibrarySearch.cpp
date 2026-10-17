/* LibraryView: Search: query keyboard and results. */
#include "LibraryView.h"
#include "LibraryDraw.h"
#include "Ui.h"
#include "../input/Input.h"
#include "../core/Text.h"
#include "../core/Lang.h"
#include <stdio.h>
#include <ogc/lwp_watchdog.h>   // gettime(), ticks_to_millisecs()

using namespace LibDraw;

bool LibraryView::updateSearchInput(ir_t& ir) {
    searchKb.setEnterLabel(N_("Enter"));
    switch (searchKb.update(ir, searchQuery, 64)) {
    case Keyboard::Result::Enter:            // Search key or [+]
        if (!searchQuery.empty()) state = State::SearchLoad;
        break;
    case Keyboard::Result::Cancel:           // B with an empty query
        state = searchReturnState;
        break;
    default: break;
    }
    return false;
}

bool LibraryView::updateSearchResults(ir_t& ir, bool aPressed) {
    int n = (int)searchResults.size();
    // B → back to search input
    if (Input::isBackPressed()) {
        state = State::SearchInput;
        return false;
    }
    // [1] → new search
    if (Input::is1Pressed()) {
        searchQuery.clear();
        searchResults.clear();
        searchSel = 0; searchTop = 0;
        searchKb.reset();
        state = State::SearchInput;
        irMode = false;
        return false;
    }
    if (n == 0) return false;

    if (Input::isUpPressed())   { searchSel--; irMode = false; clampSearchScroll(); }
    if (Input::isDownPressed()) { searchSel++; irMode = false; clampSearchScroll(); }

    // IR hover
    if (irMode && ir.valid && !Input::isUpPressed() && !Input::isDownPressed()) {
        for (int i = 0; i < SEARCH_VISIBLE; i++) {
            int idx = searchTop + i;
            if (idx >= n) break;
            int ry = LIST_Y + i * ROW_H;
            if (ir.x >= LIST_X && ir.x <= LIST_X + LIST_W &&
                ir.y >= ry && ir.y < ry + ROW_H) {
                searchSel = idx;
                irMode = true;
                clampSearchScroll();
            }
        }
    }

    if (aPressed && n > 0 && searchSel < n) {
        const JellyfinItem& sel = searchResults[searchSel];
        if (sel.type == "Series") {
            currentSeriesId   = sel.id;
            currentSeriesName = sel.name;
            showReturnState = State::SearchReady;
            state = State::ShowLoad;
        } else if (sel.type == "BoxSet" && Ui::listMode()) {
            // text list of the collection; B comes back to the results
            inItemsDrilldown    = true;
            drilldownFromSearch = true;
            currentLibId      = sel.id;
            currentLibName    = sel.name;
            currentLibType    = "boxsets";
            posterMode        = false;
            itemPage          = 0;
            state = State::ItemsInit;
        } else if (sel.type == "BoxSet") {
            inBoxSetDrilldown   = true;
            drilldownFromSearch = true;
            currentLibId      = sel.id;
            currentLibName    = sel.name;
            posterMode        = true;
            itemPage          = 0;
            freePosters();
            state = State::ItemsInit;
        } else if (sel.type == "Episode" && !sel.seriesId.empty()) {
            openShow(sel.seriesId, sel.seriesName, sel.id, State::SearchReady);
        } else {
            detailItemId        = sel.id;
            detailReturnState   = State::SearchReady;
            detailIsEpisodeHint = (sel.type == "Episode");
            state = State::DetailLoad;
        }
    }
    return false;
}

// ---------------------------------------------------------------
void LibraryView::clampSearchScroll() {
    int n = (int)searchResults.size();
    if (searchSel < 0) searchSel = 0;
    if (n > 0 && searchSel >= n) searchSel = n - 1;
    if (searchSel < searchTop) searchTop = searchSel;
    if (searchSel >= searchTop + SEARCH_VISIBLE) searchTop = searchSel - SEARCH_VISIBLE + 1;
    if (searchTop < 0) searchTop = 0;
}

// ---------------------------------------------------------------
void LibraryView::performSearch() {
    searchResults.clear();
    bool ok = false; std::string err;
    runWithLoading([&]() {
        ok = client.searchItems(serverUrl, auth, searchQuery, 50, searchResults);
        if (!ok) err = client.lastError();
    });
    if (!ok) { errMsg = err; state = State::Error; return; }
    searchSel = 0;
    searchTop = 0;
    state = State::SearchReady;
}

// ---------------------------------------------------------------
void LibraryView::renderSearchInput(ir_t& ir) {
    Ui::header(tr("Search"));
    /* the Netflix channel's keyboard, the whole screen under the band */
    searchKb.render(ir, searchQuery, false, tr("Type a title, an actor..."));
}

// ---------------------------------------------------------------
void LibraryView::renderSearchResults(ir_t& ir) {
    const Ui::Palette& p = Ui::pal();
    int n = (int)searchResults.size();

    // Header
    {
        char sc[32] = "";
        if (n > SEARCH_VISIBLE) snprintf(sc, sizeof(sc), "%d / %d", searchSel + 1, n);
        char hb[256];
        snprintf(hb, sizeof(hb), tr("Results: %s"), filterDejaVu(searchQuery, 40).c_str());
        std::string hdr = fitText(font, hb, 24, 460);
        Ui::header(hdr.c_str(), sc[0] ? sc : nullptr);
    }

    if (n == 0) {
        Ui::textCentered(320, 200, tr("No results."), 20, p.textDim);
    } else {
        for (int i = 0; i < SEARCH_VISIBLE; i++) {
            int idx = searchTop + i;
            if (idx >= n) break;
            int   ry  = LIST_Y + i * ROW_H;
            bool  sel = (idx == searchSel);
            bool  hov = ir.valid &&
                        ir.x >= LIST_X && ir.x <= LIST_X + LIST_W &&
                        ir.y >= ry && ir.y < ry + ROW_H;
            float f = focusOf(sel, hov);
            drawRow(LIST_X - 4, ry, LIST_W + 8, ROW_H, f);

            // Type badge
            const std::string& type = searchResults[idx].type;
            const char* badge = "?";
            u32 bCol = 0x446688FF;
            /* TRANSLATORS: type badges of the search results, 54 pixels wide: keep them short */
            if      (type == "Movie")       { badge = N_("MOVIE");   bCol = 0x2D7DE0FF; }
            else if (type == "Series")      { badge = N_("SERIES");  bCol = 0xE0563FFF; }
            else if (type == "Episode")     { badge = N_("EP");      bCol = 0xC2453AFF; }
            else if (type == "BoxSet")      { badge = N_("COLLEC");  bCol = 0x8E54D6FF; }
            Ui::roundRect(LIST_X + 8, ry + 12, 54, 20, 10, Ui::mix(bCol, 0xFFFFFFFF, 0.15f), bCol);
            Ui::textCentered(LIST_X + 35, ry + 15, tr(badge), 10, 0xFFFFFFFF);

            // Title
            std::string label = filterDejaVu(searchResults[idx].name, 200);
            if (type == "Episode" && !searchResults[idx].seriesName.empty()) {
                // Show "Series S01E02 - Episode title"
                char ep[64];
                /* TRANSLATORS: season and episode numbers before the episode's title */
                snprintf(ep, sizeof(ep), tr("S%02dE%02d - "),
                         searchResults[idx].seasonNumber,
                         searchResults[idx].episodeNumber);
                label = filterDejaVu(searchResults[idx].seriesName, 200) + " " + ep + filterDejaVu(searchResults[idx].name, 200);
            } else if (searchResults[idx].year > 0) {
                char yb[16];
                snprintf(yb, sizeof(yb), " (%d)", searchResults[idx].year);
                label += yb;
            }
            titleText(0, LIST_X + 74, ry + 12, LIST_W - 90, label, 16, Ui::mix(p.text, p.accentDark, f), sel);
        }
    }

    const Ui::Hint l[] = { { "A", N_("Open") }, { "1", N_("New search") } };
    const Ui::Hint r[] = { { "B", N_("Back") } };
    Ui::footer(l, 2, r, 1);
}
