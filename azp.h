/*
 * library for working with 7,62HC .AZP archives
 *
 * Licenced under GPLv3
 *
 * Bugs:
 * 15k allocs on big files and 100mb alloc'd, reduce it
*/
#ifndef _AZP_H_
#define _AZP_H_

#include <stdint.h>
#include <stdbool.h>

#define MAX_FILENAME 255

/*
 * Archive header structure
 * union for easy writing during compress
 */
typedef union azpHeader_t {
    struct {
        uint32_t magic; // magic header { 'A' , 'Z' , 'P' , 0x01 }
        uint32_t data_offset;
        uint32_t version; // set from AZP_HEADER preprocessor define
        uint32_t file_count;
    } fields;
    uint32_t data[4];
} azpHeader_t;

/*
 * TOC entry structure
 */
typedef struct azpEntry_t {
    size_t offset; // offset from start of file
    size_t compressed_size;
    size_t uncompressed_size;
    uint8_t filename_length; // lenght of filename
    char filename[MAX_FILENAME];
} azpEntry_t;

/*
 * Checks if the file is a valid AZP archive and fills in the header
 * header - empty header structure
 * archive - pointer to opened file handle
 * Returns true if valid, false if invalid
 */
bool azp_check_header(azpHeader_t *header, uint8_t *archive, size_t archive_sz);

/*
 * Gets the file list inside of archive
 * header - alpeady filled AZP header
 * archive - pointer to archive data
 * returns an array of all entries, count = header.file_count, NULL on error
 * MUST BE FREE()'d AFTER USE!!
 */
azpEntry_t *azp_get_file_list(azpHeader_t *header, uint8_t *restrict archive, size_t archive_sz);

/*
 * Extracts all files from archive
 * header - filled azp header
 * root - filled start of azp entries array
 * archive - pointer to archive data
 * returns true if ok, false if not
 */
bool azp_extract_all(const azpHeader_t *header, const azpEntry_t *root, uint8_t *archive, size_t archive_sz);

/*
 * Extracts a single file by its index nr. from archive
 * header
 * root
 * index - nr. of file
 * archive
 * returns 0 if ok, zlib errors if not
 */
int azp_extract_file(const azpEntry_t *root, const uint32_t index, const uint8_t *restrict archive, const size_t archive_sz);

/*
 * Generates the TOC filelist from passed parameters
 * Compressed size and offset not filled in!
 * returns TOC entry root or NULL if something wrong
 */
azpEntry_t *azp_make_file_list(azpHeader_t *header, char **file_list, size_t file_count);

/*
 * Compresses all the files listed in the TOC entries
 * header - filled header
 * root
 * filename - output filename
 * returns 0 if OK
 */
int azp_compress_files(const azpHeader_t *header, azpEntry_t *root, const char *filename);

/*
 * Compresses a file to filename.tmp
 * MANGLES FILENAMES TO FILENAME.TMP!!
 * filename - filename
 * filesize - returns filesize there
 * returns 0 if OK, zlib errors if not
 */
int azp_compress_file(char *filename, size_t *filesize);

#endif
