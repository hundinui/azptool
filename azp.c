#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <zlib.h>
#include <assert.h>
#include <sys/stat.h>
#include <limits.h>
#include "azp.h"

const uint32_t azpHeaderMagic = 0x01505A41;

/*	Check for AZP header from a file
 *	Returns relevant data into header struct
 *	Returns 0 if header OK, -1 if not
 */
int azpCheckHeader(azpHeader *header, FILE *archive) {
	/* Read header block */
	if(fread(header->data, sizeof(uint32_t), 4, archive) != 4) {
		perror("Error reading header");
		return -1;
	}

	/* Check against AZP header */
	if(azpHeaderMagic != header->fields.magic) {
		printf("Not a valid AZP archive, header mismatch!\n");
		return -1;
	}
#ifdef DEBUG
	printf("Header: 0x%08X\n", header->fields.magic);
	printf("Offset: 0x%08X\n", header->fields.data_offset);
	printf("Version: %i\n", header->fields.version);
	printf("Number of files: %i\n", header->fields.file_count);
#endif

	return 0;
}

/*
 *       E X T R A C T I O N    F U N C S
 */

const char *errReadingTOC = "Error reading TOC!";

azpEntry *azpGetFileList(azpHeader *header, FILE *archive) {
	/* Allocate entry TOC */
	azpEntry *out = calloc(header->fields.file_count, sizeof(azpEntry) + 1);
	uint32_t addr = 0;
	uint32_t next_key = CIPHER_KEY;

	/* Allocate memory for the ciphered TOC */
	uint8_t *toc = malloc(sizeof(uint8_t) * (header->fields.data_offset + 1));
	if(toc == NULL) {
		printf("Error allocating memory for header!\n");
		return NULL;
	}
	/* Read the ciphered TOC */
	if(fread(toc, sizeof(uint8_t), header->fields.data_offset, archive) != header->fields.data_offset) {
		perror(errReadingTOC);
		return NULL;
	}

	/* Loop trough all TOC entries */
	for(int i = 0; i < header->fields.file_count; ++i) {
		uint8_t *data_out = NULL;

		/* Decode filename lenght */
		data_out = azpCipher(toc + addr, 4, &next_key);
		/* Lets just splice it together here */
		out[i].filename_length = (data_out[3] << 24) | (data_out[2] << 16) | (data_out[1] << 8) | (data_out[0]);;
		addr += 4;

		/* Decode filename */
		data_out = azpCipher(toc + addr, out[i].filename_length, &next_key);
		snprintf(out[i].filename, out[i].filename_length + 1, "%s", data_out);
		addr += out[i].filename_length;

		/* Decode data offset */
		data_out = azpCipher(toc + addr, 4, &next_key);
		out[i].offset = (uint32_t)(data_out[3] << 24) | (data_out[2] << 16) | (data_out[1] << 8) | (data_out[0]);
		addr += 4;

		/* Decode compressed size */
		data_out = azpCipher(toc + addr, 4, &next_key);
		out[i].compressed_size = (data_out[3] << 24) | (data_out[2] << 16) | (data_out[1] << 8) | (data_out[0]);
		addr += 4;

		/* Decode uncompressed size */
		data_out = azpCipher(toc + addr, 4, &next_key);
		out[i].uncompressed_size = (data_out[3] << 24) | (data_out[2] << 16) | (data_out[1] << 8) | (data_out[0]);
		addr += 4;
	}
	free(toc);
	return out;
}

int azpExtractAll(azpHeader *header, azpEntry *root, FILE *archive) {
	/* Loop through the whole TOC */
	for(int i = 0; i < header->fields.file_count; ++i) {
		printf("%6i/%-6i Extracting file %s (%i bytes)...\n", i + 1, header->fields.file_count, root[i].filename, root[i].uncompressed_size);
		azpExtractFile(header, root, i, archive);
	}
	return 0;
}

/*
 *          C O M P R E S S I O N   F U N C S
 */

azpEntry *azpMakeFileList(azpHeader *header, sStrList *list) {
	struct stat st;
	sStrList *root = list->next;

	/* Fill known values of header */
	header->fields.magic = azpHeaderMagic;
	header->fields.version = AZP_VERSION;
	header->fields.file_count = strListGetCount(list);

	/* Check if folders
	 * Probably a better way but fugg
	 */
	int folders = 0;
	for(int i = 0; i < header->fields.file_count; ++i) {
		int stat_ret = stat(root->str, &st);
		if(stat_ret != 0) {
			perror("Error reading file");
			return NULL;
		}
		/* If is folder then dissapear the list entry */
		if(!S_ISREG(st.st_mode)) {
			++folders;
			root->prev->next = root->next;
			root->next->prev = root->prev;
			free(root);
			root = root->prev->next;
		} else {
			root = root->next;
		}
	}
	header->fields.file_count -= folders;

	/* name_len, offset, compressed size and uncompressed size */
	uint32_t offset = (sizeof(uint32_t) * 4) * header->fields.file_count;
	azpEntry *out = calloc(header->fields.file_count, sizeof(azpEntry) + 1);

	root = list->next;

	for(int i = 0; i < header->fields.file_count; ++i) {
		/* Set filename and filename lenght */
		sprintf(out[i].filename, "%s", root->str);
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
		root = root->next;
	}
	header->fields.data_offset = offset + 16;
#ifdef DEBUG
	printf("offset: 0x%08X\n", header->fields.data_offset);
#endif

	return out;
}

const char *errWritingTOC = "Error writing TOC\n";

int azpCompressFiles(azpHeader *header, azpEntry *root, const char *filename) {
	FILE *outfile = fopen(filename, "wb");
	if(outfile == NULL) {
		perror("Error opening file");
		return -1;
	}

	/* Write header */
	if(fwrite(header, sizeof(uint32_t), 4, outfile) != 4) {
		perror("Error writing header");
		return -1;
	}

	uint32_t next_key = CIPHER_KEY;
	uint32_t offset = header->fields.data_offset;

	/* Write TOC */
	for(int i = 0; i < header->fields.file_count; ++i) {
		printf("%6i/%-6i Compressing file %s (%i bytes)...\n", i+1, header->fields.file_count, root[i].filename, root[i].uncompressed_size);

		/* Compress file to disk */
		azpCompressFile(root[i].filename, &root[i].compressed_size);

		uint8_t *cipher_data;

		/* Filename len */
		cipher_data = azpCipher((uint8_t*)&root[i].filename_length, 4, &next_key);
		if(fwrite(cipher_data, sizeof(uint32_t), 1, outfile) != 1) {
			perror(errWritingTOC);
			return -1;
		}

		/* Filename */
		cipher_data = azpCipher((uint8_t*)root[i].filename, root[i].filename_length, &next_key);
		if(fwrite(cipher_data, sizeof(char), root[i].filename_length, outfile) != root[i].filename_length) {
			perror(errWritingTOC);
			return -1;
		}
		root[i].offset = offset;

		/* Offset */
		cipher_data = azpCipher((uint8_t*)&root[i].offset, 4, &next_key);
		if(fwrite(cipher_data, sizeof(uint32_t), 1, outfile) != 1) {
			perror(errWritingTOC);
			return -1;
		}

		/* Compressed size */
		cipher_data = azpCipher((uint8_t*)&root[i].compressed_size, 4, &next_key);
		if(fwrite(cipher_data, sizeof(uint32_t), 1, outfile) != 1) {
			perror(errWritingTOC);
			return -1;
		}
		offset += root[i].compressed_size;

		/* Uncompressed size */
		cipher_data = azpCipher((uint8_t*)&root[i].uncompressed_size, 4, &next_key);
		if(fwrite(cipher_data, sizeof(uint32_t), 1, outfile) != 1) {
			perror(errWritingTOC);
			return -1;
		}
	}

	FILE *compressed;

	/* Append all compressed files to the archive */
	for(int i = 0; i < header->fields.file_count; ++i) {
		compressed = fopen(root[i].filename, "rb");
		if(compressed == NULL) {
			perror("Error opening compressed file for reading");
			return -1;
		}

		uint8_t block[CHUNK+1];
		int chunksize = CHUNK;
		int written = 0;

		/* Append files in chunks, some might be big */
		while(written != root[i].compressed_size) {
			if(chunksize > (root[i].compressed_size - written)) {
				chunksize = (root[i].compressed_size - written);
			}
			if(fread(block, sizeof(uint8_t), chunksize, compressed) != chunksize) {
				perror("Error reading compressed archive");
				return -1;
			}
			if(fwrite(block, sizeof(uint8_t), chunksize, outfile) != chunksize) {
				perror("Error writing compressed archive");
				return -1;
			}
			written += chunksize;
		}

		fflush(compressed);
		fclose(compressed);
		/* Delete compressed temporary files */
		remove(root[i].filename);
	}

	fflush(outfile);
	fclose(outfile);

	return 0;
}

/* Ugly size units calculation */
const char *sizeUnit(int bytes, int *bytesdiv) {
	if(bytes > 1024) {
		if(bytes > 1024*1024) {
			if(bytes > 1024*1024*1024) {
				*bytesdiv = bytes / (1024*1024*1024);
				return "GiB";
			}
			*bytesdiv = bytes / (1024*1024);
			return "MiB";
		}
		*bytesdiv = bytes / 1024;
		return "KiB";
	}
	*bytesdiv = bytes;
	return "B";
}

/*
 * Ugly stuff for list printing
 */
void azpListEntries(azpHeader *header, azpEntry *root) {
	int sum_compressed = 0, sum_uncompressed = 0;
	const char *unit_comp;
	const char *unit_uncomp;
	printf("|--------------------------------------------------------------------|\n" \
		   "|      | Packed  | Unpacked |                     |                  |\n" \
		   "|  Nr. |  Size   | Size     |      Filename       |       Offset     |\n" \
		   "|--------------------------------------------------------------------|\n");
	for(int i = 0; i < header->fields.file_count; ++i) {
		sum_compressed += root[i].compressed_size;
		sum_uncompressed += root[i].uncompressed_size;
		int compressed_size = -1;
		int uncompressed_size = -1;
		unit_comp = sizeUnit(root[i].compressed_size, &compressed_size);
		unit_uncomp = sizeUnit(root[i].uncompressed_size, &uncompressed_size);
		printf("| %i\t%3i %-4s-  %-4i %-s\t %-15s\t0x%08X   |\n", i+1, compressed_size, unit_comp, uncompressed_size, unit_uncomp,root[i].filename, root[i].offset);
	}
	printf("|--------------------------------------------------------------------|\n");
	unit_comp = sizeUnit(sum_compressed, &sum_compressed);
	unit_uncomp = sizeUnit(sum_uncompressed, &sum_uncompressed);
	printf("\nUncompressed filesize: %i %s\nCompressed filesize: %i %s\nCompression ratio: %04f\n", sum_uncompressed, unit_uncomp, sum_compressed, unit_comp, (float)sum_compressed/sum_uncompressed);
}

/*
 * cipher
 *
 * Huge thanks to Stanislav Bobovych for the blog post and code where this cipher code was adapted from!
 * https://stan-bobovych.com/2017/08/12/239/
 *
*/
uint8_t *azpCipher(uint8_t *data, uint32_t len, uint32_t *key) {
	if(len <= 0) {
		printf("Lenght to cipher is <= 0!\n");
		return NULL;
	}
	static uint8_t output[NAME_MAX];

	uint32_t y = 0x0000FFFF & *key;
	uint32_t x = 0x0000FFFF & (*key >> 16);

	int i;
	for(i = 0; i < len; ++i) {
		uint32_t tmp = x * y;
		x = 0x0000FFFF & tmp;
		y = 0x0000FFFF & (tmp >> 16);
		x = (x - 1) ^ y;
		y = x & 0xFF;
		x = (x & 0x0000FF00) | data[i];
		y = x ^ y;
		output[i] = y & 0x000000FF;
	}
	output[i+1] = '\0';
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
int azpExtractFile(azpHeader *header, azpEntry *root, int i, FILE *archive) {
	FILE *outfile;
	/* Lets check if its in a subfolder */
	char *slash = strchr(root[i].filename, '\\');
	if(slash != NULL) {
	/* Ugly directory name splitting */
		char dirname[32] = { '\0' };
		char path[64] = { '\0' };
		strcpy(dirname, root[i].filename);
		dirname[slash - root[i].filename] = '\0';
		sprintf(path, "./%s/%s", dirname, slash+1);
#ifdef WIN32
		mkdir(dirname);
#else
		mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif
		outfile = fopen(path, "wb");
	} else {
		outfile = fopen(root[i].filename, "wb");
	}
	if(outfile == NULL) {
		printf("Error opening file %s for writing!\n", root[i].filename);
		return -1;
	}
	fseek(archive, root[i].offset, SEEK_SET);

	int ret;
	int written = 0;
	unsigned have;
	z_stream strm;
	uint8_t in[CHUNK];
	uint8_t out[CHUNK];
	int chunksize = root[i].compressed_size < CHUNK ? root[i].compressed_size : CHUNK ;

	/* allocate inflate state */
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;
	ret = inflateInit(&strm);
	if (ret != Z_OK)
		return ret;

	/* decompress until deflate stream ends or end of file */
	do {
		strm.avail_in = fread(in, 1, chunksize, archive);
		if (ferror(archive)) {
			(void)inflateEnd(&strm);
			return Z_ERRNO;
		}
		if (strm.avail_in == 0)
			break;
		strm.next_in = in;
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
			have = chunksize - strm.avail_out;
			if (fwrite(out, 1, have, outfile) != have || ferror(outfile)) {
				(void)inflateEnd(&strm);
					return Z_ERRNO;
			}
		} while (strm.avail_out == 0);
		/* done when inflate() says it's done */
		written += chunksize;
		if(written + chunksize > root[i].compressed_size) {
			chunksize = root[i].compressed_size - written;
		}
	} while (written != root[i].uncompressed_size);
	(void)inflateEnd(&strm);

	fflush(outfile);
	fclose(outfile);
	return 0;
}

int azpCompressFile(char *filename, uint32_t *filesize)
{
	struct stat st;
	FILE *source = fopen(filename, "rb");
	if(source == NULL) {
		perror("Error opening file for reading (compress)");
		return -1;
	}
	//char filename_ext[NAME_MAX];
	//sprintf(filename_ext, "%s", filename);
	strcat(filename, ".tmp");
	FILE *dest = fopen(filename, "wb");
	if(source == NULL) {
		perror("Error opening file for writing (compress)");
		fclose(source);
		return -1;
	}

    int ret, flush;
    unsigned have;
    z_stream strm;
    unsigned char in[CHUNK];
    unsigned char out[CHUNK];

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
        strm.avail_in = fread(in, 1, CHUNK, source);
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
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = deflate(&strm, flush);    /* no bad return value */
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            have = CHUNK - strm.avail_out;
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
