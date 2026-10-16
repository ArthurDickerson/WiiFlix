#pragma once
#include <grrlib.h>
#include <wiiuse/wpad.h>
#include <ogc/lwp.h>
#include <ogc/mutex.h>
#include <string>
#include <vector>
#include "../jellyfin/JellyfinClient.h"

/* -----------------------------------------------------------------------
 * CoverLine — WiiFlix's home screen.
 *
 * One row of cover art at a time, as a line from the left: the cover in
 * focus (under the pointer, or the D-pad's) grows in place, the ones after
 * it make room; the line scrolls only when the D-pad takes the focus past
 * an edge.  Above it, the row's name in a long bar (A on it: the list of
 * rows, 8 a page) and a ▼▲ pill; under it the scrub bar, then the title in
 * focus and its details.
 *
 *   Left / Right     previous / next cover (repeats while held)
 *   Up / Down        previous / next row (▲ / ▼ on screen)
 *   A                open the cover in focus (pointer: the one under it)
 *   -                the list of rows
 *   1                search        +   browse (libraries, A-Z)
 *   2                profiles
 *
 * Rows: "Continue Watching" first (the default view), "Instant Queue" (the
 * titles favourited in Jellyfin: "Add to Queue"), then one row per genre
 * of the server's movies and TV shows.  Only Continue Watching and
 * the list of genres are fetched up front; a genre row's titles are
 * fetched by the background thread when the row is first shown (and the
 * rows next to it, ahead of time).
 *
 * Along the bottom, a scrub bar shows where the selection is in the row.
 * Its knob can be dragged with the Wii Remote pointer (hold A) to move
 * through a long row quickly, the track can be clicked to jump, and the
 * round arrow buttons at both ends step one cover (held: repeat).
 *
 * Covers are fetched by the same background thread, nearest to the
 * selection first, and freed again once they are far from it (memory:
 * MPlayer needs room when playback starts).  The thread uses the shared
 * JellyfinClient connection: stopLoader() must be called before the owner
 * makes any other request (LibraryView does that whenever it leaves home).
 * ----------------------------------------------------------------------- */
class CoverLine {
public:
    enum class Action { None, Open, Search, Browse, Profiles };

    CoverLine(JellyfinClient& client, const std::string& serverUrl, const JellyfinAuth& auth);
    ~CoverLine();

    /* Continue Watching and the genre list (blocking network: run it inside
     * a loading screen).  libs is unused for now: genres span the server. */
    void build(const std::vector<JellyfinLibrary>& libs);
    bool built() const { return isBuilt; }
    /* Forget everything so the next visit rebuilds (after playback, a
     * favourite or watched flag changed...). */
    void invalidate();

    void startLoader();
    void stopLoader();
    bool loaderRunning() const { return loaderThread != LWP_THREAD_NULL; }

    Action update(const ir_t& ir, bool& irMode);
    void   render(const ir_t& ir);

    /* Item under the selection (valid after Action::Open). */
    const JellyfinItem* selectedItem() const;
    /* Changes whenever the highlight moves (LibraryView's sounds). */
    unsigned focusKey() const;
    void setUserName(const std::string& n) { userName = n; }

private:
    enum RowState  : unsigned char { ROW_PENDING, ROW_LOADING, ROW_READY, ROW_FAILED };
    enum TileState : unsigned char { TILE_NONE, TILE_LOADING, TILE_READY, TILE_FAILED };

    struct Row {
        std::string                 title;
        std::string                 genreId;      /* a genre's row               */
        bool                        queue = false;   /* Instant Queue: favourites */
        std::vector<JellyfinItem>   items;        /* touched only when READY    */
        std::vector<GRRLIB_texImg*> tex;
        std::vector<unsigned char>  st;           /* TileState                  */
        unsigned char               state = ROW_PENDING;   /* under `lock` */
        int                         sel    = 0;     /* the cover in focus (big) */
        int                         first  = 0;     /* the first one shown      */
        float                       scroll = 0.0f;  /* animated, follows first  */
        float                       fpos   = 0.0f;  /* animated, follows sel    */
    };

    JellyfinClient&  client;
    std::string      serverUrl;
    JellyfinAuth     auth;
    std::string      userName;

    std::vector<Row> rows;
    bool             isBuilt = false;
    int              rowSel  = 0;
    float            rowFade = 1.0f;      /* 0..1, fades a row in after Up/Down */
    int              rowDir  = 0;         /* -1 came from below, +1 from above  */
    /* the title to select once its row is fetched (back from playback) */
    std::string      pendingSelectId;

    /* pointer state */
    int   hoverCover  = -1;
    int   hoverArrow  = 0;                /* scrub bar arrows: -1 left, +1 right */
    int   hoverRowBtn = 0;                /* row arrows: -1 up, +1 down          */
    bool  hoverKnob   = false;
    bool  hoverLabel  = false;            /* the row's name: A opens the list */
    bool  hoverTrack  = false;
    bool  dragging    = false;
    float dragGrab    = 0.0f;             /* pointer x minus knob x at the grab   */
    float dragX       = 0.0f;             /* where the knob is held (drawn there) */
    int   repeatDir   = 0;                /* arrow button held                   */
    unsigned long long repeatNext = 0;

    lwp_t            loaderThread = LWP_THREAD_NULL;
    mutex_t          lock         = LWP_MUTEX_NULL;
    volatile bool    stopReq      = false;

    /* loader thread */
    static void* loaderMain(void* self);
    void loaderLoop();
    bool fetchRowIfNeeded();
    bool pickCover(int& r, int& i);
    void fetchRow(int r);

    /* main thread */
    void freeTextures(Row& r);
    /* fetched by the loader when first shown (Continue Watching: at once) */
    static bool lazy(const Row& r) { return r.queue || !r.genreId.empty(); }
    void evictFar();
    bool rowReady(int r) const;
    void moveRow(int delta);
    void step(Row& row, int delta);
    void setFromTrack(Row& row, float x);
    /* covers that fit across the screen (one of them grown), and first
     * moved only as far as it takes to show sel (the D-pad, the scrub bar) */
    int  visibleCount() const;
    void keepVisible(Row& r) const;

    float trackLeft() const;
    float trackRight() const;
    float knobX(const Row& row) const;
    void  coverRect(const Row& row, int i, float& x, float& y, float& w, float& h) const;

    void drawTop();

    /* the list of rows over the line (CoverLine.cpp: "The list of rows") */
    bool  menuOpen  = false;
    int   menuPage  = 0, menuSel = 0;
    int   menuHover = -1;                 /* the row under the pointer   */
    int   menuArrow = 0;                  /* the page pill's ◀ -1 / ▶ +1 */
    bool  menuClose = false;              /* the ✕ under the pointer     */
    bool  menuPtr   = false;              /* the pointer drives it       */
    int    menuPages() const;
    void   openMenu();
    void   jumpRow(int to);
    Action updateMenu(const ir_t& ir);
    void   drawMenu();
    void drawCovers(Row& row);
    void drawInfo(const Row& row);
    void drawScrubBar(const Row& row);
};
