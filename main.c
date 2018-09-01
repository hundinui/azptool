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
#include <unistd.h>
#include <limits.h> // for NAME_MAX

#ifdef COUNT_TIME_TAKEN
	#include <time.h>
#endif

#include "azp.h"

typedef enum eJobType {
	JOB_NONE,
	JOB_COMPRESS,
	JOB_EXTRACT = 2,
	JOB_APPEND = 3,
	JOB_LIST = 4
} eJobType;

const char *helptxt =
"\
Tool to unextracting and compressing 7,62 High Caliber .AZP archives\n\
\n\
Arguments:\n \
\tCompress files into archive: -c, --compress [FILES] FILENAME\n\
\tExtract files from archive:  -e, --extract  FILENAME\n\
\tAppend files to archive:     -a, --append   [FILES] FILENAME\n\
\tList all files in archive:   -l, --list     FILENAME\n\
\tList help text:              -h, --help\n";


int main(int argc, char **argv) {
	FILE *infile;
	azpHeader header;

	eJobType jobtype = JOB_NONE;
	/* List of files to compress or append */
	sStrList filelist = { .str = "START", .next = NULL, .prev = NULL };
	/* Filename of archive */
	char filename[NAME_MAX];

	/* Print welcome message */
	printf("azptool\n");
	printf("Build %s %s\n", __DATE__, __TIME__);

#ifdef COUNT_TIME_TAKEN
	time_t start_time;
	time(&start_time);
#endif

	if(argc < 2) { printf("No operations specified, use -h or --help for help\n"); }
	if(argc > 2) { strcpy(filename, argv[argc-1]); --argc; }
	/* Parse command line arguments */
	for(int i = 1; i < argc; ++i) {
		/* If we already have a job then assume that everything past is filenames */
		if(jobtype == JOB_NONE) {
			/* If long arguments then increment the pointer */
			if(argv[i][0] == '-' && argv[i][1] == '-') { ++argv[i]; };
			switch(argv[i][1]) {
				case 'c':
					jobtype = JOB_COMPRESS;
					break;
				case 'e':
					jobtype = JOB_EXTRACT;
					break;
				case 'a':
					jobtype = JOB_APPEND;
					break;
				case 'l':
					jobtype = JOB_LIST;
					break;
				default:
					printf("Invalid argument: %s\n", argv[i]);
				case 'h':
					printf("%s\n", helptxt
					);
					return 0;
					break;
			}
		} else {
			strListAppend(&filelist, argv[i]);
		}
	}
	/* If jobtype has something to do with an already existing archive then check for valid archive */
	if(jobtype >= JOB_EXTRACT) {
		printf("Reading archive %s...\n", filename);
		infile = fopen(filename, "rb");
		if(infile == NULL) {
			printf("Error opening file %s!\n", filename);
			return -1;
		}
		azpCheckHeader(&header, infile);
		azpEntry *toc = azpGetFileList(&header, infile);
		switch(jobtype) {
			default:
			case JOB_LIST:
				azpListEntries(&header, toc);
				break;
			case JOB_EXTRACT:
				azpExtractAll(&header, toc, infile);
				break;
			case JOB_APPEND:
				break;
		}

		free(toc);
		//azpListEntries(toc, &header);
		fclose(infile);
	} else if (jobtype == JOB_COMPRESS) {
		/* If archive name is provided without extention then add it */
		if(strstr(filename, ".azp") == NULL && strstr(filename, ".AZP") == NULL) {
			strcat(filename, ".azp");
		}
		sprintf(filelist.str, "%s", filename);
		printf("Creating archive %s...\n", filename);

		azpEntry *toc = azpMakeFileList(&header, &filelist);
		azpCompressFiles(&header, toc, filelist.str);
	}

	strListDestroy(&filelist);

#ifdef COUNT_TIME_TAKEN
	time_t end_time;
	time(&end_time);
	printf("Elapsed time ~%.0f seconds\n",  difftime(end_time, start_time));
#endif

    return 0;
}
