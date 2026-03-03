#include "dj.h"

#include "vfs.h"

#include "raylib.h"

#include <string.h>

enum {
    MapMinCapacity = 8,
};

static float clamp_volume(float volume) {
    if (volume < 0.0f)
        return 0.0f;

    if (volume > 1.0f)
        return 1.0f;

    return volume;
}

static usize hash_str(const char *key) {
    const unsigned char *bytes = (const unsigned char *)key;

    usize hash = (usize)1469598103934665603ULL;
    while (*bytes) {
        hash ^= (usize)(*bytes++);
        hash *= (usize)1099511628211ULL;
    }

    return hash;
}

static Heap dup_str_heap(const char *source) {
    if (!source)
        return NullHeap;

    const usize len = strlen(source);
    Heap copy = allocate(len + 1, sizeof(char));
    if (!copy.pointer)
        return NullHeap;

    memcpy(copy.pointer, source, len + 1);
    return copy;
}

static char *dup_str(const char *source, Heap *out_heap) {
    if (!source)
        return Null;

    Heap copy = dup_str_heap(source);
    if (!copy.pointer)
        return Null;

    if (out_heap)
        *out_heap = copy;

    return (char *)copy.pointer;
}

static result sound_map_rehash(StringSoundMap *map, usize requested_cap) {
    if (!map)
        return Err;

    usize next_cap = MapMinCapacity;
    while (next_cap < requested_cap)
        next_cap *= 2;

    Heap next_heap = allocate(next_cap, sizeof(*map->buckets));
    if (!next_heap.pointer)
        return Err;

    StringSoundMapBucket *next_buckets = (StringSoundMapBucket *)next_heap.pointer;
    memset(next_buckets, 0, next_cap * sizeof(*next_buckets));
    for (usize i = 0; i < map->cap; ++i) {
        if (!map->buckets || !map->buckets[i].filled)
            continue;

        const usize hash = map->buckets[i].hash;
        usize slot = hash & (next_cap - 1);

        while (next_buckets[slot].filled)
            slot = (slot + 1) & (next_cap - 1);

        next_buckets[slot] = map->buckets[i];
    }

    if (map->buckets_heap.pointer)
        deallocate(map->buckets_heap);

    map->buckets_heap = next_heap;
    map->buckets = next_buckets;
    map->cap = next_cap;
    return Ok;
}

static result ensure_sound_map_capacity(StringSoundMap *map, usize needed) {
    if (!map)
        return Err;

    if (map->cap == 0)
        return sound_map_rehash(map, MapMinCapacity);

    if (needed * 10 >= map->cap * 7)
        return sound_map_rehash(map, map->cap * 2);

    return Ok;
}

static StringSoundMapBucket *sound_map_get_bucket(StringSoundMap *map, const char *key) {
    if (!map || !key || map->cap == 0 || !map->buckets)
        return Null;

    const usize hash = hash_str(key);
    usize slot = hash & (map->cap - 1);

    for (usize i = 0; i < map->cap; ++i) {
        if (!map->buckets[slot].filled)
            return Null;

        if (map->buckets[slot].hash == hash && strcmp(map->buckets[slot].key, key) == 0)
            return &map->buckets[slot];

        slot = (slot + 1) & (map->cap - 1);
    }

    return Null;
}

static result sound_map_set(StringSoundMap *map, const char *key, Sound sound, float default_volume) {
    if (!map || !key)
        return Err;

    const float clamped_default_volume = clamp_volume(default_volume);

    if (ensure_sound_map_capacity(map, map->len + 1) != Ok)
        return Err;

    const usize hash = hash_str(key);
    usize slot = hash & (map->cap - 1);

    for (usize i = 0; i < map->cap; ++i) {
        if (!map->buckets[slot].filled)
            break;

        if (map->buckets[slot].hash == hash && strcmp(map->buckets[slot].key, key) == 0) {
            UnloadSound(map->buckets[slot].sound);
            map->buckets[slot].sound = sound;
            map->buckets[slot].default_volume = clamped_default_volume;
            return Ok;
        }

        slot = (slot + 1) & (map->cap - 1);
    }

    Heap next_key_heap = NullHeap;
    char *next_key = dup_str(key, &next_key_heap);
    if (!next_key) {
        if (next_key_heap.pointer)
            deallocate(next_key_heap);

        return Err;
    }

    map->buckets[slot].key_heap = next_key_heap;
    map->buckets[slot].key = next_key;
    map->buckets[slot].sound = sound;
    map->buckets[slot].default_volume = clamped_default_volume;
    map->buckets[slot].hash = hash;
    map->buckets[slot].filled = True;
    ++map->len;
    return Ok;
}

static void sound_map_clear(StringSoundMap *map) {
    if (!map)
        return;

    for (usize i = 0; i < map->cap; ++i) {
        if (!map->buckets || !map->buckets[i].filled)
            continue;

        if (map->buckets[i].key_heap.pointer)
            deallocate(map->buckets[i].key_heap);

        map->buckets[i].key_heap = NullHeap;
        UnloadSound(map->buckets[i].sound);
        map->buckets[i].sound = (Sound){0};
        map->buckets[i].filled = False;
    }

    if (map->buckets_heap.pointer)
        deallocate(map->buckets_heap);

    map->buckets_heap = NullHeap;
    map->buckets = Null;
    map->len = 0;
    map->cap = 0;
}

static result music_map_rehash(StringMusicMap *map, usize requested_cap) {
    if (!map)
        return Err;

    usize next_cap = MapMinCapacity;
    while (next_cap < requested_cap)
        next_cap *= 2;

    Heap next_heap = allocate(next_cap, sizeof(*map->buckets));
    if (!next_heap.pointer)
        return Err;

    StringMusicMapBucket *next_buckets = (StringMusicMapBucket *)next_heap.pointer;
    memset(next_buckets, 0, next_cap * sizeof(*next_buckets));
    for (usize i = 0; i < map->cap; ++i) {
        if (!map->buckets || !map->buckets[i].filled)
            continue;

        const usize hash = map->buckets[i].hash;
        usize slot = hash & (next_cap - 1);

        while (next_buckets[slot].filled)
            slot = (slot + 1) & (next_cap - 1);

        next_buckets[slot] = map->buckets[i];
    }

    if (map->buckets_heap.pointer)
        deallocate(map->buckets_heap);

    map->buckets_heap = next_heap;
    map->buckets = next_buckets;
    map->cap = next_cap;
    return Ok;
}

static result ensure_music_map_capacity(StringMusicMap *map, usize needed) {
    if (!map)
        return Err;

    if (map->cap == 0)
        return music_map_rehash(map, MapMinCapacity);

    if (needed * 10 >= map->cap * 7)
        return music_map_rehash(map, map->cap * 2);

    return Ok;
}

static StringMusicMapBucket *music_map_get_bucket(StringMusicMap *map, const char *key) {
    if (!map || !key || map->cap == 0 || !map->buckets)
        return Null;

    const usize hash = hash_str(key);
    usize slot = hash & (map->cap - 1);

    for (usize i = 0; i < map->cap; ++i) {
        if (!map->buckets[slot].filled)
            return Null;

        if (map->buckets[slot].hash == hash && strcmp(map->buckets[slot].key, key) == 0)
            return &map->buckets[slot];

        slot = (slot + 1) & (map->cap - 1);
    }

    return Null;
}

static result music_map_set(StringMusicMap *map, const char *key, Music music, float default_volume) {
    if (!map || !key)
        return Err;

    const float clamped_default_volume = clamp_volume(default_volume);

    if (ensure_music_map_capacity(map, map->len + 1) != Ok)
        return Err;

    const usize hash = hash_str(key);
    usize slot = hash & (map->cap - 1);

    for (usize i = 0; i < map->cap; ++i) {
        if (!map->buckets[slot].filled)
            break;

        if (map->buckets[slot].hash == hash && strcmp(map->buckets[slot].key, key) == 0) {
            UnloadMusicStream(map->buckets[slot].music);
            map->buckets[slot].music = music;
            map->buckets[slot].default_volume = clamped_default_volume;
            return Ok;
        }

        slot = (slot + 1) & (map->cap - 1);
    }

    Heap next_key_heap = NullHeap;
    char *next_key = dup_str(key, &next_key_heap);
    if (!next_key) {
        if (next_key_heap.pointer)
            deallocate(next_key_heap);

        return Err;
    }

    map->buckets[slot].key_heap = next_key_heap;
    map->buckets[slot].key = next_key;
    map->buckets[slot].music = music;
    map->buckets[slot].default_volume = clamped_default_volume;
    map->buckets[slot].hash = hash;
    map->buckets[slot].filled = True;
    ++map->len;
    return Ok;
}

static void music_map_clear(StringMusicMap *map) {
    if (!map)
        return;

    for (usize i = 0; i < map->cap; ++i) {
        if (!map->buckets || !map->buckets[i].filled)
            continue;

        if (map->buckets[i].key_heap.pointer)
            deallocate(map->buckets[i].key_heap);

        map->buckets[i].key_heap = NullHeap;
        UnloadMusicStream(map->buckets[i].music);
        map->buckets[i].music = (Music){0};
        map->buckets[i].filled = False;
    }

    if (map->buckets_heap.pointer)
        deallocate(map->buckets_heap);

    map->buckets_heap = NullHeap;
    map->buckets = Null;
    map->len = 0;
    map->cap = 0;
}

static boolean is_valid_channel(int channel) {
    return channel >= 0 && channel < ChannelMax;
}

static void stop_sound_channels_for_alias(DJ *dj, const char *alias) {
    if (!dj || !alias)
        return;

    for (int i = 0; i < ChannelMax; ++i) {
        if (dj->sound_channels[i].empty || !dj->sound_channels[i].alias)
            continue;

        if (strcmp(dj->sound_channels[i].alias, alias) == 0)
            stop_sound(dj, i);
    }
}

static void stop_music_channels_for_alias(DJ *dj, const char *alias) {
    if (!dj || !alias)
        return;

    for (int i = 0; i < ChannelMax; ++i) {
        if (dj->music_channels[i].empty || !dj->music_channels[i].alias)
            continue;

        if (strcmp(dj->music_channels[i].alias, alias) == 0)
            stop_music(dj, i);
    }
}

static void reset_sound_channel(DJ *dj, int channel) {
    if (!dj || !is_valid_channel(channel))
        return;

    if (!dj->sound_channels[channel].empty)
        StopSound(dj->sound_channels[channel].sound);

    if (dj->sound_channels[channel].alias_heap.pointer)
        deallocate(dj->sound_channels[channel].alias_heap);

    dj->sound_channels[channel].alias_heap = NullHeap;
    dj->sound_channels[channel].alias = Null;
    dj->sound_channels[channel].sound = (Sound){0};
    dj->sound_channels[channel].volume = 1.0f;
    dj->sound_channels[channel].empty = True;
}

static void reset_music_channel(DJ *dj, int channel) {
    if (!dj || !is_valid_channel(channel))
        return;

    if (!dj->music_channels[channel].empty)
        StopMusicStream(dj->music_channels[channel].music);

    if (dj->music_channels[channel].alias_heap.pointer)
        deallocate(dj->music_channels[channel].alias_heap);

    dj->music_channels[channel].alias_heap = NullHeap;
    dj->music_channels[channel].alias = Null;
    dj->music_channels[channel].music = (Music){0};
    dj->music_channels[channel].volume = 1.0f;
    dj->music_channels[channel].empty = True;
}

static int next_sound_channel(const DJ *dj) {
    if (!dj)
        return -1;

    for (int i = 0; i < ChannelMax; ++i) {
        if (dj->sound_channels[i].empty)
            return i;
    }

    return -1;
}

static int next_music_channel(const DJ *dj) {
    if (!dj)
        return -1;

    for (int i = 0; i < ChannelMax; ++i) {
        if (dj->music_channels[i].empty)
            return i;
    }

    return -1;
}

DJ init_dj(void) {
    DJ dj = {0};

    SetTraceLogLevel(LOG_NONE);

    if (!IsAudioDeviceReady())
        InitAudioDevice();

    for (int i = 0; i < ChannelMax; ++i) {
        dj.sound_channels[i].empty = True;
        dj.sound_channels[i].volume = 1.0f;
        dj.music_channels[i].empty = True;
        dj.music_channels[i].volume = 1.0f;
    }
    
    return dj;
}

void update_dj(DJ *dj) {
    if (!dj)
        return;

    for (int i = 0; i < ChannelMax; ++i) {
        if (!dj->sound_channels[i].empty && !IsSoundPlaying(dj->sound_channels[i].sound))
            reset_sound_channel(dj, i);
    }

    for (int i = 0; i < ChannelMax; ++i) {
        if (dj->music_channels[i].empty)
            continue;

        UpdateMusicStream(dj->music_channels[i].music);
        if (!IsMusicStreamPlaying(dj->music_channels[i].music) && !dj->music_channels[i].music.looping)
            reset_music_channel(dj, i);
    }
}

void dispose_dj(DJ *dj) {
    if (!dj)
        return;

    clear_music_channels(dj);
    clear_sound_channels(dj);
    clear_music_aliases(dj);
    clear_sound_aliases(dj);
}

void load_sound_source_as_with_volume(DJ *dj, const char *path, const char *alias, float default_volume) {
    if (!dj || !path || !alias)
        return;

    Sound sound = {0};
    if (vfs_load_sound(path, &sound) != Ok)
        return;

    stop_sound_channels_for_alias(dj, alias);
    if (sound_map_set(&dj->sound_aliases, alias, sound, default_volume) != Ok) {
        UnloadSound(sound);
        log_err("Failed to register sound alias '%s'", alias);
    }
}

void load_sound_source_as(DJ *dj, const char *path, const char *alias) {
    load_sound_source_as_with_volume(dj, path, alias, 1.0f);
}

int play_sound(DJ *dj, const char *alias) {
    if (!dj || !alias)
        return -1;

    StringSoundMapBucket *registered = sound_map_get_bucket(&dj->sound_aliases, alias);
    if (!registered) {
        log_warn("Sound alias '%s' is not registered", alias);
        return -1;
    }

    const int channel = next_sound_channel(dj);
    if (channel < 0) {
        log_warn("No free sound channels");
        return -1;
    }

    Sound sound = registered->sound;
    const float volume = clamp_volume(registered->default_volume);

    Heap alias_heap = NullHeap;
    char *alias_copy = dup_str(alias, &alias_heap);
    if (!alias_copy) {
        log_err("Failed to allocate sound alias copy for '%s'", alias);
        return -1;
    }

    dj->sound_channels[channel].sound = sound;
    dj->sound_channels[channel].alias_heap = alias_heap;
    dj->sound_channels[channel].alias = alias_copy;
    dj->sound_channels[channel].volume = volume;
    dj->sound_channels[channel].empty = False;
    SetSoundVolume(sound, volume);
    PlaySound(sound);
    return channel;
}

void restart_sound(DJ *dj, int channel) {
    if (!dj || !is_valid_channel(channel) || dj->sound_channels[channel].empty)
        return;

    SetSoundVolume(dj->sound_channels[channel].sound, dj->sound_channels[channel].volume);
    StopSound(dj->sound_channels[channel].sound);
    PlaySound(dj->sound_channels[channel].sound);
}

void stop_sound(DJ *dj, int channel) {
    if (!dj || !is_valid_channel(channel) || dj->sound_channels[channel].empty)
        return;

    reset_sound_channel(dj, channel);
}

void set_sound_channel_volume(DJ *dj, int channel, float volume) {
    if (!dj || !is_valid_channel(channel) || dj->sound_channels[channel].empty)
        return;

    const float clamped_volume = clamp_volume(volume);
    dj->sound_channels[channel].volume = clamped_volume;
    SetSoundVolume(dj->sound_channels[channel].sound, clamped_volume);
}

float get_sound_channel_volume(const DJ *dj, int channel) {
    if (!dj || !is_valid_channel(channel) || dj->sound_channels[channel].empty)
        return -1.0f;

    return dj->sound_channels[channel].volume;
}

void load_music_source_as_with_volume(DJ *dj, const char *path, const char *alias, float default_volume) {
    if (!dj || !path || !alias)
        return;

    Music music = {0};
    if (vfs_load_music(path, &music) != Ok)
        return;

    stop_music_channels_for_alias(dj, alias);
    if (music_map_set(&dj->music_aliases, alias, music, default_volume) != Ok) {
        UnloadMusicStream(music);
        log_err("Failed to register music alias '%s'", alias);
    }
}

void load_music_source_as(DJ *dj, const char *path, const char *alias) {
    load_music_source_as_with_volume(dj, path, alias, 1.0f);
}

int play_music(DJ *dj, const char *alias, boolean loop) {
    if (!dj || !alias)
        return -1;

    StringMusicMapBucket *registered = music_map_get_bucket(&dj->music_aliases, alias);
    if (!registered) {
        log_warn("Music alias '%s' is not registered", alias);
        return -1;
    }

    const int channel = next_music_channel(dj);
    if (channel < 0) {
        log_warn("No free music channels");
        return -1;
    }

    Music music = registered->music;
    const float volume = clamp_volume(registered->default_volume);

    Heap alias_heap = NullHeap;
    char *alias_copy = dup_str(alias, &alias_heap);
    if (!alias_copy) {
        log_err("Failed to allocate music alias copy for '%s'", alias);
        return -1;
    }

    music.looping = loop == True;
    dj->music_channels[channel].music = music;
    dj->music_channels[channel].alias_heap = alias_heap;
    dj->music_channels[channel].alias = alias_copy;
    dj->music_channels[channel].volume = volume;
    dj->music_channels[channel].empty = False;
    SetMusicVolume(music, volume);
    PlayMusicStream(music);
    return channel;
}

void restart_music(DJ *dj, int channel) {
    if (!dj || !is_valid_channel(channel) || dj->music_channels[channel].empty)
        return;

    SetMusicVolume(dj->music_channels[channel].music, dj->music_channels[channel].volume);
    StopMusicStream(dj->music_channels[channel].music);
    SeekMusicStream(dj->music_channels[channel].music, 0.0f);
    PlayMusicStream(dj->music_channels[channel].music);
}

void stop_music(DJ *dj, int channel) {
    if (!dj || !is_valid_channel(channel) || dj->music_channels[channel].empty)
        return;

    reset_music_channel(dj, channel);
}

void set_music_channel_volume(DJ *dj, int channel, float volume) {
    if (!dj || !is_valid_channel(channel) || dj->music_channels[channel].empty)
        return;

    const float clamped_volume = clamp_volume(volume);
    dj->music_channels[channel].volume = clamped_volume;
    SetMusicVolume(dj->music_channels[channel].music, clamped_volume);
}

float get_music_channel_volume(const DJ *dj, int channel) {
    if (!dj || !is_valid_channel(channel) || dj->music_channels[channel].empty)
        return -1.0f;

    return dj->music_channels[channel].volume;
}

void clear_sound_channels(DJ *dj) {
    if (!dj)
        return;

    for (int i = 0; i < ChannelMax; ++i)
        reset_sound_channel(dj, i);
}

void clear_music_channels(DJ *dj) {
    if (!dj)
        return;

    for (int i = 0; i < ChannelMax; ++i)
        reset_music_channel(dj, i);
}

void clear_sound_aliases(DJ *dj) {
    if (!dj)
        return;

    sound_map_clear(&dj->sound_aliases);
}

void clear_music_aliases(DJ *dj) {
    if (!dj)
        return;

    music_map_clear(&dj->music_aliases);
}
