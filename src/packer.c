#include "packer.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tarheader.h"
#include "path_utils.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static void write_octal_field(char *field, usize field_size, unsigned long long value) {
    if (!field || field_size == 0)
        return;

    memset(field, '0', field_size);
    if (field_size >= 2) {
        field[field_size - 1] = '\0';
        (void)snprintf(field, field_size, "%0*llo", (int)(field_size - 1), value);
    }
}

static unsigned long tar_checksum(const TarHeader *header) {
    if (!header)
        return 0;

    unsigned long sum = 0;
    const unsigned char *bytes = (const unsigned char *)header;
    for (usize index = 0; index < TarBlockSize; ++index)
        sum += (unsigned long)bytes[index];

    return sum;
}

static result write_tar_header(FILE *archive, const char *relative_path, const struct stat *st, char typeflag) {
    if (!archive || !relative_path || !st)
        return Err;

    TarHeader header;
    memset(&header, 0, sizeof(header));

    char normalized[PATH_MAX] = {0};
    if (normalize_relative_path(relative_path, normalized, sizeof(normalized)) != Ok) {
        log_err("Invalid archive entry path '%s'", relative_path);
        return Err;
    }

    char header_name[100] = {0};
    char header_prefix[155] = {0};
    if (tar_split_path(normalized, header_name, sizeof(header_name), header_prefix, sizeof(header_prefix)) != Ok) {
        log_err("Archive path too long for tar header: '%s'", normalized);
        return Err;
    }

    memcpy(header.name, header_name, strlen(header_name));
    memcpy(header.prefix, header_prefix, strlen(header_prefix));

    const unsigned long long mode = (unsigned long long)(st->st_mode & 0777);
    const unsigned long long uid = (unsigned long long)st->st_uid;
    const unsigned long long gid = (unsigned long long)st->st_gid;
    const unsigned long long size = (typeflag == '5') ? 0ULL : (unsigned long long)st->st_size;
    const unsigned long long mtime = (unsigned long long)st->st_mtime;

    write_octal_field(header.mode, sizeof(header.mode), mode);
    write_octal_field(header.uid, sizeof(header.uid), uid);
    write_octal_field(header.gid, sizeof(header.gid), gid);
    write_octal_field(header.size, sizeof(header.size), size);
    write_octal_field(header.mtime, sizeof(header.mtime), mtime);

    memset(header.chksum, ' ', sizeof(header.chksum));
    header.typeflag = typeflag;

    memcpy(header.magic, "ustar", 5);
    memcpy(header.version, "00", 2);

    const unsigned long checksum = tar_checksum(&header);
    memset(header.chksum, 0, sizeof(header.chksum));
    (void)snprintf(header.chksum, sizeof(header.chksum), "%06lo", checksum);
    header.chksum[6] = '\0';
    header.chksum[7] = ' ';

    if (fwrite(&header, 1, sizeof(header), archive) != sizeof(header)) {
        log_err("Failed to write tar header for '%s'", relative_path);
        return Err;
    }

    return Ok;
}

static result write_file_contents(FILE *archive, const char *source_path, const struct stat *st) {
    if (!archive || !source_path || !st)
        return Err;

    FILE *source = fopen(source_path, "rb");
    if (!source) {
        log_err("Failed to open source file '%s'", source_path);
        return Err;
    }

    unsigned char buffer[8192];
    unsigned long long remaining = (unsigned long long)st->st_size;

    while (remaining > 0) {
        const usize chunk_size = (remaining > sizeof(buffer)) ? sizeof(buffer) : (usize)remaining;
        const usize bytes_read = fread(buffer, 1, chunk_size, source);
        if (bytes_read != chunk_size) {
            fclose(source);
            log_err("Failed while reading source file '%s'", source_path);
            return Err;
        }

        if (fwrite(buffer, 1, bytes_read, archive) != bytes_read) {
            fclose(source);
            log_err("Failed while writing source file '%s' into archive", source_path);
            return Err;
        }

        remaining -= bytes_read;
    }

    fclose(source);

    const usize padding = (usize)((TarBlockSize - ((unsigned long long)st->st_size % TarBlockSize)) % TarBlockSize);
    if (padding > 0) {
        unsigned char zeroes[TarBlockSize] = {0};
        if (fwrite(zeroes, 1, padding, archive) != padding) {
            log_err("Failed to write tar padding for '%s'", source_path);
            return Err;
        }
    }

    return Ok;
}

static result pack_directory_recursive(FILE *archive, const char *source_root, const char *relative_path) {
    if (!archive || !source_root || !relative_path)
        return Err;

    char source_path[PATH_MAX] = {0};
    if (join_path(source_root, relative_path, source_path, sizeof(source_path)) != Ok) {
        log_err("Failed to resolve source path while packing '%s'", relative_path);
        return Err;
    }

    struct stat st = {0};
    if (lstat(source_path, &st) != 0) {
        log_err("Failed to stat '%s' while packing", source_path);
        return Err;
    }

    if (S_ISDIR(st.st_mode)) {
        char archive_dir_path[PATH_MAX] = {0};
        if (snprintf(archive_dir_path, sizeof(archive_dir_path), "%s/", relative_path) >= (int)sizeof(archive_dir_path)) {
            log_err("Directory entry path too long: '%s'", relative_path);
            return Err;
        }

        if (write_tar_header(archive, archive_dir_path, &st, '5') != Ok)
            return Err;

        DIR *directory = opendir(source_path);
        if (!directory) {
            log_err("Failed to open directory '%s'", source_path);
            return Err;
        }

        struct dirent *entry = Null;
        while ((entry = readdir(directory)) != Null) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            char child_relative_path[PATH_MAX] = {0};
            if (snprintf(child_relative_path, sizeof(child_relative_path), "%s/%s", relative_path, entry->d_name) >= (int)sizeof(child_relative_path)) {
                closedir(directory);
                log_err("Entry path too long while packing '%s/%s'", relative_path, entry->d_name);
                return Err;
            }

            if (pack_directory_recursive(archive, source_root, child_relative_path) != Ok) {
                closedir(directory);
                return Err;
            }
        }

        closedir(directory);
        return Ok;
    }

    if (S_ISREG(st.st_mode)) {
        if (write_tar_header(archive, relative_path, &st, '0') != Ok)
            return Err;

        return write_file_contents(archive, source_path, &st);
    }

    log_warn("Skipping unsupported filesystem entry '%s'", source_path);
    return Ok;
}

static result skip_file_data(FILE *archive, unsigned long long file_size) {
    if (!archive)
        return Err;

    const unsigned long long aligned_size = ((file_size + (TarBlockSize - 1)) / TarBlockSize) * TarBlockSize;
    if (fseek(archive, (long)aligned_size, SEEK_CUR) != 0)
        return Err;

    return Ok;
}

static result unpack_regular_file(FILE *archive, const char *destination_path, unsigned long long file_size) {
    if (!archive || !destination_path)
        return Err;

    char destination_parent[PATH_MAX] = {0};
    if (parent_directory(destination_path, destination_parent, sizeof(destination_parent)) == Ok) {
        if (ensure_directory_recursive(destination_parent) != Ok)
            return Err;
    }

    FILE *destination = fopen(destination_path, "wb");
    if (!destination) {
        log_err("Failed to create file '%s' while unpacking", destination_path);
        return Err;
    }

    unsigned char buffer[8192] = {0};
    unsigned long long remaining = file_size;

    while (remaining > 0) {
        const usize chunk_size = (remaining > sizeof(buffer)) ? sizeof(buffer) : (usize)remaining;
        const usize bytes_read = fread(buffer, 1, chunk_size, archive);
        if (bytes_read != chunk_size) {
            fclose(destination);
            log_err("Archive truncated while extracting '%s'", destination_path);
            return Err;
        }

        if (fwrite(buffer, 1, bytes_read, destination) != bytes_read) {
            fclose(destination);
            log_err("Failed writing extracted file '%s'", destination_path);
            return Err;
        }

        remaining -= bytes_read;
    }

    fclose(destination);

    const usize padding = (usize)((TarBlockSize - (file_size % TarBlockSize)) % TarBlockSize);
    if (padding > 0) {
        if (fseek(archive, (long)padding, SEEK_CUR) != 0) {
            log_err("Failed to skip padding while extracting '%s'", destination_path);
            return Err;
        }
    }

    return Ok;
}

result pack_directory_to_targame(const char *source_directory, const char *archive_path) {
    if (!source_directory || source_directory[0] == '\0' || !archive_path || archive_path[0] == '\0')
        return Err;

    struct stat source_stat = {0};
    if (stat(source_directory, &source_stat) != 0 || !S_ISDIR(source_stat.st_mode)) {
        log_err("Source directory does not exist or is not a directory: '%s'", source_directory);
        return Err;
    }

    char archive_parent[PATH_MAX] = {0};
    if (parent_directory(archive_path, archive_parent, sizeof(archive_parent)) == Ok) {
        if (ensure_directory_recursive(archive_parent) != Ok)
            return Err;
    }

    FILE *archive = fopen(archive_path, "wb");
    if (!archive) {
        log_err("Failed to create archive '%s'", archive_path);
        return Err;
    }

    DIR *root = opendir(source_directory);
    if (!root) {
        fclose(archive);
        log_err("Failed to open source directory '%s'", source_directory);
        return Err;
    }

    struct dirent *entry = Null;
    while ((entry = readdir(root)) != Null) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        if (pack_directory_recursive(archive, source_directory, entry->d_name) != Ok) {
            closedir(root);
            fclose(archive);
            return Err;
        }
    }

    closedir(root);

    unsigned char zero_block[TarBlockSize] = {0};
    if (fwrite(zero_block, 1, sizeof(zero_block), archive) != sizeof(zero_block)
        || fwrite(zero_block, 1, sizeof(zero_block), archive) != sizeof(zero_block)) {
        fclose(archive);
        log_err("Failed to write tar end-of-archive markers for '%s'", archive_path);
        return Err;
    }

    if (fclose(archive) != 0) {
        log_err("Failed to close archive '%s'", archive_path);
        return Err;
    }

    log_msg("Packed '%s' -> '%s'", source_directory, archive_path);
    return Ok;
}

result unpack_targame_to_directory(const char *archive_path, const char *destination_directory) {
    if (!archive_path || archive_path[0] == '\0' || !destination_directory || destination_directory[0] == '\0')
        return Err;

    if (ensure_directory_recursive(destination_directory) != Ok)
        return Err;

    FILE *archive = fopen(archive_path, "rb");
    if (!archive) {
        log_err("Failed to open archive '%s'", archive_path);
        return Err;
    }

    while (True) {
        TarHeader header;
        const usize bytes_read = fread(&header, 1, sizeof(header), archive);
        if (bytes_read == 0)
            break;

        if (bytes_read != sizeof(header)) {
            fclose(archive);
            log_err("Invalid or truncated archive '%s'", archive_path);
            return Err;
        }

        if (is_zero_block((const unsigned char *)&header, sizeof(header)))
            break;

        char entry_path[PATH_MAX] = {0};
        if (tar_read_entry_path(&header, entry_path, sizeof(entry_path)) != Ok) {
            fclose(archive);
            log_err("Failed to decode archive entry path in '%s'", archive_path);
            return Err;
        }

        if (path_has_unsafe_components(entry_path)) {
            fclose(archive);
            log_err("Refusing to extract unsafe archive entry path '%s'", entry_path);
            return Err;
        }

        char destination_path[PATH_MAX] = {0};
        if (join_path(destination_directory, entry_path, destination_path, sizeof(destination_path)) != Ok) {
            fclose(archive);
            log_err("Destination path too long while unpacking '%s'", entry_path);
            return Err;
        }

        const unsigned long long file_size = tar_parse_octal_field(header.size, sizeof(header.size));
        const char typeflag = (header.typeflag == '\0') ? '0' : header.typeflag;

        if (typeflag == '5') {
            if (ensure_directory_recursive(destination_path) != Ok) {
                fclose(archive);
                return Err;
            }
        } else if (typeflag == '0') {
            if (unpack_regular_file(archive, destination_path, file_size) != Ok) {
                fclose(archive);
                return Err;
            }
            continue;
        } else {
            log_warn("Skipping unsupported tar entry type '%c' for '%s'", typeflag, entry_path);
        }

        if (skip_file_data(archive, file_size) != Ok) {
            fclose(archive);
            log_err("Failed while skipping archive data for '%s'", entry_path);
            return Err;
        }
    }

    fclose(archive);
    log_msg("Unpacked '%s' -> '%s'", archive_path, destination_directory);
    return Ok;
}
