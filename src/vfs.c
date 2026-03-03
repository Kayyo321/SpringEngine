#include "vfs.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define TAR_BLOCK_SIZE 512

typedef enum {
    VfsModeNone = 0,
    VfsModeDisk,
    VfsModeArchive,
} VfsMode;

typedef struct {
    char path[PATH_MAX];
    usize data_offset;
    usize data_size;
    boolean is_directory;
} VfsEntry;

typedef struct {
    VfsMode mode;
    char source_path[PATH_MAX];

    Heap archive_heap;
    unsigned char *archive_bytes;
    usize archive_size;

    Heap entries_heap;
    VfsEntry *entries;
    usize entry_count;
    usize entry_capacity;
} VfsState;

typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} TarHeader;

static VfsState vfs_state = {0};

static boolean has_targame_extension(const char *path) {
    if (!path)
        return False;

    const char *dot = strrchr(path, '.');
    if (!dot)
        return False;

    return strcmp(dot, ".targame") == 0 ? True : False;
}

static boolean is_absolute_path(const char *path) {
    return (path && path[0] == '/') ? True : False;
}

static result normalize_path(const char *path, char *out_path, usize out_size) {
    if (!path || !out_path || out_size == 0)
        return Err;

    usize out_index = 0;
    usize index = 0;

    while (path[index] == ' ')
        ++index;

    while (path[index] == '.' && path[index + 1] == '/')
        index += 2;

    for (; path[index] != '\0'; ++index) {
        char ch = path[index];
        if (ch == '\\')
            ch = '/';

        if (ch == '/' && out_index > 0 && out_path[out_index - 1] == '/')
            continue;

        if (out_index + 1 >= out_size)
            return Err;

        out_path[out_index++] = ch;
    }

    while (out_index > 0 && out_path[out_index - 1] == '/')
        --out_index;

    if (out_index == 0) {
        out_path[0] = '\0';
        return Ok;
    }

    out_path[out_index] = '\0';
    return Ok;
}

static boolean path_has_unsafe_components(const char *path) {
    if (!path || path[0] == '\0')
        return True;

    if (path[0] == '/')
        return True;

    if (strcmp(path, "..") == 0)
        return True;

    if (strstr(path, "../") != Null)
        return True;

    if (strstr(path, "/..") != Null)
        return True;

    return False;
}

static unsigned long long parse_octal_field(const char *field, usize field_size) {
    if (!field || field_size == 0)
        return 0;

    unsigned long long value = 0;
    usize index = 0;

    while (index < field_size && (field[index] == ' ' || field[index] == '\0'))
        ++index;

    for (; index < field_size; ++index) {
        char ch = field[index];
        if (ch < '0' || ch > '7')
            break;
        value = (value << 3) + (unsigned long long)(ch - '0');
    }

    return value;
}

static boolean is_zero_block(const unsigned char *data, usize offset, usize data_size) {
    if (!data || offset + TAR_BLOCK_SIZE > data_size)
        return True;

    for (usize index = 0; index < TAR_BLOCK_SIZE; ++index) {
        if (data[offset + index] != 0)
            return False;
    }

    return True;
}

static result read_tar_entry_path(const TarHeader *header, char *out_path, usize out_size) {
    if (!header || !out_path || out_size == 0)
        return Err;

    char name[101] = {0};
    char prefix[156] = {0};
    memcpy(name, header->name, sizeof(header->name));
    memcpy(prefix, header->prefix, sizeof(header->prefix));

    char raw_path[PATH_MAX] = {0};
    if (prefix[0] != '\0') {
        if (snprintf(raw_path, sizeof(raw_path), "%s/%s", prefix, name) >= (int)sizeof(raw_path))
            return Err;
    } else {
        if (snprintf(raw_path, sizeof(raw_path), "%s", name) >= (int)sizeof(raw_path))
            return Err;
    }

    if (normalize_path(raw_path, out_path, out_size) != Ok)
        return Err;

    if (path_has_unsafe_components(out_path))
        return Err;

    return Ok;
}

static result ensure_entry_capacity(usize needed) {
    if (needed <= vfs_state.entry_capacity)
        return Ok;

    usize next_capacity = vfs_state.entry_capacity == 0 ? 64 : vfs_state.entry_capacity * 2;
    while (next_capacity < needed)
        next_capacity *= 2;

    const usize next_size = next_capacity * sizeof(VfsEntry);
    if (!vfs_state.entries) {
        vfs_state.entries_heap = allocate(next_capacity, sizeof(VfsEntry));
        vfs_state.entries = (VfsEntry *)vfs_state.entries_heap.pointer;
        if (!vfs_state.entries)
            return Err;
    } else {
        vfs_state.entries_heap = reallocate(vfs_state.entries_heap, next_size);
        vfs_state.entries = (VfsEntry *)vfs_state.entries_heap.pointer;
        if (!vfs_state.entries)
            return Err;
    }

    if (next_capacity > vfs_state.entry_capacity) {
        memset(vfs_state.entries + vfs_state.entry_capacity, 0, (next_capacity - vfs_state.entry_capacity) * sizeof(VfsEntry));
    }

    vfs_state.entry_capacity = next_capacity;
    return Ok;
}

static result add_archive_entry(const char *path, usize data_offset, usize data_size, boolean is_directory) {
    if (!path || path[0] == '\0')
        return Err;

    if (ensure_entry_capacity(vfs_state.entry_count + 1) != Ok)
        return Err;

    VfsEntry *entry = &vfs_state.entries[vfs_state.entry_count++];
    memset(entry, 0, sizeof(*entry));

    if (snprintf(entry->path, sizeof(entry->path), "%s", path) >= (int)sizeof(entry->path))
        return Err;

    entry->data_offset = data_offset;
    entry->data_size = data_size;
    entry->is_directory = is_directory;
    return Ok;
}

static int find_archive_entry_index(const char *path) {
    if (!path || path[0] == '\0')
        return -1;

    char normalized[PATH_MAX] = {0};
    if (normalize_path(path, normalized, sizeof(normalized)) != Ok)
        return -1;

    for (usize index = 0; index < vfs_state.entry_count; ++index) {
        if (strcmp(vfs_state.entries[index].path, normalized) == 0)
            return (int)index;
    }

    return -1;
}

static result read_disk_file_bytes(const char *path, Heap *out_bytes) {
    if (!path || !out_bytes)
        return Err;

    FILE *file = fopen(path, "rb");
    if (!file)
        return Err;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return Err;
    }

    long file_size_long = ftell(file);
    if (file_size_long < 0) {
        fclose(file);
        return Err;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return Err;
    }

    const usize file_size = (usize)file_size_long;
    Heap file_heap = allocate(file_size > 0 ? file_size : 1, sizeof(unsigned char));
    unsigned char *bytes = (unsigned char *)file_heap.pointer;
    if (!bytes) {
        fclose(file);
        return Err;
    }

    if (file_size > 0 && fread(bytes, 1, file_size, file) != file_size) {
        fclose(file);
        deallocate(file_heap);
        return Err;
    }

    fclose(file);
    *out_bytes = file_heap;
    return Ok;
}

static result mount_archive_file(const char *archive_path) {
    Heap archive_heap = NullHeap;
    if (read_disk_file_bytes(archive_path, &archive_heap) != Ok) {
        log_err("Failed to read archive '%s'", archive_path);
        return Err;
    }

    unsigned char *archive_bytes = (unsigned char *)archive_heap.pointer;
    usize archive_size = archive_heap.size;

    usize offset = 0;
    while (offset + TAR_BLOCK_SIZE <= archive_size) {
        if (is_zero_block(archive_bytes, offset, archive_size))
            break;

        TarHeader header;
        memcpy(&header, archive_bytes + offset, sizeof(header));

        char entry_path[PATH_MAX] = {0};
        if (read_tar_entry_path(&header, entry_path, sizeof(entry_path)) != Ok) {
            deallocate(archive_heap);
            log_err("Invalid archive entry path in '%s'", archive_path);
            return Err;
        }

        const char typeflag = (header.typeflag == '\0') ? '0' : header.typeflag;
        const usize data_size = (usize)parse_octal_field(header.size, sizeof(header.size));
        const usize data_offset = offset + TAR_BLOCK_SIZE;
        if (data_offset + data_size > archive_size) {
            deallocate(archive_heap);
            log_err("Archive '%s' is truncated", archive_path);
            return Err;
        }

        if (typeflag == '5') {
            if (add_archive_entry(entry_path, 0, 0, True) != Ok) {
                deallocate(archive_heap);
                return Err;
            }
        } else if (typeflag == '0') {
            if (add_archive_entry(entry_path, data_offset, data_size, False) != Ok) {
                deallocate(archive_heap);
                return Err;
            }
        }

        const usize aligned_size = ((data_size + (TAR_BLOCK_SIZE - 1)) / TAR_BLOCK_SIZE) * TAR_BLOCK_SIZE;
        offset = data_offset + aligned_size;
    }

    vfs_state.archive_heap = archive_heap;
    vfs_state.archive_bytes = archive_bytes;
    vfs_state.archive_size = archive_size;
    return Ok;
}

void vfs_unmount(void) {
    if (vfs_state.archive_heap.pointer)
        deallocate(vfs_state.archive_heap);
    if (vfs_state.entries_heap.pointer)
        deallocate(vfs_state.entries_heap);

    memset(&vfs_state, 0, sizeof(vfs_state));
}

result vfs_mount_project(const char *project_path) {
    if (!project_path || project_path[0] == '\0')
        return Err;

    vfs_unmount();

    if (snprintf(vfs_state.source_path, sizeof(vfs_state.source_path), "%s", project_path) >= (int)sizeof(vfs_state.source_path))
        return Err;

    struct stat st = {0};
    if (stat(project_path, &st) != 0)
        return Err;

    if (S_ISDIR(st.st_mode)) {
        vfs_state.mode = VfsModeDisk;
        return Ok;
    }

    if (!S_ISREG(st.st_mode) || !has_targame_extension(project_path))
        return Err;

    vfs_state.mode = VfsModeArchive;
    if (mount_archive_file(project_path) != Ok) {
        vfs_unmount();
        return Err;
    }

    return Ok;
}

boolean vfs_is_archive_mode(void) {
    return vfs_state.mode == VfsModeArchive ? True : False;
}

result vfs_resolve_path(const char *base, const char *path, char *out_path, usize out_size) {
    if (!base || !path || !out_path || out_size == 0)
        return Err;

    if (vfs_state.mode != VfsModeArchive) {
        if (is_absolute_path(path)) {
            if (snprintf(out_path, out_size, "%s", path) >= (int)out_size)
                return Err;
            return Ok;
        }

        if (base[0] == '\0') {
            if (snprintf(out_path, out_size, "%s", path) >= (int)out_size)
                return Err;
            return Ok;
        }

        if (snprintf(out_path, out_size, "%s/%s", base, path) >= (int)out_size)
            return Err;

        return Ok;
    }

    char normalized_base[PATH_MAX] = {0};
    char normalized_path[PATH_MAX] = {0};

    if (normalize_path(base, normalized_base, sizeof(normalized_base)) != Ok)
        return Err;
    if (normalize_path(path, normalized_path, sizeof(normalized_path)) != Ok)
        return Err;

    if (strcmp(normalized_base, vfs_state.source_path) == 0)
        normalized_base[0] = '\0';

    if (is_absolute_path(path)) {
        while (normalized_path[0] == '/') {
            memmove(normalized_path, normalized_path + 1, strlen(normalized_path));
        }
    }

    if (normalized_base[0] == '\0') {
        if (snprintf(out_path, out_size, "%s", normalized_path) >= (int)out_size)
            return Err;
    } else if (normalized_path[0] == '\0') {
        if (snprintf(out_path, out_size, "%s", normalized_base) >= (int)out_size)
            return Err;
    } else {
        if (snprintf(out_path, out_size, "%s/%s", normalized_base, normalized_path) >= (int)out_size)
            return Err;
    }

    return normalize_path(out_path, out_path, out_size);
}

boolean vfs_file_exists(const char *path) {
    if (!path || path[0] == '\0')
        return False;

    if (vfs_state.mode != VfsModeArchive)
        return access(path, F_OK) == 0 ? True : False;

    return find_archive_entry_index(path) >= 0 ? True : False;
}

result vfs_read_file_bytes(const char *path, Heap *out_bytes) {
    if (!path || !out_bytes)
        return Err;

    *out_bytes = NullHeap;

    if (vfs_state.mode != VfsModeArchive)
        return read_disk_file_bytes(path, out_bytes);

    const int entry_index = find_archive_entry_index(path);
    if (entry_index < 0)
        return Err;

    const VfsEntry *entry = &vfs_state.entries[entry_index];
    if (entry->is_directory)
        return Err;

    Heap bytes_heap = allocate(entry->data_size > 0 ? entry->data_size : 1, sizeof(unsigned char));
    unsigned char *bytes = (unsigned char *)bytes_heap.pointer;
    if (!bytes)
        return Err;

    if (entry->data_size > 0)
        memcpy(bytes, vfs_state.archive_bytes + entry->data_offset, entry->data_size);

    *out_bytes = bytes_heap;
    return Ok;
}

result vfs_read_file_text(const char *path, Heap *out_text) {
    if (!path || !out_text)
        return Err;

    *out_text = NullHeap;

    Heap bytes_heap = NullHeap;
    if (vfs_read_file_bytes(path, &bytes_heap) != Ok)
        return Err;

    const usize text_size = bytes_heap.size;
    Heap text_heap = allocate(text_size + 1, sizeof(char));
    char *text = (char *)text_heap.pointer;
    if (!text) {
        deallocate(bytes_heap);
        return Err;
    }

    if (text_size > 0)
        memcpy(text, bytes_heap.pointer, text_size);
    text[text_size] = '\0';

    deallocate(bytes_heap);
    *out_text = text_heap;
    return Ok;
}

result vfs_parse_toml_file(const char *path, toml_result_t *out_parsed) {
    if (!path || !out_parsed)
        return Err;

    Heap text_heap = NullHeap;
    if (vfs_read_file_text(path, &text_heap) != Ok)
        return Err;

    const char *text = (const char *)text_heap.pointer;
    const usize text_length = text_heap.size > 0 ? text_heap.size - 1 : 0;
    toml_result_t parsed = toml_parse(text, (int)text_length);
    deallocate(text_heap);

    if (!parsed.ok) {
        toml_free(parsed);
        return Err;
    }

    *out_parsed = parsed;
    return Ok;
}

static result count_conf_files_recursive(const char *directory_path, usize *out_count) {
    DIR *directory = opendir(directory_path);
    if (!directory)
        return Err;

    struct dirent *entry = Null;
    while ((entry = readdir(directory)) != Null) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char child_path[PATH_MAX] = {0};
        if (snprintf(child_path, sizeof(child_path), "%s/%s", directory_path, entry->d_name) >= (int)sizeof(child_path)) {
            closedir(directory);
            return Err;
        }

        struct stat st = {0};
        if (lstat(child_path, &st) != 0) {
            closedir(directory);
            return Err;
        }

        if (S_ISDIR(st.st_mode)) {
            if (count_conf_files_recursive(child_path, out_count) != Ok) {
                closedir(directory);
                return Err;
            }
            continue;
        }

        if (!S_ISREG(st.st_mode))
            continue;

        const char *dot = strrchr(entry->d_name, '.');
        if (dot && strcmp(dot, ".conf") == 0)
            (*out_count)++;
    }

    closedir(directory);
    return Ok;
}

result vfs_count_conf_files(usize *out_conf_count) {
    if (!out_conf_count)
        return Err;

    *out_conf_count = 0;

    if (vfs_state.mode != VfsModeArchive)
        return count_conf_files_recursive(vfs_state.source_path, out_conf_count);

    for (usize index = 0; index < vfs_state.entry_count; ++index) {
        const VfsEntry *entry = &vfs_state.entries[index];
        if (entry->is_directory)
            continue;

        const char *dot = strrchr(entry->path, '.');
        if (dot && strcmp(dot, ".conf") == 0)
            (*out_conf_count)++;
    }

    return Ok;
}

static const char *path_extension(const char *path) {
    const char *dot = path ? strrchr(path, '.') : Null;
    return dot ? dot : "";
}

result vfs_load_texture(const char *path, Texture2D *out_texture) {
    if (!path || !out_texture)
        return Err;

    if (vfs_state.mode != VfsModeArchive) {
        Texture2D texture = LoadTexture(path);
        if (texture.id == 0)
            return Err;
        *out_texture = texture;
        return Ok;
    }

    Heap bytes = NullHeap;
    if (vfs_read_file_bytes(path, &bytes) != Ok)
        return Err;

    Image image = LoadImageFromMemory(path_extension(path), (const unsigned char *)bytes.pointer, (int)bytes.size);
    deallocate(bytes);
    if (!image.data)
        return Err;

    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (texture.id == 0)
        return Err;

    *out_texture = texture;
    return Ok;
}

result vfs_load_sound(const char *path, Sound *out_sound) {
    if (!path || !out_sound)
        return Err;

    if (vfs_state.mode != VfsModeArchive) {
        Sound sound = LoadSound(path);
        if (sound.frameCount == 0)
            return Err;
        *out_sound = sound;
        return Ok;
    }

    Heap bytes = NullHeap;
    if (vfs_read_file_bytes(path, &bytes) != Ok)
        return Err;

    Wave wave = LoadWaveFromMemory(path_extension(path), (const unsigned char *)bytes.pointer, (int)bytes.size);
    deallocate(bytes);
    if (!wave.data)
        return Err;

    Sound sound = LoadSoundFromWave(wave);
    UnloadWave(wave);
    if (sound.frameCount == 0)
        return Err;

    *out_sound = sound;
    return Ok;
}

result vfs_load_music(const char *path, Music *out_music) {
    if (!path || !out_music)
        return Err;

    if (vfs_state.mode != VfsModeArchive) {
        Music music = LoadMusicStream(path);
        if (!music.ctxData)
            return Err;
        *out_music = music;
        return Ok;
    }

    Heap bytes = NullHeap;
    if (vfs_read_file_bytes(path, &bytes) != Ok)
        return Err;

    Music music = LoadMusicStreamFromMemory(path_extension(path), (const unsigned char *)bytes.pointer, (int)bytes.size);
    deallocate(bytes);
    if (!music.ctxData)
        return Err;

    *out_music = music;
    return Ok;
}