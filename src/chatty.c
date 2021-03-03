/*
 * membox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */
/**
 * @file chatty.c
 * @brief File principale del server chatterbox
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include "parser.h"
#include "global_funcs.h"
#include "global_var.h"
#include "threads.h"
#include "stats.h"

/* inserire gli altri include che servono */

/* struttura che memorizza le statistiche del server, struct statistics 
 * e' definita in stats.h.
 *
 */
struct statistics  chattyStats = { 0,0,0,0,0,0,0 };


static void usage(const char *progname) {
    fprintf(stderr, "Il server va lanciato con il seguente comando:\n");
    fprintf(stderr, "  %s -f conffile\n", progname);
}

void cleanup() {
	//DIR* dir;
	//if((dir = opendir("/tmp")) == NULL) checkErrno("Apertura directory\n");
	if(remove(UNIXPATH) == -1) checkErrno("Rimozione file\n");
	printf("File rimosso\n");
	return;
}

int main(int argc, char *argv[]) {
	printf("Chatty attivo\n");
	if(argc != 3) {
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}
	//da fare: controllare che argv[2] sia un file
	//gestisco segnali
	sigset_t threadset;
	memset(&threadset,0,sizeof(sigset_t)); 
	int err, sig;
	if(sigfillset(&threadset)==-1) checkErrno("Sigfillset thread stats");
	if(sigdelset(&threadset,SIGSEGV)==-1) checkErrno("Sigdelset thread stats");
	//if(sigdelset(&threadset,SIGTERM)==-1) checkErrno("Sigdelset thread stats");
	if((err=pthread_sigmask(SIG_SETMASK,&threadset,NULL))!=0) checkError(err,"Set sigmask thread stats");
	signal(SIGPIPE, SIG_IGN);
	//parsing del file di configurazione
	if(parser(argv[2]) == -1) checkErrno("Parsing file di configurazione");
	//dichiarazione di variabili utili
	err = 0;
	//istanzio tutte le variabili necessarie
	term = 0;
	//creo e avvio i thread
	pthread_t lTid, sTid; //TID del thread listener
	pthread_t wTid[THREADSINPOOL];
	int lRet[1]; 
	int sRet[1]; //valore di ritorno del thread listener
	int wRet[THREADSINPOOL];
	printf("Avvio i thread\n");
	printf("Avvio il listener\n");
	if((err = pthread_create(&lTid,NULL,&listener,NULL)) != 0) checkError(err,"Creazione thread listener");
	printf("Avvio l'handler\n");
	if((err = pthread_create(&sTid,NULL,&handler,NULL)) != 0) checkError(err,"Creazione thread stats");
	printf("Avvio i worker\n");
	for(int i = 0; i < THREADSINPOOL; i++) {
		if((err = pthread_create(&(wTid[i]),NULL,&worker,NULL)) != 0) checkError(err,"Creazione thread worker");
	}
	if((err = pthread_join(sTid,(void*) &sRet[1])) != 0) checkError(err,"Join thread stats");
	printf("Thread stats uscito\n");
	if((err = pthread_join(lTid,(void*) &lRet[1])) != 0) checkError(err,"Join thread listener");
	for(int i = 0; i < THREADSINPOOL; i++) {
		if((err = pthread_join(wTid[i], (void*) &wRet[i])) != 0) checkError(err,"Join thread worker");
	}
	printf("Main termina\n");
	return 0;
}
