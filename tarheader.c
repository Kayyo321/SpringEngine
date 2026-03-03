#include "tarheader.h"

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
