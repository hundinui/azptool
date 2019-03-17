/*
 *	azptool
 *
 *	Tool to unextracting and compressing 7,62 High Caliber .AZP archives
 *
 *	Arguments:
 *		Compress files into archive:	-c, --compress 	[FILES] FILENAME
 *		Extract files from archive:		-e, --extract 	FILENAME
 * 		TODO Append files to archive:		-a, --append	[FILES] FILENAME
 * 		TODO Delete files from archive		-d, --delete	[FILES] FILENAME
 * 		List all files in archive:		-l, --list		FILENAME
 * 		List help text					-h, --help
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "azp.h"

typedef enum eJobType {
    JOB_NONE,
    JOB_COMPRESS,
    JOB_EXTRACT = 2,
    JOB_APPEND = 3,
    JOB_LIST = 4
} eJobType;

void print_usage(void) {
    printf("\
    Tool to unextracting and compressing 7,62 High Caliber .AZP archives\n\
    \n\
    Arguments:\n \
    \tCompress files into archive: -c, --compress FILE_LIST FILENAME\n\
    \tExtract files from archive:  -e, --extract  FILENAME\n\
    \tList all files in archive:   -l, --list     FILENAME\n\
    \tList help text:              -h, --help\n");
}

/* Ugly size units calculation */
static const char *sizeUnit(size_t bytes, size_t *bytesdiv) {
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
static void azp_list_entries(const azpHeader_t *header, const azpEntry_t *root) {
    size_t sum_compressed = 0;
    size_t sum_uncompressed = 0;
    const char *unit_comp;
    const char *unit_uncomp;
    printf("╔═══════╦══════════╦══════════╦════════════════════════════════════════════════╗\n" \
           "║  Nr.  ║  Packed  ║ Unpacked ║                   Filename                     ║\n" \
           "║       ║   Size   ║   Size   ║                                                ║\n" \
           "╠═══════╬══════════╬══════════╬════════════════════════════════════════════════╣\n");
    for(uint32_t i = 0; i < header->fields.file_count; ++i) {
        sum_compressed += root[i].compressed_size;
        sum_uncompressed += root[i].uncompressed_size;
        size_t compressed_size = -1;
        size_t uncompressed_size = -1;
        unit_comp = sizeUnit(root[i].compressed_size, &compressed_size);
        unit_uncomp = sizeUnit(root[i].uncompressed_size, &uncompressed_size);
        printf("║ %5u ║%4zu %-4s ║ %-4zu %-4s║  %-46s║\n", 
               i+1, compressed_size, unit_comp, uncompressed_size, unit_uncomp,root[i].filename);
    }
    printf("╚═══════╩══════════╩══════════╩════════════════════════════════════════════════╝\n");
    unit_comp = sizeUnit(sum_compressed, &sum_compressed);
    unit_uncomp = sizeUnit(sum_uncompressed, &sum_uncompressed);
    printf("\nUncompressed filesize: %zu %s\nCompressed filesize: %zu %s\nCompression ratio: %04f\n", 
           sum_uncompressed, unit_uncomp, sum_compressed, unit_comp, (float)sum_compressed/sum_uncompressed);
}

int main(int argc, char **argv) {

    eJobType jobtype = JOB_NONE;
    char *filename;
    char *file_list[argc];
    size_t file_count = 0;

    if(argc < 2) {
        print_usage();
        return 1;
    }
    if(argc > 2) {
        filename = argv[argc-1];
        --argc;
    }
    for(uint16_t i = 1; i < argc; ++i) {
        /* If we already have a job then assume that everything past is filenames */
        if(jobtype == JOB_NONE) {
            /* If long arguments then increment the pointer */
            if(argv[i][0] == '-' && argv[i][1] == '-') {
                ++argv[i];
            };
            switch(argv[i][1]) {
            case 'c':
                jobtype = JOB_COMPRESS;
                break;
            case 'e':
                jobtype = JOB_EXTRACT;
                break;
            /*case 'a':
                jobtype = JOB_APPEND;
                break;*/
            case 'l':
                jobtype = JOB_LIST;
                break;
            default:
                printf("Invalid argument: %s\n", argv[i]);
            case 'h':
                print_usage();
                return 0;
                break;
            }
        } else {
            file_list[file_count++] = argv[i];
        }
    }
    
    /* If jobtype has something to do with an already existing archive then check for valid archive */
    if(jobtype >= JOB_EXTRACT) {
        printf("Reading archive %s...\n", filename);
        int infile_fd = open(filename, O_RDONLY);
        if(infile_fd == -1) {
            printf("Error opening file %s!\n", filename);
            return -1;
        }
        struct stat st;
        if(fstat(infile_fd, &st) != 0) {
            fprintf(stderr, "Cannot stat file %s\n", filename);
            close(infile_fd);
            return -1;
        }
        size_t infile_sz = st.st_size;
        
        uint8_t *infile = mmap(NULL, infile_sz, PROT_READ, MAP_PRIVATE, infile_fd, 0);
        if(infile == MAP_FAILED) {
            fprintf(stderr, "Cannot mmap file %s\n", filename);
            close(infile_fd);
            return -1;
        }
            
        azpHeader_t header;
        
        if(!azp_check_header(&header, infile, infile_sz)) {
            fprintf(stderr, "Not a valid AZP archive, header mismatch!\n");
            close(infile_fd);
            munmap(infile, infile_sz);
            return -1;
        }
        azpEntry_t *toc = azp_get_file_list(&header, infile, infile_sz);
        if(toc == NULL) {
            fprintf(stderr, "Error getting file list\n");
            close(infile_fd);
            munmap(infile, infile_sz);
            return -1;
        }
        switch(jobtype) {
            default:
            case JOB_LIST:
                azp_list_entries(&header, toc);
                break;
            case JOB_EXTRACT:
                if(!azp_extract_all(&header, toc, infile, infile_sz)) {
                    fprintf(stderr, "Error extracting archive\n");
                }
                break;
            case JOB_APPEND:
                break;
        }

        free(toc);
        close(infile_fd);
        munmap(infile, infile_sz);
    } else if (jobtype == JOB_COMPRESS) {
        azpHeader_t header;
        
        if(strstr(filename, ".azp") == NULL && strstr(filename, ".AZP") == NULL) {
            strcat(filename, ".azp");
        }
        printf("Creating archive %s...\n", filename);

        azpEntry_t *toc = azp_make_file_list(&header, file_list, file_count);
        if(toc != NULL) {
            azp_compress_files(&header, toc, filename);
        }
        free(toc);
    }

    return 0;
}
