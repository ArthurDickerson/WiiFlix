#include "PlayerView.h"
#include "../core/Text.h"
#include "../core/Lang.h"
#include "../ui/Ui.h"
#include "WiiPlayer.h"
#include "VideoSurface.h"
#include "Trickplay.h"
#include "Subtitles.h"
#include "../input/Input.h"

#include <ogc/lwp_watchdog.h>
#include <math.h>
#include <stdio.h>

/* ---- Layout (640x480 virtual screen) ----------------------------------
 * The old video channels' playback screen: over the picture, a track
 * between two arrowheads with an orange knob on it, the time played and
 * the running time in pills under it with pause between them, and a round
 * back button top right. */
static const int   TOP_H    = 52;                 /* title area (toasts go under) */
static const float BAR_Y    = 344;                /* the track's centre line      */
static const float ARROW_X  = 36, ARROW_W = 36, ARROW_H = 28;   /* the old channels' arrowheads */
static const float KNOB_R   = 15;
static const float ROW_Y    = 396;                /* pills, pause...: centre line */
static const float PILL_W   = 104, PILL_H = 30, PILL_DX = 130;
static const float SIDE_DX  = 50;                 /* |<< and >>| beside pause     */
static const float BACK_R   = 30, BACK_CY = 46;   /* as the details page's */
/* hoverButton: 0 |<<, 1 back 10 s (left arrow), 2 pause, 3 on 10 s (right
 * arrow), 4 >>|, then zoom and the back button */
static const int   BTN_ZOOM = 5, BTN_EXIT = 6;
static float arrowX(int dir) { return dir < 0 ? Ui::screenLeft() + ARROW_X : Ui::screenRight() - ARROW_X; }
static float trackL() { return arrowX(-1) + ARROW_W * 0.5f + 20; }
static float trackR() { return arrowX(+1) - ARROW_W * 0.5f - 20; }
static float zoomCx() { return Ui::screenRight() - 48; }
static float backCx() { return Ui::screenRight() - 59; }
static const int INTRO_X    = 466, INTRO_Y = 270, INTRO_W = 150, INTRO_H = 40;
static const int LIST_X     = 150, LIST_W  = 340;
static const int LIST_Y     = 112, ROW_H   = 34;
static const int LIST_ROWS  = 7;

/* MPlayer's cache (cache2.c): % ahead, -1 once the whole stream is in.
 * Playback starts at -cache-min (WiiPlayer.cpp: 8). */
extern "C" float cache_fill_status;
static const float CACHE_START = 8.0f;

static u64 nowMs() { return ticks_to_millisecs(gettime()); }

static std::string fmtTime(float secs)
{
    if (secs < 0.0f) secs = 0.0f;
    int s = (int)secs, h = s / 3600, m = (s % 3600) / 60;
    char buf[16];
    if (h > 0) snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s % 60);
    else       snprintf(buf, sizeof(buf), "%d:%02d", m, s % 60);
    return buf;
}

static bool inRect(const ir_t& ir, int x, int y, int w, int h)
{
    return ir.valid && ir.x >= x && ir.x < x + w && ir.y >= y && ir.y < y + h;
}

static bool nearBox(const ir_t& ir, float cx, float cy, float hw, float hh)
{
    return ir.valid && fabsf(ir.x - cx) < hw && fabsf(ir.y - cy) < hh;
}

/* A time as the pills show it: 1h 38m */
static std::string fmtHm(float secs)
{
    if (secs < 0.0f) secs = 0.0f;
    int m = (int)secs / 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%dh %02dm", m / 60, m % 60);
    return buf;
}

static std::string streamLabel(const MediaStream& s, int n)
{
    if (!s.displayTitle.empty()) return s.displayTitle;
    if (!s.language.empty() && !s.codec.empty()) return s.language + " (" + s.codec + ")";
    if (!s.language.empty()) return s.language;
    if (!s.codec.empty())    return s.codec;
    char buf[48];
    snprintf(buf, sizeof(buf), tr("Track %d"), n);
    return buf;
}

/* ------------------------------------------------------------------------ */

PlayerView::PlayerView(GRRLIB_ttfFont* f, GRRLIB_texImg* cursor, GRRLIB_texImg* ring)
    : font(f), cursorTex(cursor), ringTex(ring)
{
    showControls();
}

void PlayerView::setContext(const PlayerViewContext& c)
{
    ctx          = c;
    pickAudio    = c.currentAudio;
    pickSub      = c.currentSub;
    skippedSegments = 0;
    seekPending  = false;
    seekInFlight = false;
    seekInPlaceSince = false;
    lastSeenPos  = -1.0f;
}

bool PlayerView::buffering() const
{
    return lastSeenPos > 0.0f && !g_mplayer_paused && !g_wiifin_loading_active &&
           nowMs() - lastPosChangeMs > 1000;
}

float PlayerView::position() const
{
    float t    = wii_player_time_pos();
    float skip = wii_player_ss_missed() ? 0.0f : ctx.startSkip;
    return ctx.streamOrigin + (t > skip ? t : skip);
}

float PlayerView::duration() const
{
    if (ctx.runtime > 0.0f) return ctx.runtime;
    if (g_mplayer_duration > 0.0f) return ctx.streamOrigin + g_mplayer_duration;
    return 0.0f;
}

int PlayerView::activeSegment() const
{
    if (!busyMsg.empty()) return -1;
    float pos = position();
    const auto& segs = ctx.intro.segments;
    for (int i = 0; i < (int)segs.size() && i < 32; ++i) {
        if (skippedSegments & (1u << i)) continue;
        /* not in its last second: skipping would land about there anyway */
        if (pos >= segs[i].start && pos < segs[i].end - 1.0f) return i;
    }
    return -1;
}

/* Credits with an episode after them: straight to it, as Netflix does */
const char* PlayerView::segmentLabel(int i) const
{
    switch (ctx.intro.segments[i].kind) {
    case MediaSegment::Recap:      return N_("Skip recap");
    case MediaSegment::Preview:    return N_("Skip preview");
    case MediaSegment::Commercial: return N_("Skip ad");
    case MediaSegment::Outro:
        return ctx.episodeIdx + 1 < (int)ctx.episodes.size() ? N_("Next episode") : N_("Skip credits");
    default:                       return N_("Skip intro");
    }
}

bool PlayerView::controlsVisible() const
{
    /* A pause held for rebuffering is not the user's: keep the picture clean */
    return panel != Panel::None || (g_mplayer_paused && rebufPercent < 0) ||
           nowMs() < controlsUntil;
}

void PlayerView::showControls(int ms)
{
    controlsUntil = nowMs() + ms;
}

/* MPlayer cannot seek in Jellyfin's live transcodes (the TS timestamps
 * don't start at 0 and the stream isn't seekable), so every seek restarts
 * the transcode at the target.  Repeated presses are merged into one. */
void PlayerView::nudgeSeek(float delta)
{
    float base = seekPending ? seekPendingTo : position();
    float target = base + delta;
    float dur = duration();
    if (target < 0.0f) target = 0.0f;
    if (dur > 0.0f && target > dur - 5.0f) target = dur - 5.0f;
    seekPending   = true;
    seekPendingTo = target;
    seekCommitAt  = nowMs() + 400;   /* presses closer than this add up */
    showControls();
}

/* |< and >| with chapters: the next one's start, or this one's (the one
 * before when this one has just started); false past the last or before
 * the first: then they go to the next or previous episode */
bool PlayerView::chapterJump(int dir)
{
    if (ctx.chapters.size() < 2) return false;
    const float pos = seekPending ? seekPendingTo : position();
    float to = -1.0f;
    if (dir > 0) {
        for (float c : ctx.chapters) if (c > pos + 1.0f) { to = c; break; }
    } else {
        int cur = -1;
        for (int i = 0; i < (int)ctx.chapters.size(); ++i) if (ctx.chapters[i] <= pos + 0.5f) cur = i;
        if (cur < 0) return false;
        if (pos - ctx.chapters[cur] > 3.0f) to = ctx.chapters[cur];
        else if (cur > 0)                   to = ctx.chapters[cur - 1];
        else if (pos > 1.0f)                to = 0.0f;
    }
    if (to < 0.0f) return false;
    SYS_Report("[Player] chapter %s: to %.1f s\n", dir > 0 ? "next" : "back", (double)to);
    seekPending  = false;
    seekInFlight = true;
    seekTo       = to;
    showControls();
    return true;
}

void PlayerView::remoteSeek(float secs)
{
    nudgeSeek(secs - (seekPending ? seekPendingTo : position()));
}

void PlayerView::remoteTrack(bool audio, int index)
{
    if (audio) pickAudio = index;
    else       pickSub   = index;
    injected = audio ? Action::Audio : Action::Sub;
}

float PlayerView::displayPosition() const
{
    if (seekPending)  return seekPendingTo;
    if (seekInFlight) return seekTo;
    return position();
}

void PlayerView::seekInPlace()
{
    seekInPlaceSince = true;
    seekInPlaceFrom  = wii_player_time_pos();
    seekInPlaceAt    = nowMs();
}

/* The current cue, lines wrapped to the screen, white with a black outline
 * (readable on any picture), its last line ending at y = bottom. */
void PlayerView::drawSubtitle(float bottom)
{
    const Subtitles::Cue* cue = subtitles->at(position());
    if (!cue) return;
    const int SIZE = 24, LINE_H = 29;
    const float maxW = Ui::screenWidth() - 60;
    std::vector<std::string> lines;
    size_t p = 0;
    while (p <= cue->text.size()) {
        size_t e = cue->text.find('\n', p);
        std::string para = cue->text.substr(p, e == std::string::npos ? std::string::npos : e - p);
        p = e == std::string::npos ? cue->text.size() + 1 : e + 1;
        /* word wrap; a word too long for a line (Japanese and Chinese have
         * no spaces) is cut between its characters */
        std::string cur;
        size_t w = 0;
        while (w < para.size()) {
            size_t sp = para.find(' ', w);
            std::string word = para.substr(w, sp == std::string::npos ? std::string::npos : sp - w);
            w = sp == std::string::npos ? para.size() : sp + 1;
            std::string trial = cur.empty() ? word : cur + " " + word;
            if (Ui::textWidth(trial.c_str(), SIZE) <= maxW) { cur = trial; continue; }
            if (!cur.empty()) { lines.push_back(cur); cur.clear(); }
            while (Ui::textWidth(word.c_str(), SIZE) > maxW) {
                size_t n = Text::fitBytes(Ui::font(), word.c_str(), SIZE, maxW);
                lines.push_back(word.substr(0, n));
                word.erase(0, n);
            }
            cur = word;
        }
        if (!cur.empty()) lines.push_back(cur);
    }
    if (lines.size() > 4) lines.erase(lines.begin(), lines.end() - 4);
    float y = bottom - lines.size() * LINE_H;
    const float cx = (Ui::screenLeft() + Ui::screenRight()) * 0.5f;
    static const float OFF[8][2] = { {-2,0},{2,0},{0,-2},{0,2},{-1.5f,-1.5f},{1.5f,-1.5f},{-1.5f,1.5f},{1.5f,1.5f} };
    for (const std::string& l : lines) {
        for (const auto& o : OFF) Ui::textCentered(cx + o[0], y + o[1], l.c_str(), SIZE, 0x000000E0);
        Ui::textCentered(cx, y, l.c_str(), SIZE, 0xFFFFFFFF);
        y += LINE_H;
    }
}

void PlayerView::toast(const std::string& msg, int ms)
{
    toastMsg   = msg;
    toastUntil = nowMs() + ms;
}

void PlayerView::openPanel(Panel p)
{
    panel    = p;
    panelSel = 0;
    for (int i = 0; i < panelCount(); ++i)
        if (panelRowIsCurrent(i)) { panelSel = i; break; }
    panelTop = panelSel >= LIST_ROWS ? panelSel - LIST_ROWS + 1 : 0;
    prevHoverRow = -2;   /* first hover after opening counts as a move */
}

int PlayerView::panelCount() const
{
    if (panel == Panel::Audio) return (int)ctx.audioStreams.size();
    if (panel == Panel::Sub)   return (int)ctx.subStreams.size() + 1;   /* + "Off" */
    return 0;
}

std::string PlayerView::panelLabel(int row) const
{
    if (panel == Panel::Audio) return streamLabel(ctx.audioStreams[row], row + 1);
    if (row == 0) return tr("Off");
    return streamLabel(ctx.subStreams[row - 1], row);
}

bool PlayerView::panelRowIsCurrent(int row) const
{
    if (panel == Panel::Audio) return ctx.audioStreams[row].index == ctx.currentAudio;
    if (row == 0) return ctx.currentSub < 0;
    return ctx.subStreams[row - 1].index == ctx.currentSub;
}

PlayerView::Action PlayerView::activatePanelRow(int row)
{
    if (row < 0 || row >= panelCount()) return Action::None;
    bool current = panelRowIsCurrent(row);
    Panel p = panel;
    panel = Panel::None;
    showControls();
    if (current) return Action::None;
    if (p == Panel::Audio) {
        pickAudio = ctx.audioStreams[row].index;
        return Action::Audio;
    }
    pickSub = (row == 0) ? -1 : ctx.subStreams[row - 1].index;
    return Action::Sub;
}

void PlayerView::hitTest(const ir_t& ir)
{
    hoverButton = -1;
    hoverBar    = false;
    hoverRow    = -1;
    hoverIntro  = introVisible() && inRect(ir, INTRO_X, INTRO_Y, INTRO_W, INTRO_H);

    if (panel != Panel::None) {
        int rows = panelCount() - panelTop;
        if (rows > LIST_ROWS) rows = LIST_ROWS;
        for (int i = 0; i < rows; ++i)
            if (inRect(ir, LIST_X, LIST_Y + 44 + i * ROW_H, LIST_W, ROW_H))
                hoverRow = panelTop + i;
        return;
    }
    /* still on screen while they fade out: still clickable */
    if (!controlsVisible() && controlsAlpha < 0.3f) return;
    const bool chapters = ctx.chapters.size() >= 2;
    const bool hasNext  = ctx.episodeIdx + 1 < (int)ctx.episodes.size() || chapters;
    const bool hasPrev  = (!ctx.episodes.empty() && ctx.episodeIdx > 0) || chapters;
    hoverBar = ir.valid && ir.x >= trackL() - 6 && ir.x <= trackR() + 6 && fabsf(ir.y - BAR_Y) < 14;
    if (hasPrev && nearBox(ir, 320 - SIDE_DX, ROW_Y, 18, 20))                    hoverButton = 0;
    if (nearBox(ir, arrowX(-1), BAR_Y, ARROW_W * 0.5f + 8, ARROW_H * 0.5f + 4))  hoverButton = 1;
    if (nearBox(ir, 320, ROW_Y, 24, 24))                                         hoverButton = 2;
    if (nearBox(ir, arrowX(+1), BAR_Y, ARROW_W * 0.5f + 8, ARROW_H * 0.5f + 4))  hoverButton = 3;
    if (hasNext && nearBox(ir, 320 + SIDE_DX, ROW_Y, 18, 20))                    hoverButton = 4;
    if (nearBox(ir, zoomCx(), ROW_Y, 22, 20))                                    hoverButton = BTN_ZOOM;
    if (ir.valid) {
        const float dx = ir.x - backCx(), dy = ir.y - BACK_CY;
        if (dx * dx + dy * dy < (BACK_R + 4) * (BACK_R + 4)) hoverButton = BTN_EXIT;
    }
}

void PlayerView::toggleZoom()
{
    bool fill = VideoSurface::zoom() != VideoSurface::Zoom::Fill;
    VideoSurface::setZoom(fill ? VideoSurface::Zoom::Fill : VideoSurface::Zoom::Fit);
    toast(fill ? tr("Zoom: fill the screen") : tr("Zoom: whole picture"), 1500);
}

/* ---- Input ----------------------------------------------------------- */

PlayerView::Action PlayerView::update(u32 down, const ir_t& ir)
{
    /* a seek in this stream is over once MPlayer shows a picture from
     * elsewhere: until then it may still show one or two from here */
    if (seekInPlaceSince && !g_wiifin_loading_active &&
        (fabsf(wii_player_time_pos() - seekInPlaceFrom) > 1.0f || nowMs() - seekInPlaceAt > 4000)) {
        seekInPlaceSince = false;
        seekInFlight     = false;
    }
    {
        float t = wii_player_time_pos();
        if (t != lastSeenPos || g_mplayer_paused || g_wiifin_loading_active) {
            lastSeenPos     = t;
            lastPosChangeMs = nowMs();
        }
    }

    /* Pointer movement reveals the controls.  A hand holding the remote
     * shakes the pointer a few pixels all the time: while they are hidden,
     * only a real move brings them back (24 px from where the pointer was
     * half a second ago); once they show, any movement keeps them up. */
    if (ir.valid) {
        const u64 now = nowMs();
        if (lastIrX < 0.0f || now - irAnchorMs > 500) { lastIrX = ir.x; lastIrY = ir.y; irAnchorMs = now; }
        float dx = ir.x - lastIrX, dy = ir.y - lastIrY;
        float need = controlsVisible() ? 4.0f : 24.0f;
        if (dx * dx + dy * dy > need * need) {
            showControls();
            lastIrX = ir.x; lastIrY = ir.y; irAnchorMs = now;
        }
    } else {
        lastIrX = lastIrY = -1.0f;
    }

    hitTest(ir);

    /* the video's first picture: the controls, up since the loading
     * started, go a second after it (not 5 s after the start) */
    if (!firstPicture && !g_wiifin_loading_active && wii_player_time_pos() > 0.0f) {
        firstPicture = true;
        if (controlsUntil > nowMs() + 1000) controlsUntil = nowMs() + 1000;
    }

    if (down & WPAD_BUTTON_HOME) return Action::Home;
    if (!busyMsg.empty()) return Action::None;   /* switching streams */
    if (injected != Action::None) { Action a = injected; injected = Action::None; return a; }

    const bool hasNext = ctx.episodeIdx + 1 < (int)ctx.episodes.size();
    const bool hasPrev = !ctx.episodes.empty() && ctx.episodeIdx > 0;

    if (seekPending && nowMs() >= seekCommitAt) {
        seekPending  = false;
        seekInFlight = true;
        seekTo = seekPendingTo;
        return Action::SeekTo;
    }

    /* ---- Track picker ---- */
    if (panel != Panel::None) {
        int n = panelCount();
        if (hoverRow >= 0 && hoverRow != prevHoverRow) panelSel = hoverRow;
        prevHoverRow = hoverRow;
        if (down & WPAD_BUTTON_B) { panel = Panel::None; showControls(); return Action::None; }
        if ((down & WPAD_BUTTON_UP)   && panelSel > 0)     --panelSel;
        if ((down & WPAD_BUTTON_DOWN) && panelSel + 1 < n) ++panelSel;
        if (panelSel < panelTop) panelTop = panelSel;
        if (panelSel >= panelTop + LIST_ROWS) panelTop = panelSel - LIST_ROWS + 1;
        if (down & WPAD_BUTTON_A) return activatePanelRow(panelSel);
        return Action::None;
    }

    const bool visible = controlsVisible() || controlsAlpha >= 0.3f;   /* fading out: still there */
    Action act = Action::None;

    if (down & WPAD_BUTTON_A) {
        /* the pointer skips with the button only (a click on the bar meant
         * the bar, even as the controls faded); without one, A anywhere */
        if (hoverIntro || (!ir.valid && introVisible() && hoverButton < 0 && !hoverBar)) {
            int i = activeSegment();
            const MediaSegment& seg = ctx.intro.segments[i];
            skippedSegments |= 1u << i;
            if (seg.kind == MediaSegment::Outro && hasNext) {
                act = Action::Next;
            } else {
                float to = seg.end + 0.3f, dur = duration();
                if (dur > 0.0f && to > dur - 2.0f) to = dur - 2.0f;   /* credits at the very end */
                SYS_Report("[Segments] %s: to %.1f s\n", segmentLabel(i), (double)to);
                seekInFlight = true;
                seekTo = to;
                act = Action::SeekTo;
            }
        } else if (visible && hoverBar && duration() > 0.0f) {
            float frac = (ir.x - trackL()) / (trackR() - trackL());
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            seekInFlight = true;
            seekTo = frac * duration();
            act = Action::SeekTo;
        } else if (visible && hoverButton >= 0) {
            switch (hoverButton) {
            case 0:
                if (chapterJump(-1)) { act = Action::SeekTo; break; }
                if (hasPrev) act = Action::Prev; else toast(tr("No previous episode"));
                break;
            case 1: nudgeSeek(-10.0f); break;
            case 2: wii_player_pause_toggle(); break;
            case 3: nudgeSeek(+10.0f); break;
            case 4:
                if (chapterJump(+1)) { act = Action::SeekTo; break; }
                if (hasNext) act = Action::Next; else toast(tr("No next episode"));
                break;
            case BTN_ZOOM: toggleZoom(); break;
            case BTN_EXIT: act = Action::Back; break;
            }
        } else {
            wii_player_pause_toggle();
        }
        showControls();
    }

    if (down & WPAD_BUTTON_B) {
        if (visible) return Action::Back;
        showControls();
    }
    if (down & WPAD_BUTTON_RIGHT) nudgeSeek(+10.0f);
    if (down & WPAD_BUTTON_LEFT)  nudgeSeek(-10.0f);
    /* Left / Right held: on seeking, 10 s steps, 30 s after 3 s (the seek
     * is made once let go, as the presses add up) */
    {
        const u32 hd = Input::held() & (WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT);
        const u64 now = nowMs();
        if (!hd)                               holdSince = 0;
        else if (down & hd)                    holdSince = holdLast = now;
        else if (holdSince && now - holdSince > 500 && now - holdLast >= 200) {
            holdLast = now;
            const float step = now - holdSince > 3000 ? 30.0f : 10.0f;
            nudgeSeek(hd & WPAD_BUTTON_RIGHT ? step : -step);
        }
    }
    if (down & WPAD_BUTTON_UP)    { wii_player_vol_up();   toast(tr("Volume +"), 1000); }
    if (down & WPAD_BUTTON_DOWN)  { wii_player_vol_down(); toast(tr("Volume -"), 1000); }
    if (down & WPAD_BUTTON_PLUS) {
        if (hasNext) act = Action::Next; else toast(tr("No next episode"));
    }
    if (down & WPAD_BUTTON_MINUS) {
        if (hasPrev) act = Action::Prev; else toast(tr("No previous episode"));
    }
    if (down & Input::BTN_ZOOM) toggleZoom();   /* Classic ZL/ZR, GameCube Z */
    if (down & WPAD_BUTTON_1) {
        if (ctx.audioStreams.size() > 1) openPanel(Panel::Audio);
        else toast(tr("No other audio track"));
    }
    if (down & WPAD_BUTTON_2) {
        if (!ctx.subStreams.empty()) openPanel(Panel::Sub);
        else toast(tr("No subtitles available"));
    }
    return act;
}

/* ---- Drawing ----------------------------------------------------------- */

/* Trim s (adding "...") until it fits in maxW pixels. */
std::string PlayerView::fitText(const std::string& s, int size, int maxW)
{
    if ((int)Text::width(font, s.c_str(), size) <= maxW) return s;
    std::string t = s;
    while (!t.empty()) {
        t.pop_back();
        while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back();  /* UTF-8 */
        if ((int)Text::width(font, (t + "...").c_str(), size) <= maxW) break;
    }
    return t + "...";
}

void PlayerView::drawSpinner(float cx, float cy)
{
    Ui::spinner(ringTex, cx, cy);
}

/* Centred pill with a message (busy state, toasts). */
static void messagePill(float cy, const std::string& msg, int size)
{
    const Ui::Palette& p = Ui::pal();
    int w = Ui::textWidth(msg.c_str(), size) + 40;
    int h = size + 18;
    Ui::shadow(320 - w / 2, cy - h / 2 + 2, w, h, h / 2, 6.0f, p.shadow);
    Ui::roundRect(320 - w / 2, cy - h / 2, w, h, h / 2, p.cardTop, p.cardBottom);
    Ui::roundBorder(320 - w / 2, cy - h / 2, w, h, h / 2, 1.5f, p.cardBorder);
    Ui::textCentered(320, cy - size / 2 - 1, msg.c_str(), size, p.text);
}

void PlayerView::render(const ir_t& ir)
{
    const Ui::Palette& p = Ui::pal();
    GRRLIB_FillScreen(0x000000FF);
    const bool hasVideo = VideoSurface::draw();

    /* Slide the control bars in/out */
    float target = controlsVisible() ? 1.0f : 0.0f;
    controlsAlpha += (target - controlsAlpha) * 0.25f;
    if (controlsAlpha < 0.01f) controlsAlpha = 0.0f;
    if (controlsAlpha > 0.99f) controlsAlpha = 1.0f;
    const float k = controlsAlpha;

    /* Subtitles: at the bottom, lifted over the track when it shows */
    if (subtitles && !subtitles->empty()) {
        const float e = 1 - (1 - k) * (1 - k);
        drawSubtitle(452.0f + (BAR_Y - KNOB_R - 14.0f - 452.0f) * e);
    }

    /* ---- Loading / busy ---- */
    const bool stalled = buffering() || rebufPercent >= 0;
    /* a short load (a seek in the file: a fraction of a second) shows no
     * spinner, it would only flash over the picture */
    if (!g_wiifin_loading_active) loadingSince = 0;
    else if (!loadingSince)       loadingSince = nowMs();
    const bool loadingLong = g_wiifin_loading_active && nowMs() - loadingSince > 250;
    if (!hasVideo) {
        /* before the first picture (the stream asked for, then MPlayer
         * filling its cache): the title, the spinner, then "Retrieving" and
         * a bar of how far the cache is towards the start (it creeps on its
         * own while the server is still answering) */
        const u64 now = nowMs();
        if (!loadShownSince) { loadShownSince = now; loadShown = 0.0f; }
        float want = rebufPercent >= 0         ? rebufPercent / 100.0f
                   : !busyMsg.empty()           ? 0.0f     /* MPlayer not started */
                   : cache_fill_status < 0.0f   ? 1.0f
                   : cache_fill_status / CACHE_START;
        const float creep = (float)(now - loadShownSince) / 40000.0f;
        if (want < creep) want = creep;
        if (want > 0.97f) want = 0.97f;
        if (want > loadShown) loadShown += (want - loadShown) * 0.15f;

        Ui::textCentered(320, 136, fitText(ctx.title, 20, 560).c_str(), 20, 0xFFFFFFFF);
        drawSpinner(320.0f, 212.0f);
        const char* label = tr("Retrieving");
        const float BW = 200, BH = 12, lw = (float)Ui::textWidth(label, 16);
        const float x0 = 320 - (lw + 10 + BW) * 0.5f, by = 300, bx = x0 + lw + 10;
        Ui::text(x0, by - 10, label, 16, 0xFFFFFFFF);
        Ui::roundRect(bx, by - BH * 0.5f, BW, BH, 1, 0x6E6E78FF, 0x5A5A64FF);
        if (loadShown > 0.0f)
            Ui::roundRect(bx, by - BH * 0.5f, BW * loadShown, BH, 1, Ui::RED, Ui::RED);
    } else if (!busyMsg.empty() || loadingLong || !hasVideo || stalled) {
        if (hasVideo) GRRLIB_Rectangle(Ui::screenLeft(), 0, Ui::screenWidth(), 480, 0x00000099, 1);
        drawSpinner(320.0f, 220.0f);
        char msg[128];
        if (!busyMsg.empty())       snprintf(msg, sizeof(msg), "%s", tr(busyMsg.c_str()));
        else if (rebufPercent >= 0) snprintf(msg, sizeof(msg), tr("Buffering... %d%%"), rebufPercent);
        else if (stalled)           snprintf(msg, sizeof(msg), "%s", tr("Buffering..."));
        else                        snprintf(msg, sizeof(msg), "%s", tr("Loading..."));
        messagePill(300, msg, 16);
    }
    if (hasVideo) loadShownSince = 0;

    /* ---- Controls ---- */
    if (k > 0.0f) {
        const float ease = 1.0f - (1.0f - k) * (1.0f - k);
        const u32 white = 0xFFFFFFFF, hot = Ui::HOVER_TOP, shade = 0x00000090;

        /* Top: the title on a soft shade, the round back button */
        Ui::pushOffset(0, -(1.0f - ease) * (TOP_H + 30));
        Ui::roundRect(Ui::screenLeft(), 0, Ui::screenWidth(), 76, 0, 0x000000A0, 0x00000000);
        {
            std::string t = fitText(ctx.title, 20, (int)(backCx() - BACK_R - 40));
            Ui::text(25, 17, t.c_str(), 20, shade);
            Ui::text(24, 16, t.c_str(), 20, white);
        }
        Ui::backButton(backCx(), BACK_CY, BACK_R, hoverButton == BTN_EXIT);
        Ui::popOffset();

        /* Bottom: the track, its arrows and knob; pills, pause */
        Ui::pushOffset(0, (1.0f - ease) * (480 - BAR_Y + 30));
        Ui::roundRect(Ui::screenLeft(), 290, Ui::screenWidth(), 190, 0, 0x00000000, 0x000000B0);

        float dur = duration();
        float pos = displayPosition();
        float frac = dur > 0.0f ? pos / dur : 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        if (frac < 0.0f) frac = 0.0f;
        const float tl = trackL(), tr_ = trackR(), tw = tr_ - tl;

        Ui::arrowHead(arrowX(-1), BAR_Y, -1, ARROW_W, ARROW_H, true, hoverButton == 1);
        Ui::arrowHead(arrowX(+1), BAR_Y, +1, ARROW_W, ARROW_H, true, hoverButton == 3);
        /* dotted ends between the arrows and the track */
        for (float x = arrowX(-1) + ARROW_W * 0.5f + 8; x < tl - 4; x += 7) Ui::circle(x, BAR_Y, 1.5f, 0xFFFFFF90);
        for (float x = tr_ + 7; x < arrowX(+1) - ARROW_W * 0.5f - 4; x += 7) Ui::circle(x, BAR_Y, 1.5f, 0xFFFFFF90);
        const float th = hoverBar ? 5.0f : 3.0f;
        Ui::roundRect(tl, BAR_Y - th * 0.5f, tw, th, th * 0.5f, 0xC9D9E6B0, 0xC9D9E6B0);
        if (frac > 0.0f)
            Ui::roundRect(tl, BAR_Y - th * 0.5f, tw * frac < th ? th : tw * frac, th, th * 0.5f, white, white);

        /* Where a seek would land: under the pointer on the track, or the
         * target of the -10s / +10s presses still to be played.  Its time
         * in a bubble, with the trickplay thumbnail above when there is one. */
        float previewAt = -1.0f, px = 0.0f;
        if (hoverBar && dur > 0.0f) {
            float f = (ir.x - tl) / tw;
            if (f < 0.0f) f = 0.0f;
            if (f > 1.0f) f = 1.0f;
            previewAt = f * dur;
            px = ir.x;
        } else if ((seekPending || seekInFlight) && dur > 0.0f) {
            previewAt = pos;
            px = tl + tw * frac;
        }
        /* asked for whenever the controls show, so the tile is there by the
         * time the user seeks */
        GRRLIB_texImg* thumb = trickplay ? trickplay->thumbnail(previewAt >= 0.0f ? previewAt : pos, previewAt >= 0.0f)
                                         : nullptr;

        /* the orange knob, with its two little arrows */
        {
            const float kx = tl + tw * frac, kr = KNOB_R * (hoverBar ? Ui::HOVER_GROW : 1.0f);
            Ui::shadow(kx - kr, BAR_Y - kr + 2, kr * 2, kr * 2, kr, 5.0f, 0x00000080);
            Ui::circle(kx, BAR_Y, kr, 0xE2660EFF);
            Ui::circle(kx, BAR_Y - 1.5f, kr - 2.5f, 0xF5882AFF);
            Ui::roundBorder(kx - kr, BAR_Y - kr, kr * 2, kr * 2, kr, 1.5f, 0xFFFFFFB0);
            const float a = kr * 0.32f;
            Ui::triangle(kx - kr * 0.68f, BAR_Y, kx - kr * 0.18f, BAR_Y - a, kx - kr * 0.18f, BAR_Y + a, white);
            Ui::triangle(kx + kr * 0.68f, BAR_Y, kx + kr * 0.18f, BAR_Y + a, kx + kr * 0.18f, BAR_Y - a, white);
        }

        if (previewAt >= 0.0f) {
            std::string t = fmtTime(previewAt);
            int w = Ui::textWidth(t.c_str(), 14) + 20;
            const float by = BAR_Y - KNOB_R - 34;
            if (thumb) {
                const float TW = 160.0f, sc = TW / thumb->w, tH = thumb->h * sc;
                float tx = px - TW / 2, ty = by - 8 - tH;
                if (tx < Ui::screenLeft() + 8)        tx = Ui::screenLeft() + 8;
                if (tx > Ui::screenRight() - 8 - TW)  tx = Ui::screenRight() - 8 - TW;
                Ui::shadow(tx - 3, ty - 3, TW + 6, tH + 6, 6, 6.0f, 0x00000080);
                Ui::roundRect(tx - 3, ty - 3, TW + 6, tH + 6, 6, 0xE2660EFF, 0xE2660EFF);
                GRRLIB_DrawImg(tx, ty, thumb, 0, sc, sc, 0xFFFFFFFF);
            }
            Ui::roundRect(px - w / 2, by, w, 22, 11, 0xF7922EFF, 0xE2660EFF);
            Ui::triangle(px - 5, by + 22, px + 5, by + 22, px, by + 27, 0xE2660EFF);
            Ui::textCentered(px, by + 3, t.c_str(), 14, white);
        }

        /* the time played and the running time, in light blue pills */
        auto pill = [&](float cx, const std::string& s) {
            const float x = cx - PILL_W * 0.5f, y = ROW_Y - PILL_H * 0.5f;
            Ui::shadow(x, y + 2, PILL_W, PILL_H, PILL_H * 0.5f, 5.0f, 0x00000080);
            Ui::roundRect(x, y, PILL_W, PILL_H, PILL_H * 0.5f, 0xF4FAFFFF, 0xBCDCF0FF);
            Ui::roundBorder(x, y, PILL_W, PILL_H, PILL_H * 0.5f, 1.0f, 0x7FA9C6FF);
            Ui::textCentered(cx, ROW_Y - 10, s.c_str(), 17, 0x2B2B2BFF);
        };
        pill(320 - PILL_DX, fmtHm(pos));
        pill(320 + PILL_DX, dur > 0.0f ? fmtHm(dur) : "--");

        /* pause (or play), and |<< >>| beside it when there is somewhere to go */
        {
            const u32 c = hoverButton == 2 ? hot : white;
            const float g = hoverButton == 2 ? Ui::HOVER_GROW : 1.0f, cy = ROW_Y;
            if (g_mplayer_paused) {
                Ui::triangle(320 - 8 * g + 1, cy - 15 * g + 2, 320 + 14 * g + 1, cy + 2, 320 - 8 * g + 1, cy + 15 * g + 2, shade);
                Ui::triangle(320 - 8 * g, cy - 15 * g, 320 + 14 * g, cy, 320 - 8 * g, cy + 15 * g, c);
            } else {
                Ui::roundRect(320 - 11 * g + 1, cy - 15 * g + 2, 8 * g, 30 * g, 2, shade, shade);
                Ui::roundRect(320 + 3 * g + 1,  cy - 15 * g + 2, 8 * g, 30 * g, 2, shade, shade);
                Ui::roundRect(320 - 11 * g, cy - 15 * g, 8 * g, 30 * g, 2, c, c);
                Ui::roundRect(320 + 3 * g,  cy - 15 * g, 8 * g, 30 * g, 2, c, c);
            }
        }
        const bool chapters = ctx.chapters.size() >= 2;
        const bool hasNext  = ctx.episodeIdx + 1 < (int)ctx.episodes.size() || chapters;
        const bool hasPrev  = (!ctx.episodes.empty() && ctx.episodeIdx > 0) || chapters;
        /* the icons under the pointer: orange, a little bigger */
        if (hasPrev) {   /* |<< */
            const float cx = 320 - SIDE_DX, cy = ROW_Y, g = hoverButton == 0 ? Ui::HOVER_GROW : 1.0f;
            const u32 c = hoverButton == 0 ? hot : white;
            Ui::roundRect(cx - 12 * g, cy - 8 * g, 3 * g, 16 * g, 1.5f, c, c);
            Ui::triangle(cx - 9 * g, cy, cx + 1 * g, cy - 8 * g, cx + 1 * g, cy + 8 * g, c);
            Ui::triangle(cx + 1 * g, cy, cx + 11 * g, cy - 8 * g, cx + 11 * g, cy + 8 * g, c);
        }
        if (hasNext) {   /* >>| */
            const float cx = 320 + SIDE_DX, cy = ROW_Y, g = hoverButton == 4 ? Ui::HOVER_GROW : 1.0f;
            const u32 c = hoverButton == 4 ? hot : white;
            Ui::triangle(cx - 11 * g, cy - 8 * g, cx - 1 * g, cy, cx - 11 * g, cy + 8 * g, c);
            Ui::triangle(cx - 1 * g, cy - 8 * g, cx + 9 * g, cy, cx - 1 * g, cy + 8 * g, c);
            Ui::roundRect(cx + 9 * g, cy - 8 * g, 3 * g, 16 * g, 1.5f, c, c);
        }
        {
            /* zoom: a small screen showing the current mode (picture with
             * bars, or picture filling it) */
            const u32 c = hoverButton == BTN_ZOOM ? hot : white;
            const float cx = zoomCx(), cy = ROW_Y, g = hoverButton == BTN_ZOOM ? Ui::HOVER_GROW : 1.0f;
            Ui::roundBorder(cx - 13 * g, cy - 9 * g, 26 * g, 18 * g, 3, 2.0f, c);
            if (VideoSurface::zoom() == VideoSurface::Zoom::Fill)
                Ui::roundRect(cx - 9.5f * g, cy - 5.5f * g, 19 * g, 11 * g, 1.5f, c, c);
            else
                Ui::roundRect(cx - 9.5f * g, cy - 3 * g, 19 * g, 6 * g, 1, c, c);
        }
        Ui::popOffset();
    }

    /* ---- Skip intro ---- */
    if (introVisible()) {
        Ui::button(INTRO_X, INTRO_Y, INTRO_W, INTRO_H, segmentLabel(activeSegment()), 16,
                   hoverIntro ? Ui::pulse() : 0.35f);
    }

    /* ---- Track picker ---- */
    if (panel != Panel::None) {
        int n = panelCount();
        int rows = n - panelTop < LIST_ROWS ? n - panelTop : LIST_ROWS;
        int h = 44 + rows * ROW_H + 12;
        Ui::card(LIST_X, LIST_Y, LIST_W, h, 16, 0.0f);
        Ui::text(LIST_X + 18, LIST_Y + 12, panel == Panel::Audio ? tr("Audio track") : tr("Subtitles"),
                 18, p.accentDark);
        Ui::roundRect(LIST_X + 14, LIST_Y + 38, LIST_W - 28, 1.5f, 0.75f, Ui::alpha(p.cardBorder, 0.6f));
        for (int i = 0; i < rows; ++i) {
            int row = panelTop + i;
            int y = LIST_Y + 44 + i * ROW_H;
            bool sel = row == panelSel;
            if (sel)
                Ui::roundRect(LIST_X + 8, y, LIST_W - 16, ROW_H - 4, (ROW_H - 4) * 0.5f,
                              Ui::mix(p.accent, 0xFFFFFFFF, 0.2f), p.accentDark);
            bool current = panelRowIsCurrent(row);
            int  maxW = LIST_W - 48 - (current ? 70 : 0);
            Ui::text(LIST_X + 22, y + 7, fitText(panelLabel(row), 16, maxW).c_str(), 16,
                     sel ? p.textOnAccent : p.text);
            if (current)
                Ui::textRight(LIST_X + LIST_W - 22, y + 9, tr("\xe2\x9c\x93 Current"), 13,
                              sel ? p.textOnAccent : p.ok);
        }
    }

    /* ---- Toast ---- */
    if (!toastMsg.empty() && nowMs() < toastUntil)
        messagePill((k > 0.0f) ? TOP_H + 31 : 41, toastMsg, 16);

    /* ---- Pointer ---- */
    if (ir.valid && cursorTex && (k > 0.5f || panel != Panel::None || introVisible()))
        GRRLIB_DrawImg((int)ir.x - 20, (int)ir.y - 4, cursorTex, ir.angle, 1.0f, 1.0f, 0xFFFFFFFF);
}
