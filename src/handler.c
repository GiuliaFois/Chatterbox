#define _POSIX_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <signal.h>
#include <fcntl.h>
#include "stats.h"
#include "connections.h"
#include "ops.h"
#include "icl_hash.h"
#include "threads.h"
#include "parser.h"
#include "message.h"
#include "global_var.h"
#include "global_funcs.h"


void* handler() {
	FILE* f;
	//blocco tutti i segnali a parte SIGQUIT, SIGINT, SIGTERM e SIGUSR1 
	sigset_t threadset;
	memset(&threadset,0,sizeof(sigset_t)); 
	int err, sig;
	if(sigemptyset(&threadset)==-1) checkErrno("Sigfillset thread stats");
	if(sigaddset(&threadset,SIGQUIT)==-1) checkErrno("Sigdelset thread stats");
	if(sigaddset(&threadset,SIGINT)==-1) checkErrno("Sigdelset thread stats");
	if(sigaddset(&threadset,SIGTERM)==-1) checkErrno("Sigdelset thread stats");
	if(sigaddset(&threadset,SIGUSR1)==-1) checkErrno("Sigdelset thread stats");
	if(sigaddset(&threadset,SIGPIPE)==-1) checkErrno("Sigaddset thread stats");
	if((err=pthread_sigmask(SIG_SETMASK,&threadset,NULL))!=0) checkError(err,"Set sigmask thread stats");
	while(1) {
		if((err=sigwait(&threadset,&sig))!=0) checkError(err,"Sigwait");
		printf("STATSTHREAD Sveglio\n");
		if(sig==SIGUSR1) {
			printf("STATISTICHE: utenti reg %d, utenti conn %d\n", chattyStats.nusers, chattyStats.nonline);
			if((f=fopen(STATFILENAME,"w+"))==NULL) checkErrno("Apertura file stats");
			if(printStats(f)==-1) checkError(-1,"Stampa statistiche");
			if(fclose(f)!=0) checkErrno("Chiusura file stats");
			}
		if(sig == SIGPIPE) {
			printf("Ricevuto segnale SIGPIPE\n");
		}
		if(sig == SIGQUIT || sig == SIGINT || sig == SIGTERM) {
			term=1;
			lockAcquire(&mtx_head);
			pthread_cond_broadcast(&empty_queue);
			lockRelease(&mtx_head);
			if(remove(UNIXPATH) == -1) checkErrno("Rimozione file");
			icl_entry_t* toFree;
			message_t* hist;
			groupNode* groupUsr;
			for(int i = 0; i < NBUCKETS; i ++) {
				toFree = userTab->buckets[i];
				while(toFree != NULL) {
					hist = ((usrData*) toFree->data)->history;
					for(int j = 0; j < MAXHISTMSGS; j++) {
						free(hist[j].data.buf);
						}
					free(hist);
					toFree = toFree -> next;
					}
				}
			for(int i = 0; i < GROUPBUCKETS; i ++) {
				toFree = groupTab->buckets[i];
				while(toFree != NULL) {
					groupUsr = ((groupData*) toFree->data)->groupList;
					while(groupUsr->next != NULL) {
						groupUsr = groupUsr -> next;
						if(groupUsr -> prev != NULL) free(groupUsr->prev);
					}
					if(groupUsr != NULL) free(groupUsr);
					toFree = toFree -> next;
				}
			}
			icl_hash_destroy(userTab,&free,&free);
			icl_hash_destroy(groupTab,&free,&free);
			return (void*) 1; 
			}
		}
	}
