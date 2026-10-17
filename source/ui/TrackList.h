#pragma once
#include <string>
#include <vector>
#include <wiiuse/wpad.h>

/* -----------------------------------------------------------------------
 * TrackList — the list a track is picked from: audio, subtitles, version.
 * The same on the details page and in the player (1 / 2).
 *
 * A panel centred on the screen over a dimmed page: its title and a ✕ at
 * the top, then rectangular rows (ROWS at most, a scrollbar for more), the
 * one in use with a ✓.  One choice, orange and grown: the pointer takes it
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

    int  shownRows() const;
    void box(float& x, float& y, float& w, float& h) const;
};
