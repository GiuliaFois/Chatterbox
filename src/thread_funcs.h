/*
 * chatterbox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */

#ifndef THREAD_FUNCS_H_
#define THREAD_FUNCS_H_

#include "global_var.h"

/**
 * @file  thread_funcs.h
 * @brief Contiene la segnatura di funzioni utilizzate dai
 *	  thread del programma
 */


/**
* @function connectionInsert
* @brief Inserisce un file descriptor nella coda di fd condivisa
* 	 fra thread listener e threads worker
*
* @param fd File descriptor da inserire
*/

void connectionInsert(int fd);

/**
* @function connectionExtract
* @brief Estrae un file descriptor dalla coda di fd condivisa
*	 fra thread listener e threads worker
*
* @return Il file descriptor estratto
*/

int connectionExtract();


#endif

