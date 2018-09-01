#include <stdio.h>
#include <stdlib.h>
#include "strlist.h"

void strListAppend(sStrList *root, char *string) {
	if(root == NULL) {
		printf("Error root NULL\n");
		return;
	}
	sStrList *head = root;
	while(head->next != NULL) {
		head = head->next;
	}
	head->next = malloc(sizeof(sStrList));
	head->next->prev = head;
	sprintf(head->next->str, "%s", string);
}

void strListDestroy(sStrList *root) {
	sStrList *head = root;
	while(head->next != NULL) {
		head = head->next;
		free(head->prev);
		head->next = NULL;
		head->prev = NULL;
	}
	free(head->prev);
	head->next = NULL;
	head->prev = NULL;
}

void strListPrint(sStrList *root) {
	if(root == NULL) {
		printf("Error string list root NULL!\n");
		return;
	}
	sStrList *head = root;
	int i = 0;
	while(head->next != NULL) {
		printf("%i %s\n",i, head->str);
		head = head->next;
		++i;
	}
	printf("%i %s\n",i, head->str);
}

int strListGetCount(sStrList *root) {
	if(root == NULL) {
		return 0;
	}
	sStrList *head = root;
	int i = 0;
	while(head->next != NULL) {
		head = head->next;
		++i;
	}
	return i;
}