#include "tarheader.h"

#include <stdio.h>
#include <string.h>

boolean is_zero_block(const unsigned char *block, usize block_size) {
    if (!block || block_size == 0)
        return False;

    for (usize index = 0; index < block_size; ++index) {
        if (block[index] != 0)
            return False;
    }

    return True;
}

boolean is_zero_block_vfs(const unsigned char *data, usize offset, usize data_size) {
    if (!data || offset + TarBlockSize > data_size)
        return True;

    for (usize index = 0; index < TarBlockSize; ++index) {
        if (data[offset + index] != 0)
            return False;
    }

    return True;
}

unsigned long long tar_parse_octal_field(const char *field, usize field_size) {
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

result tar_read_entry_path(const TarHeader *header, char *out_path, usize out_size) {
    if (!header || !out_path || out_size == 0)
        return Err;

    char name[101] = {0};
    char prefix[156] = {0};
    memcpy(name, header->name, sizeof(header->name));
    memcpy(prefix, header->prefix, sizeof(header->prefix));

    if (prefix[0] != '\0') {
        if (snprintf(out_path, out_size, "%s/%s", prefix, name) >= (int)out_size)
            return Err;
    } else {
        if (snprintf(out_path, out_size, "%s", name) >= (int)out_size)
            return Err;
    }

    return Ok;
}
