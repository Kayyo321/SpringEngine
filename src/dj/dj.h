#ifndef DH_H
#define DH_H

#include "common.h"

#include "raylib.h"

enum {
    ChannelMax = 64,
};

typedef struct {
    Heap key_heap;
    char *key;
    Sound sound;
    float default_volume;
    usize hash;
    boolean filled;
} StringSoundMapBucket;

typedef struct {
    usize len;
    usize cap;
    Heap buckets_heap;
    StringSoundMapBucket *buckets;
} StringSoundMap; // map[string]Sound

typedef struct {
    Heap key_heap;
    char *key;
    Music music;
    float default_volume;
    usize hash;
    boolean filled;
} StringMusicMapBucket;

typedef struct {
    usize len;
    usize cap;
    Heap buckets_heap;
    StringMusicMapBucket *buckets;
} StringMusicMap; // map[string]Music

typedef struct {
    StringSoundMap sound_aliases;
    StringMusicMap music_aliases;

    struct {
        Heap alias_heap;
        char *alias;
        Sound sound;
        float volume;
        boolean empty;
    } sound_channels[ChannelMax];

    struct {
        Heap alias_heap;
        char *alias;
        Music music;
        float volume;
        boolean empty;
    } music_channels[ChannelMax];
} DJ;

DJ init_dj(void);
void update_dj(DJ *dj);
void dispose_dj(DJ *dj);

void load_sound_source_as_with_volume(DJ *dj, const char *path, const char *alias, float default_volume);
void load_sound_source_as(DJ *dj, const char *path, const char *alias);
int play_sound(DJ *dj, const char *alias); // return channel, -1 if failed
void restart_sound(DJ *dj, int channel);
void stop_sound(DJ *dj, int channel);
void set_sound_channel_volume(DJ *dj, int channel, float volume);
float get_sound_channel_volume(const DJ *dj, int channel);

void load_music_source_as_with_volume(DJ *dj, const char *path, const char *alias, float default_volume);
void load_music_source_as(DJ *dj, const char *path, const char *alias);
int play_music(DJ *dj, const char *alias, boolean loop); // return channel, -1 if failed
void restart_music(DJ *dj, int channel);
void stop_music(DJ *dj, int channel);
void set_music_channel_volume(DJ *dj, int channel, float volume);
float get_music_channel_volume(const DJ *dj, int channel);

void clear_sound_channels(DJ *dj);
void clear_music_channels(DJ *dj);
void clear_sound_aliases(DJ *dj);
void clear_music_aliases(DJ *dj);

#endif //DJH
