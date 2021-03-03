/*
 * chatterbox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */

#ifndef GLOBAL_FUNCS_H_
#define GLOBAL_FUNCS_H_

#include "global_var.h"

/**
 * @file  global_funcs.h
 * @brief Contiene la segnatura di funzioni di utilità per
 *	  il programma
 */


/**
 * @function checkErrno
 * @brief Gestisce un errore nell'esecuzione di una system call
 *
 * @param errmsg Messaggio di errore da stampare sullo stderr
 */

void checkErrno(char* errmsg);

void checkError(int numErr,const char *errormessage);

void lockAcquire(pthread_mutex_t* mtx);

void lockRelease(pthread_mutex_t* mtx);

void condWait(pthread_cond_t *cond, pthread_mutex_t* mtx);

void condSignal(pthread_cond_t* cond);



#endif
