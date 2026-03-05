#ifndef VfsH
#define VfsH

#include "common.h"

#include "raylib.h"
#include "tomlc17.h"

result vfs_mount_project(const char *project_path);
void vfs_unmount(void);

boolean vfs_is_archive_mode(void);

result vfs_resolve_path(const char *base, const char *path, char *out_path, usize out_size);
boolean vfs_file_exists(const char *path);

result vfs_read_file_bytes(const char *path, Heap *out_bytes);
result vfs_read_file_text(const char *path, Heap *out_text);
result vfs_parse_toml_file(const char *path, toml_result_t *out_parsed);

result vfs_count_conf_files(usize *out_conf_count);

typedef result (*VfsFileIterator)(const char *path, void *user_data);
result vfs_for_each_file_with_suffix(const char *base_path, const char *suffix, VfsFileIterator iterator, void *user_data);

result vfs_load_texture(const char *path, Texture2D *out_texture);
result vfs_load_sound(const char *path, Sound *out_sound);
result vfs_load_music(const char *path, Music *out_music);
void vfs_unload_music(Music *music);

#endif // VFS_H
