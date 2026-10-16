#pragma once

/**
 * MusicBGM — the menus' audio (ASND).
 *
 * WiiFlix has no menu music: this only brings ASND (and MP3Player) up for
 * the interface sounds (SoundFX), everywhere in the app except while
 * MPlayer plays (ao_gekko needs the DSP).  Call pause() before the player
 * starts and resume() once it has stopped.
 */
namespace MusicBGM {
    void init();           // One-time init: ASND_Init + MP3Player_Init
    void stop();           // Full teardown: MP3 + ASND_End (pair with init to restart)
    void pause();          // ASND off (call before starting MPlayer)
    void resume();         // ASND back on (call after MPlayer stopped)
    void reinitAudio();    // ASND on if it is off, MP3Player re-inited
    bool audioOn();        // ASND up: sound effects can play
}
