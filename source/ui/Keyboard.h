#pragma once
#include <grrlib.h>
#include <wiiuse/wpad.h>
#include <string>

/* -----------------------------------------------------------------------
 * Keyboard — on-screen keyboard shared by the search and sign-in screens,
 * the Wii Netflix channel's: the whole screen under the red band, dark
 * grey, the text in a light field at the top, then
 *
 *   1 2 3 4 5 6 7 8 9 0
 *   q w e r t y u i o p
 *   a s d f g h j k l -
 *   [ ⇧  ] z x c v b n m '
 *   [!#$ ][áéí ][ ␣  ][◀][▶][ ⌫  ]
 *            [ Cancel ] [ Enter ]
 *
 * Light keys type, dark keys do the rest: Shift (twice quickly: caps
 * lock), the symbols page and the accents page (their key then reads
 * "abc", back to the letters), space, the caret left / right, delete (the
 * character before the caret).  The key chosen is orange and grown.
 *
 * One choice, orange: the pointer takes it when it moves onto another key,
 * the D-pad moves it too; A presses it (A with the pointer off the keys
 * does nothing, unless the D-pad chose last).
 * B deletes a character (Cancel when the text is empty), - is Shift,
 * + is Enter.
 * ----------------------------------------------------------------------- */
class Keyboard {
public:
    enum class Result { None, Enter, Cancel };

    void setEnterLabel(const char* s)   { enterLabel = s; }
    /* The letters page, no Shift, the caret at the end of the text. */
    void reset();

    /* Edits text (at most maxLen bytes, UTF-8) at the caret. */
    Result update(const ir_t& ir, std::string& text, size_t maxLen = 128);
    /* The whole keyboard; masked shows the text as *; placeholder (already
     * translated) shows in the empty field. */
    void   render(const ir_t& ir, const std::string& text, bool masked = false,
                  const char* placeholder = nullptr) const;

    static const int COLS = 10, ROWS = 6;   /* row 5: Cancel and Enter */

private:
    enum Special { K_NONE = 0, K_SHIFT, K_SYM, K_ACC, K_SPACE, K_LEFT, K_RIGHT, K_DEL,
                   K_CANCEL, K_ENTER };
    struct Key { int row, col, span; const char* str; Special sp; };

    const char* enterLabel = "Enter";   /* untranslated: N_() marks it */
    int    page  = 0;          /* 0 letters, 1 symbols, 2 accents */
    int    shift = 0;          /* 0 off, 1 next letter, 2 lock */
    int    selRow = 1, selCol = 0;
    int    hoverRow = -1, hoverCol = -1;   /* the key under the pointer last frame */
    bool   dpad = false;                   /* the D-pad moved the choice last */
    size_t caret = (size_t)-1; /* byte index in the text; past the end: the end */
    unsigned long long lastShiftMs = 0;
    mutable unsigned long long flashMs = 0;   /* key press feedback */
    int    flashRow = -1, flashCol = -1;

    Key  keyAt(int row, int col) const;        /* the key covering a cell */
    void keyRect(const Key& k, float& x, float& y, float& w, float& h) const;
    bool keyUnder(const ir_t& ir, Key& k) const;
    Result press(const Key& k, std::string& text, size_t maxLen);
    std::string label(const Key& k) const;      /* a typing key's text, shifted */
    void drawKey(const Key& k, bool on) const;
};
