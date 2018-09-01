#ifndef _STRLIST_H_
#define _STRLIST_H_

#include <limits.h>

typedef struct sStrList {
	char str[NAME_MAX];
	struct sStrList *next;
	struct sStrList *prev;
} sStrList;

/*
 * Append string to string list
 */
void strListAppend(sStrList *root, char *string);

/*
 * Free string list
 */
void strListDestroy(sStrList *root);

/*
 * Print string list to stdout
 */
void strListPrint(sStrList *root);

/*
 * Get count of string list
 */
int strListGetCount(sStrList *root);

#endif
