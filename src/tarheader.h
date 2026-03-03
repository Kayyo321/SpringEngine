#ifndef TarHeaderH
#define TarHeaderH

#include "common.h"

enum {
    TarBlockSize = 512,
};

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

boolean is_zero_block(const unsigned char *block, usize block_size);
boolean is_zero_block_vfs(const unsigned char *data, usize offset, usize data_size);
unsigned long long tar_parse_octal_field(const char *field, usize field_size);
result tar_read_entry_path(const TarHeader *header, char *out_path, usize out_size);

#endif //TARHEADER_H
