#ifndef PathUtilsH
#define PathUtilsH

#include "common.h"

boolean is_absolute_path(const char *path);
result join_path(const char *base, const char *path, char *out_path, usize out_size);
result ensure_directory(const char *path);
result ensure_directory_recursive(const char *path);
result parent_directory(const char *path, char *out_parent, usize out_size);
result normalize_path(const char *path, char *out_path, usize out_size);
result normalize_relative_path(const char *path, char *out_path, usize out_size);
result tar_split_path(const char *relative_path, char *out_name, usize out_name_size, char *out_prefix, usize out_prefix_size);
const char *path_extension(const char *path);
boolean path_has_extension(const char *path, const char *extension); // ("./ex.targame", ".targame") -> True
boolean path_is_targame_archive(const char *path);
boolean path_has_unsafe_components(const char *path);

#endif // PATH_UTILS_H
