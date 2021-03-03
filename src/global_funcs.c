/*
 * chatterbox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "global_funcs.h"

/**
 * @file  global_funcs.c
 * @brief Contiene l'implementazione delle funzioni definite
 * 	  in global_funcs.h
 */

void checkErrno(char* errmsg) {
	perror(errmsg);
	exit(EXIT_FAILURE); //O ALTRO?
}


void checkError(int numErr,const char *errormessage) {
	fprintf(stderr,"Errore numero %d:",numErr);
	fprintf(stderr," %s\n",errormessage);
	exit(EXIT_FAILURE);
	}

void lockAcquire(pthread_mutex_t* mtx) {
	int rv;
	if((rv = pthread_mutex_lock(mtx)) != 0) {
		fprintf(stderr, "Errore %d: ",rv);
		fprintf(stderr, "Errore acquisizione lock\n");
		exit(EXIT_FAILURE);
		}
}

void lockRelease(pthread_mutex_t* mtx) {
	int rv;
	if((rv = pthread_mutex_unlock(mtx)) != 0) {
		fprintf(stderr, "Errore %d: ",rv);
		fprintf(stderr, "Errore acquisizione lock\n");	
		exit(EXIT_FAILURE);
		}
}

void condWait(pthread_cond_t* cond, pthread_mutex_t* mtx) {
	int rv;
	if((rv = pthread_cond_wait(cond,mtx)) != 0) checkError(rv,"Wait su condition variable");
	}


void condSignal(pthread_cond_t* cond) {
	int rv;
	if((rv = pthread_cond_signal(cond)) != 0) {
		//qua ci metto la checkError
		fprintf(stderr, "Errore %d: ",rv);
		fprintf(stderr, "Errore signal\n");
		exit(EXIT_FAILURE);
	}
}


