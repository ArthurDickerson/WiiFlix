/* App: what a profile has of its own.  Settings from a profile's menu,
 * the passcode asked when it opens, and the questions a new profile is
 * asked: how it signs in, its home screen, its library view. */
#include "App.h"
#include "AppInternal.h"
#include "Lang.h"
#include "SoundFX.h"
#include "../ui/Ui.h"
#include "../ui/PinPad.h"
#include "../input/Input.h"
#include <wiiuse/wpad.h>

extern volatile bool g_app_powerOff;
extern volatile bool g_app_reset;

static void drawPointer(const ir_t& ir, GRRLIB_texImg* tex) {
    if (ir.valid && tex) {
        orient_t orient; WPAD_Orientation(WPAD_CHAN_0, &orient);
        GRRLIB_DrawImg((int)ir.x - 20, (int)ir.y - 4, tex, orient.roll, 1, 1, 0xFFFFFFFF);
    }
}

bool App::askPasscode(const SavedProfile& p) {
    PinPad pin;
    pin.start(N_("Enter the passcode"), N_("This profile is locked"));
    for (;;) {
        Input::update();
        Input::readIR(ir);
        if (g_app_powerOff || g_app_reset) { running = false; return false; }
        const int r = pin.update(ir);
        if (r < 0) return false;
        if (r > 0) {
            if (PinPad::hash(p.passSalt, pin.code()) == p.passHash) {
                SYS_Report("[WiiFin] passcode right\n");
                SoundFX::play(SoundFX::FX::Start);
                return true;
            }
            SYS_Report("[WiiFin] passcode wrong\n");
            pin.reject();
        }
        Ui::background();
        pin.render(ir);
        drawPointer(ir, cursorPointerTex);
        GRRLIB_Render();
    }
}

/* ---------------------------------------------------------------
 * A new profile's questions
 * --------------------------------------------------------------- */

/* title / note: N_ English; labels: N_ each.
 * The index picked, -1 with B (or the power button). */
static int askChoice(ir_t& ir, GRRLIB_texImg* cursor, const char* title, const char* note,
                     const char* const* labels, int n, int sel) {
    const float W = 150, H = 52, GAP = 20;
    const float x0 = 320 - (n * W + (n - 1) * GAP) / 2, Y = 200;
    float focus[3] = {};
    for (;;) {
        Input::update();
        Input::readIR(ir);
        if (g_app_powerOff || g_app_reset) return -1;
        if (Input::isBackPressed()) { SoundFX::play(SoundFX::FX::Back); return -1; }
        const int was = sel;
        if (Input::isLeftPressed()  && sel > 0)     sel--;
        if (Input::isRightPressed() && sel < n - 1) sel++;
        int hover = -1;
        if (ir.valid)
            for (int i = 0; i < n; ++i) {
                float x = x0 + i * (W + GAP);
                if (ir.x >= x && ir.x <= x + W && ir.y >= Y && ir.y <= Y + H) hover = i;
            }
        if (hover >= 0) sel = hover;
        if (sel != was) SoundFX::play(SoundFX::FX::Move);
        if (Input::isAJustPressed() && (!ir.valid || hover >= 0)) { SoundFX::play(SoundFX::FX::Start); return sel; }

        const Ui::Palette& p = Ui::pal();
        Ui::background();
        Ui::textCentered(320, 70, tr(title), 22, p.text);
        if (note) Ui::textCentered(320, 104, tr(note), 14, p.textDim);
        for (int i = 0; i < n; ++i) {
            focus[i] = Ui::approach(focus[i], i == sel ? 1.0f : 0.0f);
            const float x = x0 + i * (W + GAP);
            Ui::button(x, Y, W, H, labels[i], 19, focus[i] > 0.01f ? Ui::pulse() * focus[i] : 0.0f);
        }
        const Ui::Hint l[] = { { "LR", N_("Choose") }, { "A", N_("Select") } };
        Ui::bottomBar(l, 2, nullptr, 0);
        drawPointer(ir, cursor);
        GRRLIB_Render();
    }
}

void App::runOnboarding(SavedProfile& p) {
    SYS_Report("[WiiFin] new profile: its questions\n");
    static const char* const YESNO[2] = { N_("Yes"), N_("No") };
    /* how it signs in */
    int a = askChoice(ir, cursorPointerTex, N_("Sign in automatically on this Wii?"),
                      N_("Anyone who opens WiiFlix can then open this profile"), YESNO, 2, 0);
    if (!running || g_app_powerOff || g_app_reset) return;
    if (a == 1) {
        int b = askChoice(ir, cursorPointerTex, N_("Protect this profile with a passcode?"),
                          N_("Four digits, asked each time it opens"), YESNO, 2, 0);
        if (b == 0) {
            PinPad pin;
            pin.start(N_("Choose a passcode"), N_("Four digits, asked when this profile opens"));
            std::string first;
            for (;;) {
                Input::update();
                Input::readIR(ir);
                if (g_app_powerOff || g_app_reset) return;
                const int r = pin.update(ir);
                if (r < 0) { b = 1; break; }   /* none after all: the password each time */
                if (r > 0 && first.empty()) {
                    first = pin.code();
                    pin.start(N_("Enter it again"));
                } else if (r > 0 && pin.code() != first) {
                    first.clear();
                    pin.start(N_("Choose a passcode"), N_("The two didn't match: once more"));
                    pin.reject();
                } else if (r > 0) {
                    p.passSalt = PinPad::newSalt();
                    p.passHash = PinPad::hash(p.passSalt, pin.code());
                    p.signIn   = SavedProfile::Passcode;
                    break;
                }
                Ui::background();
                pin.render(ir);
                drawPointer(ir, cursorPointerTex);
                GRRLIB_Render();
            }
        }
        if (b != 0) {
            p.signIn = SavedProfile::Password;
            static const char* const OK[1] = { N_("OK") };
            askChoice(ir, cursorPointerTex, N_("The password each time, then"),
                      N_("Or Quick Connect: this profile keeps no sign-in on the Wii"), OK, 1, 0);
        }
    }
    SYS_Report("[WiiFin] new profile: sign-in %d\n", p.signIn);
}

/* Sign Out, from the WiiFlix menu: after a yes, every saved account is
 * forgotten (its token with it), so the next Connect asks for the server
 * and the password again.  The server keeps the session until it is
 * removed there (Dashboard > Devices). */
void App::signOut() {
    static const char* const OK[1] = { N_("OK") };
    if (profiles.empty()) {
        askChoice(ir, cursorPointerTex, N_("You are not signed in"), nullptr, OK, 1, 0);
        return;
    }
    static const char* const YESNO[2] = { N_("Sign Out"), N_("Cancel") };
    const int a = askChoice(ir, cursorPointerTex, N_("Sign out of Jellyfin?"),
                            N_("WiiFlix forgets every saved sign-in on this Wii"), YESNO, 2, 1);
    if (a != 0) return;
    SYS_Report("[WiiFin] signed out (%d profiles forgotten)\n", (int)profiles.size());
    profiles.clear();
    saveSettings();
    askChoice(ir, cursorPointerTex, N_("Signed out"), N_("Connect To Jellyfin signs in again"), OK, 1, 0);
}
