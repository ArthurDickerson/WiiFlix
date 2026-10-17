#pragma once
#include <string>
#include <vector>
#include <wiiuse/wpad.h>

/* -----------------------------------------------------------------------
 * TrackList — the list a track is picked from: audio, subtitles, version.
 * The same on the details page and in the player (1 / 2).
 *
 * A panel centred on the screen over a dimmed page: its title and a ✕ at
 * the top, then rectangular rows (ROWS at most), the one in use with a ✓.
 * A longer list gets a scroll bar at the right like the home page's: a
 * thin track with a white knob, orange and grown under the pointer, that
 * can be dragged (A held), or a click on the track to jump there.  One choice, orange and grown: the pointer takes it
 * when it moves onto another row, Up / Down move it too, A takes it.  B,
 * the ✕, or A off the list (unless the D-pad chose last) close it.
 * ----------------------------------------------------------------------- */
class TrackList {
public:
    enum class Result { None, Picked, Closed };

    /* labels: the rows; current: the one in use (-1 none), chosen first */
    void open(const std::string& title, const std::vector<std::string>& labels, int current);
    void close() { shown = false; }
    bool isOpen() const { return shown; }

    /* Once a frame while open.  Picked: chosen() is the row taken; the
     * list is closed either way but None. */
    Result update(const ir_t& ir, bool up, bool down, bool a, bool b);
    int    chosen() const { return sel; }
    void   draw() const;

private:
    bool shown = false;
    std::string title;
    std::vector<std::string> labels;
    int  current = -1, sel = 0, top = 0;
    int  hover = -1;          /* the row under the pointer last frame */
    bool dpad = false;        /* the D-pad moved the choice last */
    bool closeHot = false;    /* the pointer on the ✕ */
    bool knobHot = false, trackHot = false;   /* the pointer on the scroll bar */
    bool dragging = false;    /* the knob held */
    float dragGrab = 0.0f;    /* pointer y minus knob y at the grab */

    int  shownRows() const;
    void box(float& x, float& y, float& w, float& h) const;
    bool  scrolls() const { return (int)labels.size() > shownRows(); }
    /* the scroll bar's track, top to bottom, and its knob's centre */
    void  trackSpan(float& tx, float& t0, float& t1) const;
    float knobY() const;
    void  scrollTo(float knobCentre);   /* top from a knob position */
};
