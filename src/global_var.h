/*
 * chatterbox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */

#ifndef GLOBAL_VAR_H_
#define GLOBAL_VAR_H_

#define NBUCKETS 50 
#define GROUPBUCKETS 20 

#include <pthread.h>
#include <sys/select.h>
#include "message.h"
#include "icl_hash.h"
#include "stats.h"



/**
 * @file  global_var.h
 * @brief Contiene variabili globali condivise tra i thread
 *	  del programma
 */





typedef struct connectionNode_ {
	int fd;
	struct _connectionNode* prev; 
	struct _connectionNode* next;
} connectionNode;

extern connectionNode* head;
extern connectionNode* tail;

extern fd_set set;
extern fd_set rdset;
extern fd_set checkset;

extern int fd_num;

extern pthread_mutex_t mtx_head;
extern pthread_mutex_t mtx_tail;
extern pthread_mutex_t mtx_set;
extern pthread_mutex_t mtx_onlist;
extern pthread_mutex_t mtx_term;
extern pthread_mutex_t mtx_stats_register;
extern pthread_mutex_t mtx_stats_connect;
extern pthread_mutex_t mtx_stats_messages;
extern pthread_mutex_t mtx_stats_errors;
extern pthread_mutex_t mtx_tab[NBUCKETS]; 
extern pthread_mutex_t mtx_group_tab[GROUPBUCKETS]; 
extern pthread_cond_t empty_queue;

int term; //per la terminazione. inizializzata a 0 nel main

typedef struct _onUsr {
	char* name;
	int fd;
	struct _onUsr* next;
} onUsr;

int numOn;
onUsr* onList;
onUsr* lastOn;
icl_hash_t* userTab;
icl_hash_t* groupTab;

typedef struct _usrData {
	int lastFd;
	int lastMsgIdx;
	int isOn;
	message_t* history;
} usrData;

typedef struct _groupNode {
	char member[MAX_NAME_LENGTH+1];
	usrData* usr;
	int lastFd;
	struct _groupNode* prev;
	struct _groupNode* next;
} groupNode;


typedef struct _groupData {
	groupNode* groupList;
	groupNode* lastMember; //per non dover scorrere tutta la lista
	int lastMsgIdx;
	message_t* history;
} groupData;

struct statistics  chattyStats;


#endif

