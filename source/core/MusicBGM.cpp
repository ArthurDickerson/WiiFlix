#include "MusicBGM.h"
#include "SoundFX.h"
#include "MusicSession.h"

#include <asndlib.h>
#include <mp3player.h>
#include <ogc/lwp.h>
#include <unistd.h>   // usleep
#include <ogc/system.h> // SYS_Report

static bool s_asndRunning = false; /* true while ASND_Init has been called and ASND_End has not */

/* --- MP3Player_Stop with timeout ---
 * MP3Player_Stop can deadlock when the ASND DMA callback state is
 * inconsistent after multiple play/pause cycles.  Run it in a disposable
 * thread and abandon it after 500 ms if it hasn't returned. */
static volatile bool s_stopDone = false;
static u8 s_stopStack[4 * 1024] ATTRIBUTE_ALIGN(32);

static void* stopThreadFunc(void*) {
    MP3Player_Stop();
    s_stopDone = true;
    return nullptr;
}

static void mp3PlayerStopSafe() {
    s_stopDone = false;
    lwp_t t = LWP_THREAD_NULL;
    if (LWP_CreateThread(&t, stopThreadFunc, nullptr,
                         s_stopStack, sizeof(s_stopStack), 80) != 0) {
        /* Thread creation failed — try direct call as fallback */
        MP3Player_Stop();
        return;
    }
    /* Poll for completion with 500 ms timeout (25 × 20 ms) */
    for (int i = 0; i < 25 && !s_stopDone; ++i)
        usleep(20000);
    if (s_stopDone) {
        LWP_JoinThread(t, nullptr);
    } else {
        SYS_Report("[Audio] MP3Player_Stop timed out: thread abandoned\n");
        /* Thread is leaked but we avoid the freeze.  The abandoned thread
         * will eventually return when ASND_End() tears down the DSP. */
    }
}

/* ASND up (it may have been ended: an ASND_End()+ASND_Init() cycle) */
static void asndUp() {
    ASND_Init();
    ASND_Pause(0);     /* ASND_Init first-time path sets global_pause=1.
                        * MP3Player_Init calls ASND_Pause(0) only on its
                        * very first invocation (init_done guard) so after
                        * an ASND_End()+ASND_Init() cycle the unpause is
                        * never issued — all voices stay silent.  Force it. */
    s_asndRunning = true;
}

void MusicBGM::init() {
    asndUp();
    MP3Player_Init();
}

void MusicBGM::stop() {
    mp3PlayerStopSafe();
    if (s_asndRunning) {
        ASND_End();
        s_asndRunning = false;
    }
}

void MusicBGM::pause() {
    mp3PlayerStopSafe();
    if (s_asndRunning) {
        SoundFX::fadeOut();
        ASND_End();          /* Fully shut down ASND's internal DSP thread and its
                              * AI DMA callback so ao_gekko can take over DMA
                              * without conflict.  resume() re-calls ASND_Init(). */
        s_asndRunning = false;
    }
}

void MusicBGM::resume() {
    if (MusicSession::active()) return;   /* the audio is MPlayer's: ASND stays off */
    if (s_asndRunning) {   /* pause() normally ended it: never end ASND twice */
        ASND_End();
        s_asndRunning = false;
    }
    asndUp();
    MP3Player_Init();
}

void MusicBGM::reinitAudio() {
    if (!s_asndRunning) asndUp();
    MP3Player_Init();
}

bool MusicBGM::audioOn() {
    return s_asndRunning;
}
