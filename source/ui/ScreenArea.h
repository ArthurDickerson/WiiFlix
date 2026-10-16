#pragma once
#include <grrlib.h>
#include <wiiuse/wpad.h>

/* Screen area (overscan) calibration, offered once at the first start:
 * two corner brackets, moved with the D-pad until they sit in the corners
 * of the visible picture.  The interface follows live (Ui::setSafeArea).
 *
 *   D-pad  move the corner          A  the other corner
 *   -      back to the full screen  B  done
 */
class ScreenArea {
public:
    /* false once B leaves it */
    bool update();
    void render();

private:
    int calCorner    = 0;   /* 0 top-left, 1 bottom-right */
    u64 calHeldSince = 0;   /* d-pad auto-repeat */
    u64 calLastStep  = 0;
};
