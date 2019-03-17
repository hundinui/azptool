#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <zlib.h>
#include <assert.h>
#include <sys/stat.h>
#include "azp.h"

const uint32_t azpHeaderMagic = 0x01505A41;
#define CHUNK_SZ 16384
#define CIPHER_KEY 0xF69DA025
#define AZP_VERSION 0x00000006

static uint8_t *azp_cipher(const uint8_t *restrict data, const size_t len, uint32_t *key);

bool azp_check_header(azpHeader_t *header, uint8_t *archive, size_t archive_sz) {
    if(archive == NULL || archive_sz < sizeof(azpHeader_t)) {
        return false;
    }
    memcpy(header->data, archive, sizeof(azpHeader_t));
    if(azpHeaderMagic != header->fields.magic) {
        return false;
    }
    return true;
}

/*
 *       E X T R A C T I O N    F U N C S
 */

azpEntry_t *azp_get_file_list(azpHeader_t *header, uint8_t *restrict archive, size_t archive_sz) {
    if(archive == NULL || archive_sz < (header->fields.file_count * sizeof(azpEntry_t))) {
        return NULL;
    }
    /* Allocate entry TOC */
    azpEntry_t *out = calloc(header->fields.file_count, sizeof(azpEntry_t));
    if(out == NULL) {
        return NULL;
    }
    uint32_t addr = 0;
    uint32_t next_key = CIPHER_KEY;

    uint8_t *toc = archive + sizeof(azpHeader_t);
    
    for(uint32_t i = 0; i < header->fields.file_count; ++i) {
        uint8_t *data_out = NULL;

        /* Decode filename lenght */
        data_out = azp_cipher(toc + addr, 4, &next_key);
        out[i].filename_length = (data_out[3] << 24) | (data_out[2] << 16) | (data_out[1] << 8) | (data_out[0]);;
        addr += 4;

        /* Decode filename */
        data_out = azp_cipher(toc + addr, out[i].filename_length, &next_key);
        snprintf(out[i].filename, out[i].filename_length + 1, "%s", data_out);
        addr += out[i].filename_length;

        /* Decode data offset */
        data_out = azp_cipher(toc + addr, 4, &next_key);
        out[i].offset = (uint32_t)(data_out[3] << 24) | (data_out[2] << 16) | (data_out[1] << 8) | (data_out[0]);
        addr += 4;

        /* Decode compressed size */
        data_out = azp_cipher(toc + addr, 4, &next_key);
        out[i].compressed_size = (data_out[3] << 24) | (data_out[2] << 16) | (data_out[1] << 8) | (data_out[0]);
        addr += 4;

        /* Decode uncompressed size */
        data_out = azp_cipher(toc + addr, 4, &next_key);
        out[i].uncompressed_size = (data_out[3] << 24) | (data_out[2] << 16) | (data_out[1] << 8) | (data_out[0]);
        addr += 4;
    }
    return out;
}

bool azp_extract_all(const azpHeader_t *header, const azpEntry_t *root, uint8_t *archive, size_t archive_sz) {
    for(uint32_t i = 0; i < header->fields.file_count; ++i) {
        printf("%6u/%-6u Extracting file %s (%zu bytes)...\n", 
               i + 1, header->fields.file_count, root[i].filename, root[i].uncompressed_size);
        if(azp_extract_file(root, i, archive, archive_sz) != 0) {
            return false;
        }
    }
    return true;
}

/*
 *          C O M P R E S S I O N   F U N C S
 */

azpEntry_t *azp_make_file_list(azpHeader_t *header, char **file_list, size_t file_count) {
    struct stat st;

    /* Fill known values of header */
    header->fields.magic = azpHeaderMagic;
    header->fields.version = AZP_VERSION;
    header->fields.file_count = file_count;

    /* Check if folders
     * Probably a better way but fugg
     */
    uint32_t folders = 0;
    for(uint32_t i = 0; i < header->fields.file_count; ++i) {
        int stat_ret = stat(file_list[i], &st);
        if(stat_ret != 0) {
            return NULL;
        }
        /* If is folder then dissapear the list entry */
        if(!S_ISREG(st.st_mode)) {
            ++folders;
        }
    }
    header->fields.file_count -= folders;

    /* name_len, offset, compressed size and uncompressed size */
    uint32_t offset = (sizeof(uint32_t) * 4) * header->fields.file_count;
    azpEntry_t *out = calloc(header->fields.file_count, sizeof(azpEntry_t));

    for(uint32_t i = 0; i < header->fields.file_count; ++i) {
        /* Set filename and filename lenght */
        sprintf(out[i].filename, "%s", file_list[i]);
        out[i].filename_length = strlen(out[i].filename);

        /* Increment offset by filename len */
        offset += out[i].filename_length;

        /* Set uncompressed size */
        int stat_ret = stat(out[i].filename, &st);
        if(stat_ret != 0) {
            perror("Error reading file");
            return NULL;
        }
        out[i].uncompressed_size = st.st_size;
    }
    header->fields.data_offset = offset + 16;
#ifdef DEBUG
    printf("offset: 0x%08X\n", header->fields.data_offset);
#endif

    return out;
}

int azp_compress_files(const azpHeader_t *header, azpEntry_t *root, const char *filename) {
    const char *errWritingTOC = "Error writing TOC\n";
    FILE *outfile = fopen(filename, "wb");
    if(outfile == NULL) {
        perror("Error opening file");
        return -1;
    }

    /* Write header */
    if(fwrite(header, sizeof(uint32_t), 4, outfile) != 4) {
        perror("Error writing header");
        goto fail_outfile;
    }

    uint32_t next_key = CIPHER_KEY;
    uint32_t offset = header->fields.data_offset;

    /* Write TOC */
    for(uint32_t i = 0; i < header->fields.file_count; ++i) {
        printf("%6u/%-6u Compressing file %s (%zu bytes)...\n", 
               i+1, header->fields.file_count, root[i].filename, root[i].uncompressed_size);

        /* Compress file to disk */
        if(azp_compress_file(root[i].filename, &root[i].compressed_size) != 0) {
            goto fail_outfile;
        }

        uint8_t *cipher_data;

        /* Filename len */
        cipher_data = azp_cipher((uint8_t*)&root[i].filename_length, 4, &next_key);
        if(fwrite(cipher_data, sizeof(uint32_t), 1, outfile) != 1) {
            perror(errWritingTOC);
            goto fail_outfile;
        }

        /* Filename */
        cipher_data = azp_cipher((uint8_t*)root[i].filename, root[i].filename_length, &next_key);
        if(fwrite(cipher_data, sizeof(char), root[i].filename_length, outfile) != root[i].filename_length) {
            perror(errWritingTOC);
            goto fail_outfile;
        }
        root[i].offset = offset;

        /* Offset */
        cipher_data = azp_cipher((uint8_t*)&root[i].offset, 4, &next_key);
        if(fwrite(cipher_data, sizeof(uint32_t), 1, outfile) != 1) {
            perror(errWritingTOC);
            goto fail_outfile;
        }

        /* Compressed size */
        cipher_data = azp_cipher((uint8_t*)&root[i].compressed_size, 4, &next_key);
        if(fwrite(cipher_data, sizeof(uint32_t), 1, outfile) != 1) {
            perror(errWritingTOC);
            goto fail_outfile;
        }
        offset += root[i].compressed_size;

        /* Uncompressed size */
        cipher_data = azp_cipher((uint8_t*)&root[i].uncompressed_size, 4, &next_key);
        if(fwrite(cipher_data, sizeof(uint32_t), 1, outfile) != 1) {
            perror(errWritingTOC);
            goto fail_outfile;
        }
    }

    FILE *compressed;

    /* Append all compressed files to the archive */
    for(uint32_t i = 0; i < header->fields.file_count; ++i) {
        compressed = fopen(root[i].filename, "rb");
        if(compressed == NULL) {
            perror("Error opening compressed file for reading");
            return -1;
        }

        uint8_t block[CHUNK_SZ+1];
        int chunksize = CHUNK_SZ;
        int written = 0;

        /* Append files in chunks, some might be big */
        while(written != root[i].compressed_size) {
            if(chunksize > (root[i].compressed_size - written)) {
                chunksize = (root[i].compressed_size - written);
            }
            if(fread(block, sizeof(uint8_t), chunksize, compressed) != chunksize) {
                perror("Error reading compressed archive");
                goto fail_compressed;
            }
            if(fwrite(block, sizeof(uint8_t), chunksize, outfile) != chunksize) {
                perror("Error writing compressed archive");
                goto fail_compressed;
            }
            written += chunksize;
        }

        fflush(compressed);
        fclose(compressed);
        /* Delete compressed temporary files */
        remove(root[i].filename);
        continue;

fail_compressed:
        fclose(compressed);
        goto fail_outfile;
    }

    fflush(outfile);
    fclose(outfile);

    return 0;

fail_outfile:
    fclose(outfile);
    return -1;
}

/*
 * cipher
 *
 * Huge thanks to Stanislav Bobovych for the blog post and code where this cipher code was adapted from!
 * https://stan-bobovych.com/2017/08/12/239/
 *
*/
static uint8_t *azp_cipher(const uint8_t *restrict data, const size_t len, uint32_t *key) {
    if(len == 0) {
        printf("Lenght to cipher is 0!\n");
        return NULL;
    }
    static uint8_t output[MAX_FILENAME];

    uint32_t y = 0x0000FFFF & *key;
    uint32_t x = 0x0000FFFF & (*key >> 16);

    for(uint32_t i = 0; i < len; ++i) {
        uint32_t tmp = x * y;
        x = 0x0000FFFF & tmp;
        y = 0x0000FFFF & (tmp >> 16);
        x = (x - 1) ^ y;
        y = x & 0xFF;
        x = (x & 0x0000FF00) | data[i];
        y = x ^ y;
        output[i] = y & 0x000000FF;
    }
    output[len] = '\0';
    *key = (x << 0x10) | y;
    return output;
}

/*
 * 		Z L I B     F U N C S
 */

#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#  include <fcntl.h>
#  include <io.h>
#  define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#  define SET_BINARY_MODE(file)
#endif

/*
 * Mostly from zlib zpipe.c example
 */
int azp_extract_file(const azpEntry_t *root, const uint32_t index, const uint8_t *archive, const size_t archive_sz) {
    if(archive == NULL || root == NULL) {
        return -1;
    }
    FILE *outfile;
    
    /* Lets check if its in a subfolder */
    char *slash = strchr(root[index].filename, '\\');
    if(slash != NULL) {
        /* Ugly directory name splitting */
        char dirname[32] = { '\0' };
        char path[64] = { '\0' };
        strcpy(dirname, root[index].filename);
        dirname[slash - root[index].filename] = '\0';
        sprintf(path, "./%s/%s", dirname, slash+1);
#ifdef WIN32
        mkdir(dirname);
#else
        mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif
        outfile = fopen(path, "wb");
    } else {
        outfile = fopen(root[index].filename, "wb");
    }
    if(outfile == NULL) {
        return -1;
    }
    uint8_t *data = (uint8_t*)archive + root[index].offset;
    
    int ret;
    int written = 0;
    unsigned have;
    z_stream strm;
    uint8_t out[CHUNK_SZ];
    size_t chunksize = CHUNK_SZ;

    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK) {
        fclose(outfile);
        return ret;
    }

    /* decompress until deflate stream ends or end of file */
    do {
        strm.avail_in = archive_sz;
        strm.next_in = data;
        /* run inflate() on input until output buffer not full */
        do {
            strm.avail_out = chunksize;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     /* and fall through */
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                return ret;
            }
            have = chunksize- strm.avail_out;
            if (fwrite(out, 1, have, outfile) != have || ferror(outfile)) {
                (void)inflateEnd(&strm);
                return Z_ERRNO;
            }
        } while (strm.avail_out == 0);
        /* done when inflate() says it's done */
        written += chunksize;
    } while (written < root[index].uncompressed_size);
    (void)inflateEnd(&strm);

    fflush(outfile);
    fclose(outfile);
    return 0;
}

int azp_compress_file(char *filename, size_t *filesize) {
    struct stat st;
    FILE *source = fopen(filename, "rb");
    if(source == NULL) {
        return -1;
    }
    strcat(filename, ".tmp");
    FILE *dest = fopen(filename, "wb");
    if(dest == NULL) {
        fclose(source);
        return -1;
    }

    int ret, flush;
    unsigned have;
    z_stream strm;
    unsigned char in[CHUNK_SZ];
    unsigned char out[CHUNK_SZ];

    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
    if (ret != Z_OK) {
        fclose(source);
        fclose(dest);
        return ret;
    }

    /* compress until end of file */
    do {
        strm.avail_in = fread(in, 1, CHUNK_SZ, source);
        if (ferror(source)) {
            (void)deflateEnd(&strm);
            fclose(source);
            fclose(dest);
            return Z_ERRNO;
        }
        flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = in;

        /* run deflate() on input until output buffer not full, finish
           compression if all of source has been read in */
        do {
            strm.avail_out = CHUNK_SZ;
            strm.next_out = out;
            ret = deflate(&strm, flush);    /* no bad return value */
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            have = CHUNK_SZ - strm.avail_out;
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)deflateEnd(&strm);
                return Z_ERRNO;
            }
        } while (strm.avail_out == 0);
        assert(strm.avail_in == 0);     /* all input will be used */

        /* done when last data in file processed */
    } while (flush != Z_FINISH);
    assert(ret == Z_STREAM_END);        /* stream will be complete */

    /* clean up and return */
    (void)deflateEnd(&strm);

    fflush(dest);
    fclose(source);
    fclose(dest);

    int stat_ret = stat(filename, &st);
    if(stat_ret != 0) {
        perror("Error reading filesize");
        return -1;
    }
    *filesize = st.st_size;

    return Z_OK;
}
