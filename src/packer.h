#ifndef PackerH
#define PackerH

#include "common.h"

result pack_directory_to_targame(const char *source_directory, const char *archive_path);
result unpack_targame_to_directory(const char *archive_path, const char *destination_directory);

#endif // PACKER_H
