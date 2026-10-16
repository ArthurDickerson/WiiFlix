#include "CoverLine.h"
#include "../core/ExitZone.h"
#include "Ui.h"
#include "../core/Utils.h"
#include "../core/Lang.h"
#include "../input/Input.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ogc/lwp_watchdog.h>

/* Defined in LibraryView.cpp: decodes a JPEG into an RGBA8 texture. */
GRRLIB_texImg* loadJPEGTexture(const u8* data, u32 size);

/* ---- Layout (640x480 column; the bars and the line span the whole
 * screen, which is wider in 16:9 — see Ui::screenLeft/Right) ------------- */
static const float CENTER_X  = 320.0f;
static const float CENTER_Y  = 205.0f;   /* middle of the cover line        */
static const float BIG_W     = 140.0f, BIG_H   = 210.0f;   /* selected, 2:3 */
static const float SMALL_W   =  96.0f, SMALL_H = 144.0f;   /* the others    */
static const float GAP       = 14.0f;
static const float RADIUS    = 6.0f;

/* the wordmark on the red band (Ui::headerBand), then the row's name in a
 * long bar under it, the Up/Down pill beside it */
static const float WORD_X    = 24.0f, WORD_SIZE = 26;
static const float BAR_Y     = 54.0f, BAR_H = 30.0f;
static const float BAR_SIDE  = 20.0f;    /* from the screen edges            */
static const float SWITCH_W  = 76.0f;    /* the ▼▲ pill                      */
static const float SWITCH_GAP = 10.0f;

static const float SCRUB_Y   = 336.0f;   /* centre line of the scrub bar     */
static const float KNOB_R    = 11.0f;
static const float ARROW_X   = 38.0f;    /* arrow centres, from the edges    */
static const float ARROW_W   = 26.0f, ARROW_H = 36.0f;
static const float TITLE_Y   = 360.0f;   /* selected title, under the bar    */
static const float META_Y    = 390.0f;   /* its details, stars, rating, n/N  */
static const float HINTS_Y   = 454.0f;

/* Covers kept in memory around a row's selection (each side) */
static const int   WIN_CURRENT  = 9;
static const int   WIN_NEIGHBOR = 4;
/* Titles per genre: fetched PAGE at a time (the HTTP buffer is 256 KB),
 * at most MAX_ITEMS */
static const int   PAGE      = 150;
static const int   MAX_ITEMS = 900;
static const int   RESUME_MAX = 60;

/* ------------------------------------------------------------------------ */

static unsigned long long nowMs() { return ticks_to_millisecs(gettime()); }

static std::string fit(const std::string& s, int size, int maxW)
{
    if (Ui::textWidth(s.c_str(), size) <= maxW) return s;
    std::string t = s;
    while (!t.empty() && Ui::textWidth((t + "...").c_str(), size) > maxW) {
        while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back();
        if (!t.empty()) t.pop_back();
    }
    return t + "...";
}

/* Name shown for an item: episodes and seasons go by their series. */
static const std::string& displayName(const JellyfinItem& it)
{
    if ((it.type == "Episode" || it.type == "Season") && !it.seriesName.empty()) return it.seriesName;
    return it.name;
}

/* ------------------------------------------------------------------------ */

CoverLine::CoverLine(JellyfinClient& c, const std::string& url, const JellyfinAuth& a)
    : client(c), serverUrl(url), auth(a)
{
    LWP_MutexInit(&lock, false);
}

CoverLine::~CoverLine()
{
    stopLoader();
    for (auto& r : rows) freeTextures(r);
    if (lock != LWP_MUTEX_NULL) LWP_MutexDestroy(lock);
}

void CoverLine::freeTextures(Row& r)
{
    LWP_MutexLock(lock);
    for (size_t i = 0; i < r.tex.size(); ++i) {
        if (r.tex[i]) { GRRLIB_FreeTexture(r.tex[i]); r.tex[i] = nullptr; }
        r.st[i] = TILE_NONE;
    }
    LWP_MutexUnlock(lock);
}

void CoverLine::invalidate()
{
    stopLoader();
    for (auto& r : rows) freeTextures(r);
    /* rows are kept until the rebuild, which reads the selection off them */
    isBuilt = false;
    dragging = false;
}

/* ---- Rows ---------------------------------------------------------------- */

void CoverLine::build(const std::vector<JellyfinLibrary>& libs)
{
    (void)libs;
    stopLoader();

    /* Come back where we were (after playback, say): same row, same title */
    std::string keepRow, keepItem;
    if (rowSel >= 0 && rowSel < (int)rows.size()) {
        keepRow = rows[rowSel].title;
        if (const JellyfinItem* it = selectedItem()) keepItem = it->id;
    }
    for (auto& r : rows) freeTextures(r);
    rows.clear();

    const std::string& uid = auth.userId;
    char q[384];

    /* Continue Watching: fetched now, it is the first thing shown */
    {
        Row r;
        r.title = tr("Continue Watching");
        snprintf(q, sizeof(q), "/Users/%s/Items/Resume?Limit=%d&MediaTypes=Video",
                 uid.c_str(), RESUME_MAX);
        bool ok = client.getItemsByQuery(serverUrl, auth, q, r.items);
        r.tex.assign(r.items.size(), nullptr);
        r.st.assign(r.items.size(), TILE_NONE);
        r.state = ok ? ROW_READY : ROW_FAILED;
        rows.push_back(std::move(r));
    }

    /* Instant Queue: the favourites (fetched later, like the genres) */
    {
        Row r;
        r.title = tr("Instant Queue");
        r.queue = true;
        r.state = ROW_PENDING;
        rows.push_back(std::move(r));
    }

    /* One row per genre of the movies and TV shows (titles fetched later) */
    {
        std::vector<JellyfinItem> genres;
        snprintf(q, sizeof(q),
                 "/Genres?UserId=%s&IncludeItemTypes=Movie,Series&Recursive=true"
                 "&SortBy=SortName&SortOrder=Ascending", uid.c_str());
        client.getItemsByQuery(serverUrl, auth, q, genres);
        for (const auto& g : genres) {
            if (g.id.empty() || g.name.empty()) continue;
            Row r;
            r.title   = g.name;
            r.genreId = g.id;
            r.state   = ROW_PENDING;
            rows.push_back(std::move(r));
        }
    }

    rowSel = 0;
    for (int i = 0; i < (int)rows.size(); ++i)
        if (rows[i].title == keepRow) { rowSel = i; break; }
    pendingSelectId = keepItem;
    if (rows[rowSel].state == ROW_READY) {
        Row& r = rows[rowSel];
        for (int i = 0; i < (int)r.items.size(); ++i)
            if (r.items[i].id == keepItem) { r.sel = i; r.scroll = (float)i; break; }
        pendingSelectId.clear();
    }
    rowFade  = 1.0f;
    rowDir   = 0;
    dragging = false;
    isBuilt  = true;
    SYS_Report("[CoverLine] %d rows (%d to continue)\n", (int)rows.size(), (int)rows[0].items.size());
}

/* Loader thread: the titles of row r (a genre, or the queue), a page at a
 * time. */
void CoverLine::fetchRow(int r)
{
    const std::string genreId = rows[r].genreId;
    const bool queue = rows[r].queue;
    std::vector<JellyfinItem> all;
    bool ok = true;
    char q[384];
    for (int start = 0; start < MAX_ITEMS && !stopReq; start += PAGE) {
        std::vector<JellyfinItem> page;
        int total = 0;
        if (queue)
            snprintf(q, sizeof(q),
                     "/Users/%s/Items?Recursive=true&Filters=IsFavorite"
                     "&IncludeItemTypes=Movie,Series,Season,Episode,Video"
                     "&SortBy=SortName&SortOrder=Ascending&StartIndex=%d&Limit=%d",
                     auth.userId.c_str(), start, PAGE);
        else
            snprintf(q, sizeof(q),
                     "/Users/%s/Items?Recursive=true&IncludeItemTypes=Movie,Series&GenreIds=%s"
                     "&SortBy=SortName&SortOrder=Ascending&StartIndex=%d&Limit=%d",
                     auth.userId.c_str(), genreId.c_str(), start, PAGE);
        if (!client.getItemsByQuery(serverUrl, auth, q, page, &total)) { ok = !all.empty(); break; }
        for (auto& it : page) all.push_back(std::move(it));
        if ((int)page.size() < PAGE || (int)all.size() >= total) break;
    }
    if (stopReq) {                       /* half fetched: try again next time */
        LWP_MutexLock(lock);
        rows[r].state = ROW_PENDING;
        LWP_MutexUnlock(lock);
        return;
    }

    LWP_MutexLock(lock);
    Row& row = rows[r];
    row.items.swap(all);
    row.tex.assign(row.items.size(), nullptr);
    row.st.assign(row.items.size(), TILE_NONE);
    row.sel = 0;
    if (r == rowSel && !pendingSelectId.empty()) {
        for (int i = 0; i < (int)row.items.size(); ++i)
            if (row.items[i].id == pendingSelectId) { row.sel = i; break; }
        pendingSelectId.clear();
    }
    row.scroll = (float)row.sel;
    row.state  = ok ? ROW_READY : ROW_FAILED;
    LWP_MutexUnlock(lock);
    SYS_Report("[CoverLine] %s \"%s\": %d titles\n", queue ? "queue" : "genre",
               row.title.c_str(), (int)row.items.size());
}

bool CoverLine::rowReady(int r) const
{
    if (r < 0 || r >= (int)rows.size()) return false;
    LWP_MutexLock(lock);
    bool ready = rows[r].state == ROW_READY;
    LWP_MutexUnlock(lock);
    return ready;
}

const JellyfinItem* CoverLine::selectedItem() const
{
    if (!rowReady(rowSel)) return nullptr;
    const Row& r = rows[rowSel];
    if (r.sel < 0 || r.sel >= (int)r.items.size()) return nullptr;
    return &r.items[r.sel];
}

unsigned CoverLine::focusKey() const
{
    unsigned k = (unsigned)(rowSel + 1) * 7919u;
    if (rowSel >= 0 && rowSel < (int)rows.size()) k += (unsigned)rows[rowSel].sel * 2654435761u;
    return k;
}

/* ---- Loader thread ------------------------------------------------------ */

void CoverLine::startLoader()
{
    if (loaderThread != LWP_THREAD_NULL || rows.empty()) return;
    static u8 stack[64 * 1024] DEAD_AT_EXIT __attribute__((aligned(32)));
    stopReq = false;
    /* Below the main thread: it runs while the UI waits for vsync. */
    if (LWP_CreateThread(&loaderThread, loaderMain, this, stack, sizeof(stack), 40) < 0)
        loaderThread = LWP_THREAD_NULL;
}

void CoverLine::stopLoader()
{
    if (loaderThread == LWP_THREAD_NULL) return;
    stopReq = true;
    LWP_JoinThread(loaderThread, nullptr);
    loaderThread = LWP_THREAD_NULL;
}

void* CoverLine::loaderMain(void* self)
{
    static_cast<CoverLine*>(self)->loaderLoop();
    return nullptr;
}

/* The row on screen first, then the ones Up and Down would show. */
bool CoverLine::fetchRowIfNeeded()
{
    const int cur = rowSel;
    const int order[3] = { cur, cur + 1, cur - 1 };
    for (int o = 0; o < 3; ++o) {
        int r = order[o];
        if (r < 0 || r >= (int)rows.size()) continue;
        LWP_MutexLock(lock);
        bool take = rows[r].state == ROW_PENDING;
        if (take) rows[r].state = ROW_LOADING;
        LWP_MutexUnlock(lock);
        if (take) { fetchRow(r); return true; }
        if (o == 0 && !rowReady(r)) return false;   /* failed: nothing to prefetch for */
    }
    return false;
}

/* Next cover to fetch: nearest to the selection of the row on screen, then
 * a few around the selection of the rows above and below. */
bool CoverLine::pickCover(int& outR, int& outI)
{
    const int cur = rowSel;
    const int order[3] = { cur, cur + 1, cur - 1 };
    for (int o = 0; o < 3; ++o) {
        int r = order[o];
        if (!rowReady(r)) continue;
        Row& row = rows[r];
        const int n   = (int)row.items.size();
        const int sel = row.sel;
        const int win = o == 0 ? WIN_CURRENT : WIN_NEIGHBOR;
        for (int d = 0; d <= win; ++d) {
            for (int s = 0; s < 2; ++s) {
                if (d == 0 && s == 1) continue;
                int i = s == 0 ? sel + d : sel - d;
                if (i < 0 || i >= n) continue;
                LWP_MutexLock(lock);
                bool take = row.st[i] == TILE_NONE;
                if (take) row.st[i] = TILE_LOADING;
                LWP_MutexUnlock(lock);
                if (take) { outR = r; outI = i; return true; }
            }
        }
    }
    return false;
}

void CoverLine::loaderLoop()
{
    while (!stopReq) {
        if (fetchRowIfNeeded()) continue;
        int r, i;
        if (!pickCover(r, i)) { usleep(30 * 1000); continue; }

        const JellyfinItem& it = rows[r].items[i];
        /* Episodes: the series poster fits a portrait cover better */
        const std::string id = (it.type == "Episode" && !it.seriesId.empty()) ? it.seriesId : it.id;
        const std::string seriesId = it.seriesId;
        const bool season = it.type == "Season";
        std::string bytes;
        client.getItemImageBytes(serverUrl, auth, id, (int)BIG_W, (int)BIG_H, bytes);
        if (bytes.empty() && season && !seriesId.empty())
            client.getItemImageBytes(serverUrl, auth, seriesId, (int)BIG_W, (int)BIG_H, bytes);
        GRRLIB_texImg* t = bytes.empty() ? nullptr
                         : loadJPEGTexture((const u8*)bytes.data(), (u32)bytes.size());

        LWP_MutexLock(lock);
        if (rows[r].st[i] == TILE_LOADING) {          /* not evicted meanwhile */
            rows[r].tex[i] = t;
            rows[r].st[i]  = t ? TILE_READY : TILE_FAILED;
            t = nullptr;
        }
        LWP_MutexUnlock(lock);
        if (t) GRRLIB_FreeTexture(t);
    }
}

/* Main thread, between frames (the GPU is done with what it frees): drop
 * the covers far from every selection. */
void CoverLine::evictFar()
{
    for (int r = 0; r < (int)rows.size(); ++r) {
        if (!rowReady(r)) continue;
        Row& row = rows[r];
        int dist = r > rowSel ? r - rowSel : rowSel - r;
        int keep = dist == 0 ? WIN_CURRENT + 3 : dist == 1 ? WIN_NEIGHBOR + 2 : -1;  /* hysteresis */
        LWP_MutexLock(lock);
        for (int i = 0; i < (int)row.st.size(); ++i) {
            if (row.st[i] == TILE_NONE) continue;
            if (keep >= 0 && i >= row.sel - keep && i <= row.sel + keep) continue;
            if (row.tex[i]) { GRRLIB_FreeTexture(row.tex[i]); row.tex[i] = nullptr; }
            row.st[i] = TILE_NONE;
        }
        LWP_MutexUnlock(lock);
    }
}

/* ---- Geometry ------------------------------------------------------------ */

/* the ▼▲ pill, top right; its left half is ▼ (next row), its right ▲ */
static float switchX() { return Ui::screenRight() - BAR_SIDE - SWITCH_W; }
/* the scrub bar's arrows: centre x of the left (-1) or right (+1) one */
static float arrowX(int dir) { return dir < 0 ? Ui::screenLeft() + ARROW_X : Ui::screenRight() - ARROW_X; }

float CoverLine::trackLeft()  const { return arrowX(-1) + ARROW_W * 0.5f + 22.0f; }
float CoverLine::trackRight() const { return arrowX(+1) - ARROW_W * 0.5f - 22.0f; }

float CoverLine::knobX(const Row& row) const
{
    int n = (int)row.items.size();
    float frac = n > 1 ? row.scroll / (float)(n - 1) : 0.0f;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    return trackLeft() + frac * (trackRight() - trackLeft());
}

/* Cover i of the row at the row's current (animated) scroll position: the
 * one at the scroll position is big, the others small; sizes blend in
 * between so the line glides from one cover to the next. */
void CoverLine::coverRect(const Row& row, int i, float& x, float& y, float& w, float& h) const
{
    float d  = (float)i - row.scroll;
    float ad = fabsf(d);
    float k  = ad < 1.0f ? 1.0f - ad : 0.0f;            /* 1 at the centre */
    w = SMALL_W + (BIG_W - SMALL_W) * k;
    h = SMALL_H + (BIG_H - SMALL_H) * k;
    const float first = BIG_W * 0.5f + GAP + SMALL_W * 0.5f;   /* centre to the next centre */
    float off = ad <= 1.0f ? ad * first : first + (ad - 1.0f) * (SMALL_W + GAP);
    float cx  = CENTER_X + (d < 0 ? -off : off);
    x = cx - w * 0.5f;
    y = CENTER_Y - h * 0.5f;
}

/* ---- Input -------------------------------------------------------------- */

void CoverLine::moveRow(int delta)
{
    int to = rowSel + delta;
    if (to < 0 || to >= (int)rows.size()) return;
    rowSel   = to;
    rowFade  = 0.0f;
    rowDir   = delta;
    dragging = false;
    repeatDir = 0;
}

void CoverLine::step(Row& row, int delta)
{
    int n = (int)row.items.size();
    int s = row.sel + delta;
    if (s < 0) s = 0;
    if (s > n - 1) s = n - 1;
    row.sel = s < 0 ? 0 : s;
}

void CoverLine::setFromTrack(Row& row, float x)
{
    int n = (int)row.items.size();
    if (n < 1) return;
    float frac = (x - trackLeft()) / (trackRight() - trackLeft());
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    row.sel = (int)(frac * (n - 1) + 0.5f);
}

CoverLine::Action CoverLine::update(const ir_t& ir, bool& irMode)
{
    if (rows.empty()) return Action::None;
    evictFar();

    const bool up    = Input::isUpPressed(),   down  = Input::isDownPressed();
    const bool left  = Input::isLeftPressed(), right = Input::isRightPressed();
    const bool aDown = Input::isAJustPressed();
    const bool aHeld = (Input::held() & WPAD_BUTTON_A) != 0;
    if (up || down || left || right) irMode = false;

    if (Input::is1Pressed())      return Action::Search;
    if (Input::isActionPressed()) return Action::Browse;
    if (Input::is2Pressed())      return Action::Profiles;

    if (up)   moveRow(-1);
    if (down) moveRow(+1);

    const bool ready = rowReady(rowSel);
    Row& row = rows[rowSel];
    const int n = ready ? (int)row.items.size() : 0;

    /* ---- Pointer: what is under it ---- */
    hoverCover = -1; hoverArrow = 0; hoverRowBtn = 0; hoverKnob = false; hoverTrack = false;
    const bool ptr = ir.valid && irMode;
    if (ptr) {
        const float px = ir.x, py = ir.y;
        /* the ▼▲ pill: ▼ the next row, ▲ the one before */
        const float sx = switchX();
        if (py >= BAR_Y - 2 && py < BAR_Y + BAR_H + 2 && px >= sx - 2 && px < sx + SWITCH_W + 2) {
            if (px < sx + SWITCH_W * 0.5f) { if (rowSel + 1 < (int)rows.size()) hoverRowBtn = +1; }
            else if (rowSel > 0)                                                  hoverRowBtn = -1;
        }
        if (n > 0) {
            /* covers: the one nearest the middle wins where they overlap */
            float best = 1e9f;
            int lo = (int)floorf(row.scroll) - 8, hi = (int)ceilf(row.scroll) + 8;
            for (int i = lo < 0 ? 0 : lo; i <= hi && i < n; ++i) {
                float x, y, w, h;
                coverRect(row, i, x, y, w, h);
                if (px >= x && px < x + w && py >= y && py < y + h) {
                    float d = fabsf((float)i - row.scroll);
                    if (d < best) { best = d; hoverCover = i; }
                }
            }
            /* scrub bar */
            if (fabsf(py - SCRUB_Y) < ARROW_H * 0.5f + 4) {
                if (fabsf(px - arrowX(-1)) < ARROW_W * 0.5f + 8)      hoverArrow = -1;
                else if (fabsf(px - arrowX(+1)) < ARROW_W * 0.5f + 8) hoverArrow = +1;
            }
            if (fabsf(px - knobX(row)) < KNOB_R + 6 && fabsf(py - SCRUB_Y) < KNOB_R + 6) hoverKnob = true;
            else if (px >= trackLeft() - 4 && px <= trackRight() + 4 && fabsf(py - SCRUB_Y) < 10) hoverTrack = true;
        }
    }

    /* ---- Dragging the knob ---- */
    if (dragging) {
        if (!aHeld || !ir.valid || n == 0) dragging = false;
        else { dragX = ir.x - dragGrab; setFromTrack(row, dragX); }
    }

    /* ---- Arrow buttons held: repeat ---- */
    if (repeatDir) {
        unsigned long long t = nowMs();
        if (!aHeld || hoverArrow != repeatDir) repeatDir = 0;
        else if (t >= repeatNext) { step(row, repeatDir); repeatNext = t + 90; }
    }

    /* ---- D-pad ---- */
    if (n > 0) {
        if (left)  step(row, -1);
        if (right) step(row, +1);
    }

    /* ---- A ---- */
    if (aDown) {
        if (!ready) {
            /* a row that failed to load: A tries again */
            LWP_MutexLock(lock);
            bool failed = row.state == ROW_FAILED;
            if (failed && lazy(row)) row.state = ROW_PENDING;
            LWP_MutexUnlock(lock);
            if (failed && !lazy(row)) { isBuilt = false; return Action::None; }  /* rebuild */
            if (ptr && hoverRowBtn) moveRow(hoverRowBtn);
            return Action::None;
        }
        if (ptr) {
            if (hoverRowBtn)   { moveRow(hoverRowBtn); return Action::None; }
            if (hoverArrow)    { step(row, hoverArrow); repeatDir = hoverArrow; repeatNext = nowMs() + 400; return Action::None; }
            if (hoverKnob)     { dragging = true; dragGrab = ir.x - knobX(row); dragX = knobX(row); return Action::None; }
            if (hoverTrack)    { dragging = true; dragGrab = 0; dragX = ir.x; setFromTrack(row, ir.x); return Action::None; }
            if (hoverCover >= 0) {
                if (hoverCover == row.sel && fabsf(row.scroll - row.sel) < 0.3f) return Action::Open;
                row.sel = hoverCover;
                return Action::None;
            }
        } else if (n > 0) {
            return Action::Open;
        }
    }

    /* ---- Animation ---- */
    rowFade = Ui::approach(rowFade, 1.0f, 0.2f);
    if (ready) {
        /* a long jump (dragging, a click on the track) glides fast */
        float dist = fabsf(row.scroll - (float)row.sel);
        float speed = dragging ? 0.55f : dist > 6.0f ? 0.4f : 0.25f;
        row.scroll = Ui::approach(row.scroll, (float)row.sel, speed);
    }
    return Action::None;
}

/* ---- Drawing ------------------------------------------------------------ */

/* A rounded bar of the top row: card colours, a soft shadow. */
static void pill(float x, float y, float w, float h, bool hot)
{
    const Ui::Palette& p = Ui::pal();
    Ui::shadow(x, y + 2, w, h, h * 0.5f, 5.0f, p.shadow);
    Ui::roundRect(x, y, w, h, h * 0.5f, p.cardTop, p.cardBottom);
    Ui::roundBorder(x, y, w, h, h * 0.5f, hot ? 2.0f : 1.0f, hot ? p.accent : p.cardBorder);
}

/* Solid down (dir +1) or up (-1) triangle, wide and flat. */
static void updown(float cx, float cy, int dir, float g, u32 col)
{
    const float s = (float)dir;
    Ui::triangle(cx - 10 * g, cy - s * 5 * g, cx + 10 * g, cy - s * 5 * g, cx, cy + s * 6 * g, col);
}

/* "WiiFlix" on the red band: white, a dark outline, drawn twice 1 px
 * apart for a bolder stroke */
static void wordmark()
{
    const char* word = "WiiFlix";
    const float wx = Ui::screenLeft() + WORD_X, wy = (Ui::headerBand() - WORD_SIZE) * 0.5f - 1;
    const u32 edge = 0x00000060;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 2; ++dx)
            if (dx || dy) Ui::text(wx + dx, wy + dy, word, WORD_SIZE, edge);
    Ui::text(wx, wy, word, WORD_SIZE, 0xFFFFFFFF);
    Ui::text(wx + 1, wy, word, WORD_SIZE, 0xFFFFFFFF);
}

void CoverLine::drawTop()
{
    const Ui::Palette& p = Ui::pal();
    const Row& row = rows[rowSel];

    /* the row's name, after a dot, in a bar as long as the screen allows */
    const float bx = Ui::screenLeft() + BAR_SIDE, sx = switchX();
    const float bw = sx - SWITCH_GAP - bx, cy = BAR_Y + BAR_H * 0.5f;
    pill(bx, BAR_Y, bw, BAR_H, false);
    Ui::circle(bx + 18, cy, 6.5f, p.text);
    std::string name = fit(row.title, 18, (int)bw - 48);
    Ui::text(bx + 32, BAR_Y + 5, name.c_str(), 18, p.text);

    /* ▼ next row, ▲ the one before: dimmed at the ends; under the pointer
     * the pill grows and turns orange, like every button */
    const bool canDown = rowSel + 1 < (int)rows.size(), canUp = rowSel > 0;
    const bool hot = hoverRowBtn != 0;
    float px = sx, py = BAR_Y, pw = SWITCH_W, ph = BAR_H;
    if (hot) {
        Ui::grow(px, py, pw, ph, Ui::HOVER_GROW);
        Ui::shadow(px - 2, py, pw + 4, ph + 3, ph * 0.5f + 2, 8.0f, 0xF7922E80);
        Ui::roundRect(px, py, pw, ph, ph * 0.5f, Ui::HOVER_TOP, Ui::HOVER_BOTTOM);
        Ui::roundBorder(px, py, pw, ph, ph * 0.5f, 1.5f, Ui::HOVER_EDGE);
    } else {
        pill(px, py, pw, ph, false);
    }
    const u32 on = hot ? 0xFFFFFFFF : p.text, off = hot ? 0xFFFFFF70 : Ui::alpha(p.textDim, 0.35f);
    const float mid = px + pw * 0.5f;   /* the two side by side, close */
    updown(mid - 12, cy, +1, hoverRowBtn == +1 ? 1.15f : 1.0f, canDown ? on : off);
    updown(mid + 12, cy, -1, hoverRowBtn == -1 ? 1.15f : 1.0f, canUp ? on : off);
}

void CoverLine::drawCovers(Row& row)
{
    const Ui::Palette& p = Ui::pal();
    const int n = (int)row.items.size();
    const float fade = rowFade;
    const float dy   = (1.0f - fade) * 24.0f * (float)rowDir;   /* slides in from Up/Down */
    const bool  ptr  = hoverCover >= 0;

    /* how many covers fit on each side */
    int side = (int)((Ui::screenWidth() * 0.5f - BIG_W * 0.5f) / (SMALL_W + GAP)) + 2;
    int c = (int)floorf(row.scroll + 0.5f);

    /* floor shadow under the line */
    Ui::shadow(Ui::screenLeft() + 30, CENTER_Y + BIG_H * 0.5f - 6 + dy, Ui::screenWidth() - 60, 12, 6,
               14.0f, Ui::alpha(p.shadow, 0.5f * fade));

    /* farthest first, so the middle cover is drawn over its neighbours */
    for (int d = side; d >= 0; --d) {
        for (int s = 0; s < 2; ++s) {
            if (d == 0 && s == 1) continue;
            int i = s == 0 ? c - d : c + d;
            if (i < 0 || i >= n) continue;
            float x, y, w, h;
            coverRect(row, i, x, y, w, h);
            if (x > Ui::screenRight() || x + w < Ui::screenLeft()) continue;
            y += dy;

            float dist = fabsf((float)i - row.scroll);
            float k    = dist < 1.0f ? 1.0f - dist : 0.0f;          /* 1 in the middle */
            bool  sel  = i == row.sel;
            bool  hov  = ptr && i == hoverCover && !sel;
            /* side covers a little darker, fading towards the edges */
            float bright = 0.62f + 0.38f * k;
            if (hov) bright = 0.9f;
            float edge = 1.0f - (dist - 2.5f) / (float)side;          /* fade out near the edges */
            if (edge > 1) edge = 1;
            if (edge < 0) edge = 0;
            float a = fade * edge;
            if (a <= 0.01f) continue;
            u8  lv   = (u8)(255 * bright);
            u32 tint = ((u32)lv << 24) | ((u32)lv << 16) | ((u32)lv << 8) | (u32)(255 * a);

            Ui::shadow(x + 2, y + 4, w - 4, h - 2, RADIUS, 6.0f, Ui::alpha(p.shadow, a));

            LWP_MutexLock(lock);
            GRRLIB_texImg* tex = row.tex[i];
            LWP_MutexUnlock(lock);
            if (tex) {
                Ui::texCover(tex, x, y, w, h, RADIUS, w / h, tint);
            } else {
                /* placeholder: the name on a card */
                Ui::roundRect(x, y, w, h, RADIUS, Ui::alpha(Ui::mix(p.cardTop, p.bgBottom, 1 - bright), a),
                              Ui::alpha(Ui::mix(p.cardBottom, p.bgBottom, 1 - bright), a));
                std::string cap = fit(displayName(row.items[i]), 12, (int)w - 12);
                Ui::textCentered(x + w * 0.5f, y + h * 0.5f - 7, cap.c_str(), 12, Ui::alpha(p.textDim, a));
            }

            const JellyfinItem& it = row.items[i];
            if (it.playbackPositionTicks > 0 && it.runtimeTicks > 0)
                Ui::progress(x + 6, y + h - 10, w - 12, 4,
                             (float)it.playbackPositionTicks / (float)it.runtimeTicks);

            /* a thin frame, the same on the selection (no colour) */
            Ui::roundBorder(x, y, w, h, RADIUS, 1.0f, Ui::alpha(p.cardBorder, a * (hov ? 1.0f : 0.6f)));
        }
    }
}

void CoverLine::drawInfo(const Row& row)
{
    const Ui::Palette& p = Ui::pal();
    if (row.sel < 0 || row.sel >= (int)row.items.size()) return;
    /* only once the line has settled on it */
    float settle = 1.0f - fabsf(row.scroll - row.sel) * 2.0f;
    if (settle <= 0.02f) return;
    float a = settle * rowFade;
    const JellyfinItem& it = row.items[row.sel];

    std::string title = fit(displayName(it), 20, (int)Ui::screenWidth() - 80);
    Ui::textCentered(CENTER_X, TITLE_Y, title.c_str(), 20, Ui::alpha(p.text, a));

    /* one line under it: details, stars, the age rating in a box, n/N */
    char meta[200] = "";
    auto add = [&](const char* s) {
        size_t l = strlen(meta);
        snprintf(meta + l, sizeof(meta) - l, "%s%s", l ? "   \xc2\xb7   " : "", s);
    };
    char buf[96];
    if (it.type == "Episode") {
        /* TRANSLATORS: season and episode numbers, then the episode's title */
        snprintf(buf, sizeof(buf), tr("S%d E%d  \xc2\xb7  %s"), it.seasonNumber, it.episodeNumber, it.name.c_str());
        add(buf);
    } else {
        if (it.year > 0) { snprintf(buf, sizeof(buf), "%d", it.year); add(buf); }
        if (it.type == "Series") {
            if (it.childCount > 0) {
                snprintf(buf, sizeof(buf), it.childCount > 1 ? tr("%d seasons") : tr("%d season"), it.childCount);
                add(buf);
            }
        } else if (it.type == "Season") {
            if (it.childCount > 0) {
                snprintf(buf, sizeof(buf), it.childCount > 1 ? tr("%d episodes") : tr("%d episode"), it.childCount);
                add(buf);
            }
        } else {
            int mins = (int)(it.runtimeTicks / 600000000LL);
            if (mins > 0) add(Lang::runTime(mins).c_str());
        }
    }
    if (it.playbackPositionTicks > 0 && it.runtimeTicks > 0) {
        int left = (int)((it.runtimeTicks - it.playbackPositionTicks) / 600000000LL);
        if (left > 0) { snprintf(buf, sizeof(buf), tr("%d min left"), left); add(buf); }
    }

    /* the pieces' widths first, to centre the line */
    const int   TS = 15;                       /* text size              */
    const float SEP = 18.0f, STAR = 15.0f, BADGE_PAD = 6.0f;
    const bool  stars = it.communityRating > 0.0f;
    const std::string rating = it.officialRating;
    char pos[32];
    snprintf(pos, sizeof(pos), "%d/%d", row.sel + 1, (int)row.items.size());
    std::string m = fit(meta, TS, (int)Ui::screenWidth() - 300);

    float w = 0;
    int   parts = 0;
    auto  span = [&](float pw) { w += (parts++ ? SEP : 0) + pw; };
    if (!m.empty())      span((float)Ui::textWidth(m.c_str(), TS));
    if (stars)           span(5 * STAR + 4 * STAR * 0.5f * 0.35f);
    if (!rating.empty()) span(Ui::textWidth(rating.c_str(), 13) + BADGE_PAD * 2);
    span((float)Ui::textWidth(pos, 14));

    float x = CENTER_X - w * 0.5f;
    parts = 0;
    auto next = [&](float pw) { float at = x + (parts++ ? SEP : 0); x = at + pw; return at; };
    const float midY = META_Y + 9;
    if (!m.empty()) {
        float at = next((float)Ui::textWidth(m.c_str(), TS));
        Ui::text(at, META_Y, m.c_str(), TS, Ui::alpha(p.text, a));
    }
    if (stars) {
        float at = next(5 * STAR + 4 * STAR * 0.5f * 0.35f);
        Ui::stars(at, midY, STAR * 0.5f, it.communityRating, Ui::alpha(p.accent, a),
                  Ui::alpha(p.textDim, 0.3f * a));
    }
    if (!rating.empty()) {
        float bw = Ui::textWidth(rating.c_str(), 13) + BADGE_PAD * 2;
        float at = next(bw);
        Ui::roundBorder(at, midY - 10, bw, 20, 3, 1.5f, Ui::alpha(p.text, a));
        Ui::text(at + BADGE_PAD, midY - 8, rating.c_str(), 13, Ui::alpha(p.text, a));
    }
    Ui::text(next((float)Ui::textWidth(pos, 14)), META_Y + 1, pos, 14, Ui::alpha(p.textDim, a));
}

void CoverLine::drawScrubBar(const Row& row)
{
    const Ui::Palette& p = Ui::pal();
    const int   n  = (int)row.items.size();
    const float tl = trackLeft(), tr_ = trackRight();
    /* the knob sits under the pointer while dragged, else on the selection */
    float kx = knobX(row);
    if (dragging) kx = dragX < tl ? tl : dragX > tr_ ? tr_ : dragX;

    /* end arrows: Wii Menu arrowheads (a notch at the back), silver */
    Ui::arrowHead(arrowX(-1), SCRUB_Y, -1, ARROW_W, ARROW_H, row.sel > 0,     hoverArrow == -1);
    Ui::arrowHead(arrowX(+1), SCRUB_Y, +1, ARROW_W, ARROW_H, row.sel < n - 1, hoverArrow == +1);

    /* a thin track */
    const float th = hoverTrack || dragging ? 5.0f : 3.0f;
    Ui::roundRect(tl, SCRUB_Y - th * 0.5f, tr_ - tl, th, th * 0.5f, p.fieldBorder);

    /* white knob */
    bool hot = hoverKnob || dragging;
    float r = hot ? KNOB_R + 2 : KNOB_R;
    if (hot) Ui::circle(kx, SCRUB_Y, r + 6, Ui::alpha(p.glow, 0.6f));
    Ui::shadow(kx - r, SCRUB_Y - r + 2, r * 2, r * 2, r, 4.0f, p.shadow);
    Ui::circle(kx, SCRUB_Y, r, 0xFFFFFFFF);
    Ui::roundBorder(kx - r, SCRUB_Y - r, r * 2, r * 2, r, 1.5f, hot ? p.accent : 0xA0A6B0FF);

    /* while dragging: the title the knob is on, in a bubble above it */
    if (dragging && row.sel >= 0 && row.sel < n) {
        std::string s = fit(displayName(row.items[row.sel]), 14, 220);
        float w = Ui::textWidth(s.c_str(), 14) + 20, h = 24;
        float bx = kx - w * 0.5f, by = SCRUB_Y - r - 14 - h;
        if (bx < Ui::screenLeft() + 8) bx = Ui::screenLeft() + 8;
        if (bx + w > Ui::screenRight() - 8) bx = Ui::screenRight() - 8 - w;
        Ui::shadow(bx, by + 2, w, h, 8, 5.0f, p.shadow);
        Ui::roundRect(bx, by, w, h, 8, p.cardTop, p.cardBottom);
        Ui::roundBorder(bx, by, w, h, 8, 1.5f, p.accent);
        Ui::triangle(kx - 6, by + h - 1, kx + 6, by + h - 1, kx, by + h + 6, p.cardBottom);
        Ui::text(bx + 10, by + 4, s.c_str(), 14, p.text);
    }
}

void CoverLine::drawHints()
{
    const Ui::Hint h[] = { { "A", N_("Open") }, { "1", N_("Search") }, { "+!", N_("Browse") }, { "2", N_("Profiles") } };
    const int nh = 4;
    float w = 0;
    for (int i = 0; i < nh; ++i) w += Ui::hintWidth(h[i]);
    float x = CENTER_X - w * 0.5f;
    for (int i = 0; i < nh; ++i) x += Ui::hint(x, HINTS_Y, h[i]);
}

void CoverLine::render(const ir_t& ir)
{
    (void)ir;
    const Ui::Palette& p = Ui::pal();
    Ui::background();                 /* with the red band */
    wordmark();

    if (rows.empty()) {
        Ui::textCentered(CENTER_X, 200, tr("Nothing to show yet"), 20, p.text);
        drawHints();
        return;
    }

    drawTop();

    Row& row = rows[rowSel];
    LWP_MutexLock(lock);
    unsigned char state = row.state;
    LWP_MutexUnlock(lock);

    if (state == ROW_READY && !row.items.empty()) {
        drawCovers(row);
        drawInfo(row);
        drawScrubBar(row);
    } else if (state == ROW_READY) {
        if (row.queue) {
            Ui::textCentered(CENTER_X, CENTER_Y - 20, tr("Your queue is empty"), 20, p.text);
            Ui::textCentered(CENTER_X, CENTER_Y + 10, tr("Add titles with \"Add to Queue\" on their page."), 14, p.textDim);
        } else if (!lazy(row)) {
            Ui::textCentered(CENTER_X, CENTER_Y - 20, tr("Nothing to continue"), 20, p.text);
            Ui::textCentered(CENTER_X, CENTER_Y + 10, tr("Press Down to browse by genre."), 14, p.textDim);
        } else {
            Ui::textCentered(CENTER_X, CENTER_Y - 10, tr("No titles in this genre"), 18, p.textDim);
        }
    } else if (state == ROW_FAILED) {
        Ui::textCentered(CENTER_X, CENTER_Y - 20, tr("This row could not be loaded"), 18, p.text);
        Ui::textCentered(CENTER_X, CENTER_Y + 10, tr("Press A to try again."), 14, p.textDim);
    } else {
        /* loading: the five blocks */
        Ui::spinner(nullptr, CENTER_X, CENTER_Y);
        Ui::textCentered(CENTER_X, CENTER_Y + 24, tr("Loading..."), 14, p.textDim);
    }

    drawHints();
}
