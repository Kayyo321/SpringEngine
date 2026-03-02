#include "testing.h"

#include "dj/dj.h"

#include "common.h"

static void initialize_empty_channels(DJ *dj) {
    if (!dj)
        return;

    for (int i = 0; i < ChannelMax; ++i) {
        dj->sound_channels[i].empty = True;
        dj->music_channels[i].empty = True;
    }
}

void run_dj_tests(void) {
#ifdef TESTING
    usize failed = 0;

    log_msg("Running DJ tests...");

    DJ dj = {0};
    initialize_empty_channels(&dj);

    const usize warn_before_missing_sound = get_warn_count();
    const usize err_before_missing_sound = get_error_count();
    if (play_sound(&dj, "missing") != -1) {
        log_err("play_sound should return -1 for unknown aliases");
        ++failed;
    }
    restore_diagnostic_counts(warn_before_missing_sound, err_before_missing_sound);

    if (play_sound(Null, "missing") != -1 || play_sound(&dj, Null) != -1) {
        log_err("play_sound should reject null inputs");
        ++failed;
    }

    const usize warn_before_missing_music = get_warn_count();
    const usize err_before_missing_music = get_error_count();
    if (play_music(&dj, "missing", True) != -1) {
        log_err("play_music should return -1 for unknown aliases");
        ++failed;
    }
    restore_diagnostic_counts(warn_before_missing_music, err_before_missing_music);

    if (play_music(Null, "missing", False) != -1 || play_music(&dj, Null, False) != -1) {
        log_err("play_music should reject null inputs");
        ++failed;
    }

    load_sound_source_as(Null, "path.wav", "alias");
    load_sound_source_as(&dj, Null, "alias");
    load_sound_source_as(&dj, "path.wav", Null);

    load_music_source_as(Null, "path.ogg", "alias");
    load_music_source_as(&dj, Null, "alias");
    load_music_source_as(&dj, "path.ogg", Null);

    restart_sound(Null, 0);
    restart_sound(&dj, -1);
    restart_sound(&dj, ChannelMax);

    stop_sound(Null, 0);
    stop_sound(&dj, -1);
    stop_sound(&dj, ChannelMax);

    restart_music(Null, 0);
    restart_music(&dj, -1);
    restart_music(&dj, ChannelMax);

    stop_music(Null, 0);
    stop_music(&dj, -1);
    stop_music(&dj, ChannelMax);

    update_dj(Null);
    update_dj(&dj);

    clear_sound_channels(Null);
    clear_music_channels(Null);
    clear_sound_aliases(Null);
    clear_music_aliases(Null);

    clear_sound_channels(&dj);
    clear_music_channels(&dj);
    clear_sound_aliases(&dj);
    clear_music_aliases(&dj);

    for (int i = 0; i < ChannelMax; ++i) {
        if (dj.sound_channels[i].empty != True || dj.music_channels[i].empty != True) {
            log_err("clear_*_channels should leave every channel empty");
            ++failed;
            break;
        }
    }

    dispose_dj(Null);
    dispose_dj(&dj);

    if (dj.sound_aliases.len != 0 || dj.sound_aliases.cap != 0 || dj.sound_aliases.buckets != Null) {
        log_err("dispose_dj should clear sound aliases");
        ++failed;
    }

    if (dj.music_aliases.len != 0 || dj.music_aliases.cap != 0 || dj.music_aliases.buckets != Null) {
        log_err("dispose_dj should clear music aliases");
        ++failed;
    }

    record_test_result("DJ tests", failed);
#endif // TESTING
}
