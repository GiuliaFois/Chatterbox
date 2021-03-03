/*
 * chatterbox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include "global_var.h"
#include "thread_funcs.h"

/**
 * @file  thread_funcs.c
 * @brief Contiene l'implementazione delle funzioni definite
 *	  in thread_funcs.h
 */


void connectionInsert(int fd) {
	connectionNode* new;
	while((new = malloc(sizeof(connectionNode))) == NULL)
		new = malloc(sizeof(connectionNode));
	new -> fd = fd;
	new -> next = NULL;
	if(head == NULL) {
		head = new;
		tail = head;
	}
	else {
		tail -> next = new;
		tail = tail -> next;
	}
}

int connectionExtract() {
	int fd = head -> fd;
	connectionNode* toFree = head;
	head = head -> next;
	if(head == tail) tail = head;
	free(toFree);
	return fd;
}
