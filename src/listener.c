#define _POSIX_SOURCE //SPIEGARE PERCHE'
#define _GNU_SOURCE //???????

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
#include <limits.h>
#include "global_funcs.h"
#include "stats.h"
#include "global_var.h"
#include "thread_funcs.h"
#include "parser.h"

#define BITS_IN_int     ( sizeof(int) * CHAR_BIT )
#define THREE_QUARTERS  ((int) ((BITS_IN_int * 3) / 4))
#define ONE_EIGHTH      ((int) (BITS_IN_int / 8))
#define HIGH_BITS       ( ~((unsigned int)(~0) >> ONE_EIGHTH ))

#if !defined(UNIX_PATH_MAX)
#define UNIX_PATH_MAX  64
#endif

//gestione numero massimo di connessioni: accetto tutti ma nel worker
//se numconnections > valore restituisco un messaggio di errore
//tipo OP_FAIL

connectionNode* head;
connectionNode* tail;

fd_set set;
fd_set rdset;
fd_set checkset;
int fd_num;
pthread_mutex_t mtx_head = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_tail = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_set = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_onlist = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_term = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_stats_register = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_stats_connect = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_stats_messages = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_stats_errors = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_tab[NBUCKETS];
pthread_mutex_t mtx_group_tab[GROUPBUCKETS];
pthread_cond_t empty_queue = PTHREAD_COND_INITIALIZER;

static unsigned int
hash_pjw(void* key)
{
    char *datum = (char *)key;
    unsigned int hash_value, i;

    if(!datum) return 0;

    for (hash_value = 0; *datum; ++datum) {
        hash_value = (hash_value << ONE_EIGHTH) + *datum;
        if ((i = hash_value & HIGH_BITS) != 0)
            hash_value = (hash_value ^ (i >> THREE_QUARTERS)) & ~HIGH_BITS;
    }
    return (hash_value);
}

static int string_compare(void* a, void* b) 
{
    return (strcmp( (char*)a, (char*)b ) == 0);
}

/* --------------------------------------------------------------------- */

void* listener() {
	printf("Listener attivo\n");
	sigset_t threadset;
	memset(&threadset,0,sizeof(sigset_t)); 
	int err, sig;
	if(sigfillset(&threadset)==-1) checkErrno("Sigfillset thread stats");
	if(sigdelset(&threadset,SIGSEGV)==-1) checkErrno("Sigdelset thread stats");
	if(sigdelset(&threadset,SIGINT)==-1) checkErrno("Sigdelset thread stats");
	//if(sigdelset(&threadset,SIGTERM)==-1) checkErrno("Sigdelset thread stats");
	if((err=pthread_sigmask(SIG_SETMASK,&threadset,NULL))!=0) checkError(err,"Set sigmask thread stats");
	//inizializzazione della coda descrittori
	head = NULL;
	tail = NULL;
	int sfd; //descrittore socket server
	int cfd; //descrittore connessione client
	fd_num = 0; //massimo descrittore attivo
	//inizializzo variabili mutex
	for(int i = 0; i < NBUCKETS; i++) {
		if((err = pthread_mutex_init(&mtx_tab[i],NULL)) != 0) checkError(err, "Inizializzazione mutex array");
	}
	for(int i = 0; i < NBUCKETS; i++) {
		if((err = pthread_mutex_init(&mtx_group_tab[i],NULL)) != 0) checkError(err, "Inizializzazione mutex array");
	}
	userTab = icl_hash_create(NBUCKETS,hash_pjw,string_compare);
	groupTab = icl_hash_create(GROUPBUCKETS,hash_pjw,string_compare);
	onList = NULL;
	lastOn = NULL;
	numOn = 0;
	if((sfd = socket(AF_UNIX,SOCK_STREAM,0)) == -1) checkErrno("Creazione socket");
	struct sockaddr_un sa;
	strncpy(sa.sun_path,UNIXPATH,UNIX_PATH_MAX);
	sa.sun_family = AF_UNIX;
	if(bind(sfd, (struct sockaddr*)&sa, sizeof(sa)) == -1) checkErrno("Bind socket");
	if(listen(sfd,SOMAXCONN) == -1) checkErrno("Listen su socket");
	if(sfd > fd_num) fd_num=sfd;
	struct timeval tsel; //timer per la select
	tsel.tv_sec = 0;
	tsel.tv_usec = 1000; //controllare timer
	FD_ZERO(&set);
	FD_ZERO(&checkset);
	FD_SET(sfd,&set);
	lockRelease(&mtx_set);
	while(1) {
		rdset = set;
		if(select(fd_num+1,&rdset,NULL,NULL,&tsel) == -1) checkErrno("Select");
		for(int currfd = 0; currfd <= fd_num; currfd ++) {
			if(FD_ISSET(currfd,&rdset)) {
				if(currfd == sfd) {
					printf("Accettata nuova connessione\n");
					cfd = accept(sfd,NULL,0);
					lockAcquire(&mtx_set);
					FD_SET(cfd,&set);
					if(cfd > fd_num) fd_num = cfd;
					lockRelease(&mtx_set);
				}
				else if(!FD_ISSET(currfd,&checkset)) {
					printf("Richiesta su connessione già esistente\n");
					lockAcquire(&mtx_set);
					FD_CLR(currfd,&set);
					FD_SET(currfd,&checkset);
					if(currfd == fd_num) fd_num --;
					lockRelease(&mtx_set);
					printf("Inserisco in testa\n");
					lockAcquire(&mtx_head);
					connectionInsert(currfd);
					condSignal(&empty_queue);
					lockRelease(&mtx_head);
				}
			}
		}
	//lockAcquire(&mtx_term);
	if(term==1) {
 			printf("LISTENER: TERM E' 1\n");
			//lockRelease(&mtx_term);
			printf("LISTENER STA USCENDO\n");
			return (void*) 1; 
			}
	//lockRelease(&mtx_term);
	}
}

