#ifndef _AZP_H_
#define _AZP_H_

#include <stdint.h>
#include <limits.h>
#include "strlist.h"

#define CIPHER_KEY 0xF69DA025
#define AZP_VERSION 0x00000006
#define CHUNK 16384

/*
 * Archive header structure
 * union for easy writing during compress
 */
typedef union azpHeader {
	struct {
		uint32_t magic; // magic header { 'A' , 'Z' , 'P' , 0x01 }
		uint32_t data_offset;
		uint32_t version; // set from AZP_HEADER preprocessor define
		uint32_t file_count;
	} fields;
	uint32_t data[4];
} azpHeader;

/*
 * TOC entry structure
 */
typedef struct azpEntry {
	uint32_t filename_length; // lenght of filename
    char filename[NAME_MAX];
    uint32_t offset; // offset from start of file
    uint32_t compressed_size;
    uint32_t uncompressed_size;
} azpEntry;

/*
 * Checks if the file is a valid AZP archive and fills in the header
 * header - empty header structure
 * archive - pointer to opened file handle
 * Returns 0 if valid, -1 if invalid
 */
int azpCheckHeader(azpHeader *header, FILE *archive);

/*
 * AZP cipher function
 * data - ciphered data
 * len - data lenght
 * key - cipher key
 * returns array to deciphered data or NULL if error
 */
uint8_t *azpCipher(uint8_t *data, uint32_t len, uint32_t *key);

/*
 * Gets the file list inside of archive
 * header - already filled AZP header
 * archive - pointer to opened file handle
 * returns an array of all entries, count = header.file_count
 */
azpEntry *azpGetFileList(azpHeader *header, FILE *archive);

/*
 * Lists all entries with sizes to stdout
 * root - start of the entry array
 * header - filled archive header structure
 */
void azpListEntries(azpHeader *header, azpEntry *root);

/*
 * Extracts all files from archive
 * header - filled azp header
 * root - filled start of azp entries array
 * archive - pointer to azp archive
 * returns 0 if ok, zlib errors if not
 */
int azpExtractAll(azpHeader *header, azpEntry *root, FILE *archive);

/*
 * Extracts a single file by its index nr. from archive
 * header
 * root
 * i - nr. of file
 * archive
 * returns 0 if ok, zlib errors if not
 */
int azpExtractFile(azpHeader *header, azpEntry *root, int i, FILE *archive);

/*
 * Generates the TOC filelist from passed parameters
 * Compressed size and offset not filled in!
 * returns TOC entry root or NULL if something wrong
 *
 * First entry ignored! (list[0])
 *
 */
azpEntry *azpMakeFileList(azpHeader *header, sStrList *list);

/*
 * Compresses all the files listed in the TOC entries
 * header - filled header
 * root
 * filename - output filename
 * returns 0 if OK
 */
int azpCompressFiles(azpHeader *header, azpEntry *root, const char *filename);

/*
 * Compresses a file to filename.tmp
 * NAMES COMPRESSED FILE FILENAMES TO FILENAME.TMP!!
 * filename - filename
 * filesize - returns filesize there
 * returns 0 if OK, zlib errors if not
 */
int azpCompressFile(char *filename, uint32_t *filesize);

#endif
