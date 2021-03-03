#define _POSIX_SOURCE //SPIEGARE PERCHE'
#define _GNU_SOURCE
#define _XOPEN_SOURCE //?

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
#include <linux/limits.h>
#include "connections.h"
#include "message.h"
#include "threads.h"
#include "global_var.h"
#include "global_funcs.h"
#include "parser.h"
#include "icl_hash.h"
#include "ops.h"

#define BITS_IN_int     ( sizeof(int) * CHAR_BIT )
#define THREE_QUARTERS  ((int) ((BITS_IN_int * 3) / 4))
#define ONE_EIGHTH      ((int) (BITS_IN_int / 8))
#define HIGH_BITS       ( ~((unsigned int)(~0) >> ONE_EIGHTH ))

#if !defined(UNIX_PATH_MAX)
#define UNIX_PATH_MAX  64
#endif

connectionNode* head;
connectionNode* tail;

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


//---struttura utile per memorizzare informazioni relative alle richieste gestite dai thread workers----

typedef struct _request {
	op_t op;
	char sender[MAX_NAME_LENGTH+1];
	icl_hash_t* usr; //mantengo il puntatore al nodo dell'utente
	char receiver[MAX_NAME_LENGTH+1];
	icl_hash_t* dest;
	int destkey;
	int size;
	char* message;
	int hashVal; //valore hash del sender
	} request;


//---definizione di funzioni utili 

void handleFd(int fd) {
	lockAcquire(&mtx_set);
	FD_SET(fd,&set);
	FD_CLR(fd,&checkset);
	if(fd > fd_num) fd_num=fd;
	lockRelease(&mtx_set);
}

usrData* newData(int fd, char* usr) { //USR NON E' NECESSARIO
	usrData* data = calloc(1,sizeof(usrData));
	//data -> type = 0;
	data->lastFd = fd;
	data->lastMsgIdx = 0; //al primo messaggio salvato in history diventa 0
	data->isOn = 1; //quando lo registro è online (POSSO CANCELLARLO) (O no?)
	data->history = calloc(MAXHISTMSGS,sizeof(message_t));
	return data;
}

void registerUsr(char* usr, usrData* data) {
	lockAcquire(&mtx_stats_register);
	chattyStats.nusers++;
	lockRelease(&mtx_stats_register);
	int position = hash_pjw((void*) usr) % NBUCKETS;
	lockAcquire(&mtx_tab[position]);
	icl_hash_insert(userTab,(void*) usr, (void*) data); //SE RITORNA NULL E' GIA' REGISTRATO, MODIFICARE REGUSR
	lockRelease(&mtx_tab[position]);
	return;
}

usrData* searchUsr(char* usr) {
	int hashVal = hash_pjw((void*) usr) % NBUCKETS;
	lockAcquire(&mtx_tab[hashVal]);
	usrData* found = icl_hash_find(userTab,(void*) usr);
	lockRelease(&mtx_tab[hashVal]);
	return found;
}

groupData* newGroupData(char* creator) {
	groupData* data = calloc(1,sizeof(groupData));
	groupNode* head = calloc(1,sizeof(groupNode));
	strncpy(head->member,creator,strlen(creator)+1);
	head->next = NULL;
	head->prev = NULL;
	head->usr = searchUsr(creator);
	data -> groupList = head;
	data -> lastMember = head;
	//data -> lastMsgIdx = 0;
	//data -> history = calloc(MAXHISTMSGS,sizeof(message_t));
	return data;
}

icl_entry_t* createGroup(char* name, groupData* data) {
	icl_entry_t* retVal;
	int position = hash_pjw((void*) name) % GROUPBUCKETS;
	lockAcquire(&mtx_group_tab[position]); //MOTIVO ALTRA TABELLA: PER NON BLOCCARE LA HASH TABLE UTENTI CON STESSE MUTEX
	retVal = icl_hash_insert(groupTab,(void*) name,(void*) data); //SE RITORNA NULL E' GIA' REGISTRATO, MODIFICARE REGUSR
	lockRelease(&mtx_group_tab[position]);
	if(retVal == NULL) printf("Retval è NULL\n");
	printf("Gruppo creato\n");
	return retVal;
}

groupData* searchGroup(char* group) {
	printf("Sto cercando il gruppo %s\n", group);
	groupData* retVal;
	int position = hash_pjw((void*) group) % GROUPBUCKETS;
	lockAcquire(&mtx_group_tab[position]); 
	retVal = (groupData*) icl_hash_find(groupTab,(void*) group);
	//printf("RETVAL MEMBRO: %s\n", ((retVal->data)->groupList)->member);
	lockRelease(&mtx_group_tab[position]);
	if(!retVal) {
		printf("RETVAL E' NULL\n");
		return NULL;
		}
	else return retVal;
}

groupNode* searchMember(char* group, char* name) {
	int position = hash_pjw((void*) group) % GROUPBUCKETS;
	lockAcquire(&mtx_group_tab[position]);
	groupData* groupEntry = (groupData*) icl_hash_find(groupTab,group);
	groupNode* currMember = groupEntry->groupList;
	while(currMember != NULL) {
		printf("Membro gruppo: %s\n", currMember->member);
		printf("Name: %s\n", name);
		if(strncmp(currMember->member,name,MAX_NAME_LENGTH+1) == 0) {
			lockRelease(&mtx_group_tab[position]);
			return currMember;
			}
		currMember = currMember->next;
	}
	lockRelease(&mtx_group_tab[position]);
	return NULL;
}



int deleteMember(char* group, char* name) {
	groupNode* member;
	if((member = searchMember(group, name)) == NULL) return 0;
	int position = hash_pjw((void*) group) % GROUPBUCKETS;
	lockAcquire(&mtx_group_tab[position]);
	icl_entry_t* tabEntry = (icl_entry_t*) groupTab->buckets[position];
	groupData* groupEntry = (groupData*) tabEntry->data; 
	groupNode* prev = member->prev;
	groupNode* next = member->next;
	if(member == groupEntry->groupList) {
		groupEntry->groupList = next;
		next -> prev = NULL;
		}
	else if(member == groupEntry->lastMember) {
		groupEntry->lastMember = prev;
		prev->next = NULL;
		}
		
	else {
		prev->next = next;
		next->prev = prev;
		}
	free(member);
	lockRelease(&mtx_group_tab[position]);
	return 1;
}




void connectUsr(char* usr, int fd) {
	lockAcquire(&mtx_stats_connect);
	chattyStats.nonline++;
	lockRelease(&mtx_stats_connect);
	lockAcquire(&mtx_onlist);
	numOn++;
	onUsr* newOn;
	if((newOn = calloc(1,sizeof(onUsr))) == NULL) checkErrno("Calloc");
	if((newOn->name = calloc(1,MAX_NAME_LENGTH+1)) == NULL) checkErrno("Calloc nome");
	strncpy(newOn->name,usr,MAX_NAME_LENGTH+1);
	newOn -> fd = fd;
	newOn -> next = NULL;
	if(onList == NULL) {
		printf("Lista utenti connessi vuota\n");
		onList = newOn;
		lastOn = onList;	
	}
	else {
		printf("Aggiungo in coda\n");
		lastOn->next = newOn;
		lastOn = lastOn -> next;
	}
	printf("Connessione completata\n");
	lockRelease(&mtx_onlist);
	return;
}

void disconnectUsr(int fd) {
	lockAcquire(&mtx_stats_connect);
	chattyStats.nonline--;
	lockRelease(&mtx_stats_connect);
	lockAcquire(&mtx_onlist);
	onUsr* toFree;
	onUsr* curr = onList;
	onUsr* prev = curr;
	while(curr != NULL) {
		if(curr-> fd == fd) {
			if(curr == onList) { //è il primo elemento
				onList = curr->next;
				toFree = curr;
				free(toFree->name);
				free(toFree);
				break;
				}
			else {
				if(curr == lastOn) lastOn = prev; //se è l'ultimo elemento
				prev->next = curr->next;
				toFree = curr;
				free(toFree->name);
				free(toFree);
				break;
				}
			}
		else {
			prev = curr;
			curr = curr->next;
			}
		}
	numOn--;
	lockRelease(&mtx_onlist);
	}

int isOn(char* usr) {
	lockAcquire(&mtx_onlist);
	onUsr* search = onList;
	int found = 0;
	while(search != NULL && !found) {
		if(strncmp(search->name,usr,MAX_NAME_LENGTH+1) == 0) found = 1;
		else search = search -> next;
	}
	lockRelease(&mtx_onlist);
	printf("Ison ritorna\n");
	return found;
}

char* checkOnList(int* n) {
	lockAcquire(&mtx_onlist);
	*n = numOn; //lo salvo perchè così se poi lo mando all'utente è lo stesso
	char* buf, *ret;
	printf("NUMERO DI UTENTI ONLINE: %d\n", numOn);
	if((buf = calloc(numOn,(MAX_NAME_LENGTH+1))) == NULL) {
		checkErrno("Calloc");
		exit(EXIT_FAILURE);
	}
	ret = buf;
	onUsr* curr = onList;
	while(curr != NULL) {
		printf("Utente online: %s\n",curr->name);
		strncpy(buf,curr->name,MAX_NAME_LENGTH+1);
		buf += MAX_NAME_LENGTH+1;
		curr = curr->next;
	}
	lockRelease(&mtx_onlist);
	return ret;
}

void addMember(char* group, char* name, int fd) {
	groupNode* newNode = calloc(1,sizeof(groupNode));
	newNode -> next = NULL;
	newNode -> prev = NULL;
	strncpy(newNode->member,name,MAX_NAME_LENGTH+1);
	newNode -> usr = searchUsr(name);
	newNode -> lastFd = fd; //DEVO AGGIORNARLO AD OGNI CONNESSIONE //mi sa che non serve
	int position = hash_pjw((void*) group) % GROUPBUCKETS;
	lockAcquire(&mtx_group_tab[position]);
	groupData* groupEntry = icl_hash_find(groupTab,group);
	if(groupEntry->groupList == NULL) groupEntry->groupList = newNode;
	else {
		printf("ADDMEMBER: RAMO ELSE, utente in lastMember è %s\n", groupEntry->lastMember->member);
		//groupNode* lastMember = groupEntry -> lastMember;
		newNode -> prev = groupEntry -> lastMember;
		groupEntry -> lastMember -> next = newNode;
		groupEntry -> lastMember =  groupEntry -> lastMember -> next;
		}
	lockRelease(&mtx_group_tab[position]);
	return;
}


//ipotesi: l'utente è di sicuro già registrato (faccio il controllo prima di chiamare la funzione)
void sendMsg(char* usr, usrData* data, message_t* clientMsg, int type) {
	printf("SENDMSG\n");
	message_t newMsg;
	int len = ((clientMsg->data).hdr).len;
	printf("Len è %d\n", len);
	char buf[len];
	strncpy(buf,clientMsg->data.buf,len);
	printf("Buf è %s\n", buf);
	if(type == 0) setHeader(&(newMsg.hdr),TXT_MESSAGE,clientMsg->hdr.sender);
	else setHeader(&(newMsg.hdr),FILE_MESSAGE,clientMsg->hdr.sender);
	setData(&(newMsg.data),usr,buf,len);
	int hashVal = hash_pjw((void*) usr) % NBUCKETS;
	lockAcquire(&mtx_tab[hashVal]);
	if(isOn(usr)) {
		//CONTROLLARE WRITE E SISTEMARE LOCK
		printf("MANDO MESSAGGIO SENDMSG\n");
		//writeHeader((long) data->lastFd, &(newMsg.hdr));
		printf("Header scritto\n");
		printf("L'FD IN SENDMSG E' %d\n", data->lastFd);
		writeMsg((long) data->lastFd,&newMsg);
		lockAcquire(&mtx_stats_messages);
		if(type == 0) chattyStats.ndelivered++;
		else chattyStats.nfiledelivered++;
		lockRelease(&mtx_stats_messages);
		}
	else {
		lockAcquire(&mtx_stats_messages);
		if(type == 0) chattyStats.nnotdelivered++;
		else chattyStats.nfilenotdelivered++;
		lockRelease(&mtx_stats_messages);
		}
	printf("Messaggio mandato\n");
	int nextMsg = data->lastMsgIdx;
	(data->history[nextMsg]).hdr.op = clientMsg->hdr.op;
	strncpy((data->history[nextMsg]).hdr.sender,clientMsg->hdr.sender,MAX_NAME_LENGTH+1);
	strncpy((data->history[nextMsg]).data.hdr.receiver,clientMsg->data.hdr.receiver,MAX_NAME_LENGTH+1);
	(data->history[nextMsg]).data.hdr.len = clientMsg->data.hdr.len;
	(data->history[nextMsg]).data.buf = calloc(1,clientMsg->data.hdr.len);
	strncpy((data->history[nextMsg]).data.buf,clientMsg->data.buf,clientMsg->data.hdr.len);
	data->lastMsgIdx = (data->lastMsgIdx + 1) % MAXHISTMSGS;
	lockRelease(&mtx_tab[hashVal]);
	return;
}

void sendGroupMsg(char* group, groupData* data, message_t* msg, int type) {
	printf("SENDGROUPMSG\n");
	int hashVal = hash_pjw((void*) group) % GROUPBUCKETS;	
	lockAcquire(&mtx_group_tab[hashVal]);
	printf("Lock acquisita\n");
	groupNode* curr = data->groupList;
	printf("Curr->member è %s\n", curr->member);
	while(curr != NULL) {
		lockRelease(&mtx_group_tab[hashVal]);
		printf("SENDGROUPMSG: %s\n", curr->member);
		//if(strncmp(curr->member,msg->hdr.sender,MAX_NAME_LENGTH+1)!=0)
		printf("L'fd dell'utente è %d\n", curr->usr->lastFd);
		sendMsg(curr->member,curr->usr,msg,type);
		lockAcquire(&mtx_group_tab[hashVal]);
		curr = curr->next;
		}
	lockRelease(&mtx_group_tab[hashVal]);
	return;
}


int closeConnection(int fd) {
	lockAcquire(&mtx_set);
	FD_CLR(fd,&set);
	FD_CLR(fd,&checkset);
	if(fd == fd_num) fd_num--;
	lockRelease(&mtx_set);
	if(close(fd)!=0) return -1;
	else return 0;
}


//CONTROLLARE LE WRITE
void* worker() {
	printf("Attivato worker\n");
	sigset_t threadset;
	memset(&threadset,0,sizeof(sigset_t)); 
	int err, sig;
	if(sigfillset(&threadset)==-1) checkErrno("Sigfillset thread stats");
	if(sigdelset(&threadset,SIGSEGV)==-1) checkErrno("Sigdelset thread stats");
	if(sigdelset(&threadset,SIGINT)==-1) checkErrno("Sigdelset thread stats");
	//if(sigdelset(&threadset,SIGTERM)==-1) checkErrno("Sigdelset thread stats");
	if((err = pthread_sigmask(SIG_SETMASK,&threadset,NULL))!=0) checkError(err,"Set sigmask thread stats");
	signal(SIGPIPE, SIG_IGN);
	//dichiarazione variabili
	err = 0;
	int reqFd; //fd prelevato dalla coda
	int rRet, wRet; //valori di ritorno delle read/write
	op_t opRep; //codice di risposta al client 
	int lastNumOn; //usato per salvare il valore dell'ultimo numero di persone online
	while(1) {
		message_t clientMsg;
		message_t replyMsg;
		lockAcquire(&mtx_head);
		while(head == NULL) { //la coda dei descrittori è vuota
			condWait(&empty_queue,&mtx_head);
			//lockAcquire(&mtx_term);
			if(term == 1) {
				printf("WORKER: TERM E' 1\n");
				//lockRelease(&mtx_term);
				lockRelease(&mtx_head);
				printf("THREAD WORKER TERMINA\n");
				return (void*) 1; //controllare errore
				}
			//lockRelease(&mtx_term);
		}
		//thread svegliato da una signal
		printf("Worker sveglio\n");
		reqFd = connectionExtract();
		lockRelease(&mtx_head);
		if((rRet = readHeader(reqFd,&(clientMsg.hdr))) < 0) checkErrno("Lettura header richiesta");
		else if(rRet == 0) { //client ha chiuso la connessione
			printf("WORKER CHIUDE LA CONNESSIONE\n");
			disconnectUsr(reqFd);
			if(closeConnection(reqFd) != 0) checkErrno("Chiusura connessione");		
			}
		else {
			request req; //struttura dove salvo i dati della richiesta
			req.op = clientMsg.hdr.op;
			switch(req.op) {
			case POSTTXT_OP:
			case POSTTXTALL_OP:
			case POSTFILE_OP: 
			case GETFILE_OP:
			case CREATEGROUP_OP:
			case ADDGROUP_OP:
			case DELGROUP_OP: {
				printf("Leggo i dati\n");		
				if((rRet = readData(reqFd, &(clientMsg.data))) < 0) checkErrno("Lettura dati richiesta");
				strncpy(req.receiver,clientMsg.data.hdr.receiver,MAX_NAME_LENGTH+1);
				 } break;
			default:
				break;
			}
			strncpy(req.sender,(clientMsg.hdr).sender,MAX_NAME_LENGTH+1);
			req.hashVal = hash_pjw((void*) req.sender) % NBUCKETS;
			switch(req.op) {
				case REGISTER_OP: {
					printf("WORKER: Register OP\n");
					connectUsr(req.sender,reqFd);
					printf("Connessione utente completata\n");
					if(searchUsr(req.sender) != NULL) {	//l'utente è già registrato
						opRep = OP_NICK_ALREADY;
						lockAcquire(&mtx_stats_errors);
						chattyStats.nerrors++;
						lockRelease(&mtx_stats_errors);
						setHeader(&(replyMsg.hdr),opRep,"");
						lockAcquire(&mtx_tab[req.hashVal]); //perchè altri thread possono scrivere su questo fd
						writeHeader(reqFd,&(replyMsg.hdr));
						lockRelease(&mtx_tab[req.hashVal]);
						}
					else {	//l'utente non è registrato
						char* onBuf; //usato per scrivere la lista degli online
						char* copy = calloc(1,MAX_NAME_LENGTH+1);
						strncpy(copy,req.sender,MAX_NAME_LENGTH+1);
						usrData* data = newData(reqFd,copy);
						registerUsr(copy,data);
						printf("Registrazione utente completata\n");
						opRep = OP_OK;
						onBuf = checkOnList(&lastNumOn);
						setHeader(&(replyMsg.hdr),opRep,"");
						setData(&(replyMsg.data),"",onBuf,lastNumOn*(MAX_NAME_LENGTH+1));	
						lockAcquire(&mtx_tab[req.hashVal]);
						if(writeMsg(reqFd,&replyMsg) <= 0) checkErrno("Errore write");
						lockRelease(&mtx_tab[req.hashVal]);
						free(onBuf);
						}
					handleFd(reqFd);
					} break;
			case CONNECT_OP: {
				printf("WORKER: Connect OP\n");
				connectUsr(req.sender, reqFd); //lo connetto perchè al ciclo successivo verrà disconnesso
				if(searchUsr(req.sender) != NULL) {
					opRep = OP_OK;
					char* onBuf; //usato per scrivere la lista degli online
					onBuf = checkOnList(&lastNumOn);
					setHeader(&(replyMsg.hdr),opRep,"");
					setData(&(replyMsg.data),"",onBuf,lastNumOn*(MAX_NAME_LENGTH+1));
					usrData* usr = searchUsr(req.sender);
					usr -> lastFd = reqFd; //METTERE IN UNA FUNZIONE A PARTE
					lockAcquire(&mtx_tab[req.hashVal]);
					writeMsg(reqFd,&replyMsg);
					lockRelease(&mtx_tab[req.hashVal]);
					printf("CONNECTOP: LIBERO MEMORIA\n");
					free(onBuf);
					}
				else {
					printf("Connect OP: l'utente %s non è registrato\n", req.sender);
					opRep = OP_NICK_UNKNOWN;
					lockAcquire(&mtx_stats_errors);
					chattyStats.nerrors++;
					lockRelease(&mtx_stats_errors);
					setHeader(&(replyMsg.hdr),opRep,"");
					lockAcquire(&mtx_tab[req.hashVal]);
					writeHeader(reqFd,&(replyMsg.hdr));
					lockRelease(&mtx_tab[req.hashVal]);
					}
				handleFd(reqFd);
				} break;
			case POSTTXT_OP: {
				printf("WORKER: POSTTXT_OP\n");
				//strncpy(req.receiver,((clientMsg.data).hdr).receiver,MAX_NAME_LENGTH+1);
				usrData* recData;
				groupData* groupRecData;
				req.size = ((clientMsg.data).hdr).len;
				if(req.size > MAXMSGSIZE) {	//il messaggio è troppo lungo
					opRep = OP_MSG_TOOLONG;
					lockAcquire(&mtx_stats_errors);
					chattyStats.nerrors++;
					lockRelease(&mtx_stats_errors);
					printf("Messaggio troppo lungo\n");
					}
				else {
					recData = searchUsr(req.receiver);
					groupRecData = searchGroup(req.receiver);
					if(!recData && !groupRecData) {
						printf("UTENTE SCONOSCIUTO\n");
						opRep = OP_NICK_UNKNOWN;
						lockAcquire(&mtx_stats_errors);
						chattyStats.nerrors++;
						lockRelease(&mtx_stats_errors);
						}
					else if(recData) {
						opRep = OP_OK;
						sendMsg(req.receiver,recData,&clientMsg,0);
						}
					else {
						if(searchMember(req.receiver,req.sender)) {
							printf("SONO QUI\n");
							printf("Primo elemento gruppo %s\n", groupRecData->groupList->member);
							opRep = OP_OK;
							printf("FD E' %d\n", reqFd);
							sendGroupMsg(req.receiver,groupRecData,&clientMsg,0);
							}
						else opRep = OP_NICK_UNKNOWN; //l'utente non fa parte del gruppo
						}
					}
				setHeader(&(replyMsg.hdr),opRep,"");
				lockAcquire(&mtx_tab[req.hashVal]);
				writeHeader(reqFd,&(replyMsg.hdr));
				lockRelease(&mtx_tab[req.hashVal]);
				free(clientMsg.data.buf);
				printf("GESTISCO L'FD\n");
				handleFd(reqFd);
				} break;
			case POSTTXTALL_OP: {
				//strncpy(req.receiver,((clientMsg.data).hdr).receiver,MAX_NAME_LENGTH+1);
				printf("WORKER: PostTXTALL\n");
				req.size = ((clientMsg.data).hdr).len; 
				if(req.size > MAXMSGSIZE) {
					opRep = OP_MSG_TOOLONG;
					lockAcquire(&mtx_stats_errors);
					chattyStats.nerrors++;
					lockRelease(&mtx_stats_errors);
					}
				else {
					opRep = OP_OK;
					icl_entry_t* curr = userTab->buckets[0];
					for(int i = 0; i < NBUCKETS; i++) {
						lockAcquire(&mtx_tab[i]);
						curr = userTab->buckets[i];
						while(curr != NULL) {
							if(strncmp(curr->key,req.sender,MAX_NAME_LENGTH) != 0) {
								lockRelease(&mtx_tab[i]);
								sendMsg(curr->key,curr->data,&clientMsg,0);
								lockAcquire(&mtx_tab[i]);
								}
							curr = curr->next;
							}
						lockRelease(&mtx_tab[i]);
						}
					}
				setHeader(&(replyMsg.hdr),opRep,"");
				lockAcquire(&mtx_tab[req.hashVal]);
				writeHeader(reqFd,&(replyMsg.hdr));
				lockRelease(&mtx_tab[req.hashVal]);
				free(clientMsg.data.buf);
				handleFd(reqFd);
				} break;
			case POSTFILE_OP: {
				//strncpy(req.receiver,((clientMsg.data).hdr).receiver,MAX_NAME_LENGTH+1);
				printf("WORKER: Postfile\n");
				int len = clientMsg.data.hdr.len;
				//strncpy(req.receiver,clientMsg.data.hdr.receiver,MAX_NAME_LENGTH+1);
				//usrData* recData = searchUsr(req.receiver);
				char* fileName = calloc(1,len);
				char* newName = NULL;
				strncpy(fileName,clientMsg.data.buf,len);
				//ricevo il file
				printf("Ricevo il file dal client\n");
				message_t fileMsg;
				lockAcquire(&mtx_tab[req.hashVal]);
				if(readData(reqFd,&(fileMsg.data)) <= 0) checkErrno("Lettura messaggio file");
				lockRelease(&mtx_tab[req.hashVal]);
				printf("File ricevuto dal client\n");
				off_t fileSize = fileMsg.data.hdr.len; //filesize è in kilobytes quindi devo dividere per 2^10
				printf("La dimensione del file è %zu\n", fileSize);
				if(fileSize / 1024 > MAXFILESIZE) {
					printf("File troppo grande\n");
					lockAcquire(&mtx_stats_errors);
					chattyStats.nerrors++;
					lockRelease(&mtx_stats_errors);
					opRep = OP_MSG_TOOLONG;
					}
				else {
					opRep = OP_OK;
					usrData* recData;
					groupData* groupRecData;
					printf("FILENAME: %s\n", fileName);
					int slashPosition = -1;
					for(int i = 0; i < len-1; i++) {
						if(fileName[i] == '/') slashPosition = i;
					}
					if(slashPosition != -1) {
						int newLen = len-slashPosition+1+2; //2: ./
						newName = calloc(newLen,sizeof(char));
						newName[0] = '.';
						newName[1] = '/';
						strncpy(newName+2,fileName+slashPosition+1,newLen-2);
					}
					else {
						newName = calloc(len,sizeof(char));
						strncpy(newName,fileName,len);
					}
					printf("NUOVO FILENAME: %s\n", newName);
					//memorizzo il file nella cartella: salvo in un buffer la current directory
					char currDir[PATH_MAX];
					FILE* file;
					if(getcwd(currDir,PATH_MAX) == NULL) checkErrno("Get name directory corrente");
					//mi sposto nella directory dove salvare il file
					//se non esiste la cartella la creo
					if(chdir(DIRNAME) != 0) {
						if(mkdir(DIRNAME, 0666 | 0700) != 0) checkErrno("Creazione cartella");
						if(chdir(DIRNAME) != 0) checkErrno("Spostamento in cartella");
					}
					//if((file = fopen(fileName,"w")) == NULL) checkErrno("Creazione file");
					//if(chmod(fileName, 0700 | 0666)) perror("chmod");
					int fileop;
					if((fileop = open(newName, O_CREAT | O_TRUNC | O_RDWR, 0700 | 0666)) < 0) checkErrno("Apertura file");
					printf("DESCRITTORE DEL FILE: %d\n", fileop);
					int bw = 0;
					if((bw = write(fileop,fileMsg.data.buf,fileSize)) <= 0) checkErrno("Write file");
					//fflush(file);
					//chiudo il file
					//if(fclose(file) != 0) checkErrno("Chiusura file\n");
					close(fileop);
					if(chdir(currDir) != 0) checkErrno("Spostamento in directory d'origine");
					//notifico al receiver il file
					recData = searchUsr(req.receiver);
					groupRecData = searchGroup(req.receiver);
					if(!recData && !groupRecData) {
						opRep = OP_NICK_UNKNOWN;
						lockAcquire(&mtx_stats_errors);
						chattyStats.nerrors++;
						lockRelease(&mtx_stats_errors);
						}
					else if(recData) {
						opRep = OP_OK;
						sendMsg(req.receiver,recData,&clientMsg,1);
						}
					else {
						if(searchMember(req.receiver,req.sender)) {
							opRep = OP_OK;
							sendGroupMsg(req.receiver,groupRecData,&clientMsg,1);
							}
						else opRep = OP_NICK_UNKNOWN; //l'utente non fa parte del gruppo
						}
					}
				free(fileName);
				if(newName != NULL) free(newName);
				setHeader(&(replyMsg.hdr),opRep,"");
				lockAcquire(&mtx_tab[req.hashVal]);
				writeHeader(reqFd,&(replyMsg.hdr));
				lockRelease(&mtx_tab[req.hashVal]);
				free(clientMsg.data.buf);
				free(fileMsg.data.buf);
				handleFd(reqFd);
				} break;
			case GETFILE_OP: {
				//strncpy(req.receiver,((clientMsg.data).hdr).receiver,MAX_NAME_LENGTH+1);
				int len = clientMsg.data.hdr.len;
				char fileName[len];
				strncpy(fileName,clientMsg.data.buf,len);
				FILE* file = NULL;
				char currDir[PATH_MAX];
				if(getcwd(currDir,PATH_MAX) == NULL) checkErrno("Get name directory corrente");
				if(chdir(DIRNAME) != 0) checkErrno("Spostamento in directory");
				if((file = fopen(fileName,"r")) == NULL) {
					opRep = OP_NO_SUCH_FILE;
					lockAcquire(&mtx_stats_errors);
					chattyStats.nerrors++;
					lockRelease(&mtx_stats_errors);
					setHeader(&(replyMsg.hdr),opRep,"");
					lockAcquire(&mtx_tab[req.hashVal]);
					writeHeader(reqFd,&(replyMsg.hdr));
					lockRelease(&mtx_tab[req.hashVal]);
					}
				else {
					opRep = OP_OK;
					struct stat fileSt;
					if(stat(fileName,&fileSt) != 0) checkErrno("Stats file");
					int fileSize = fileSt.st_size;
					char* fileBuf = calloc(1,fileSize);
					clearerr(file);
					fread(fileBuf,1,(size_t) fileSize,file);
					int fErr;
					if((fErr = ferror(file)) != 0) checkError(fErr,"Lettura da file");
					if(fclose(file) != 0) checkErrno("Chiusura file\n");
					if(chdir(currDir) != 0) checkErrno("Spostamento in directory d'origine");
					setHeader(&(replyMsg.hdr),opRep,"");
					setData(&(replyMsg.data),"",fileBuf,fileSize);
					lockAcquire(&mtx_tab[req.hashVal]);
					writeMsg(reqFd,&replyMsg);
					lockRelease(&mtx_tab[req.hashVal]);
					free(fileBuf);
					}
				free(clientMsg.data.buf);
				handleFd(reqFd);
				} break;
			case GETPREVMSGS_OP: {
				printf("WORKER: GET PREVMSGS\n");
				usrData* data = searchUsr(req.sender);
				lockAcquire(&mtx_tab[req.hashVal]);
				printf("GETPRVMSGS: NUMERO MESSAGGI %d\n", data->lastMsgIdx);
				int nmsgs = data -> lastMsgIdx;
				lockRelease(&mtx_tab[req.hashVal]);
				int buf[1];
				buf[0] = nmsgs;
				printf("Buf è %d\n", *buf);
				size_t prova = *(size_t*) buf;
				if(nmsgs == 0) { 
					opRep = OP_FAIL;
					lockAcquire(&mtx_stats_errors);
					chattyStats.nerrors++;
					lockRelease(&mtx_stats_errors);
					setHeader(&(replyMsg.hdr),opRep,"");
					lockAcquire(&mtx_tab[req.hashVal]);
					writeHeader(reqFd,&(replyMsg.hdr));
					lockRelease(&mtx_tab[req.hashVal]);
					}
				else {
					printf("Numero di messaggi pendenti: %d\n", nmsgs);
					opRep = OP_OK;
					char newSen[MAX_NAME_LENGTH+1];
					char newRec[MAX_NAME_LENGTH+1];
					int len = 0;
					op_t newOp;
					setHeader(&(replyMsg.hdr),opRep,"");
					setData(&(replyMsg.data),"", (char*) buf, sizeof(int));
					lockAcquire(&mtx_tab[req.hashVal]);
					writeMsg(reqFd,&replyMsg); //CONTROLLI SULLE WRITE
					for(int i = 0; i < nmsgs; i++) {
						printf("MESSAGGIO NUMERO %d\n", i);
						newOp = (data->history[i]).hdr.op;
						strncpy(newSen,(data->history[i]).hdr.sender,MAX_NAME_LENGTH+1);
						strncpy(newRec,(data->history[i]).data.hdr.receiver,MAX_NAME_LENGTH+1);
						len = (data->history[i]).data.hdr.len;
						char* msg = calloc(1,len);
						printf("Il messaggio è %s\n",(data->history[i]).data.buf);
						strncpy(msg,(data->history[i]).data.buf,len);
						setHeader(&(replyMsg.hdr),newOp,newSen);
						setData(&(replyMsg.data),newRec,msg,len);
						writeMsg(reqFd,&replyMsg);
						free(msg);
					}
					lockRelease(&mtx_tab[req.hashVal]);
					}
				handleFd(reqFd);
				} break;
			case USRLIST_OP: {
				opRep = OP_OK;
				char* onBuf; //usato per scrivere la lista degli online
				onBuf = checkOnList(&lastNumOn);
				setHeader(&(replyMsg.hdr),opRep,"");
				setData(&(replyMsg.data),"",onBuf,lastNumOn*(MAX_NAME_LENGTH+1));	
				lockAcquire(&mtx_tab[req.hashVal]);
				writeMsg(reqFd,&replyMsg);
				lockRelease(&mtx_tab[req.hashVal]);
				free(onBuf);
				handleFd(reqFd);
				} break;
			case UNREGISTER_OP: {
				//devo cancellare la entry nella tabella hash
				printf("UNREGISTER OP\n");
				int ret = icl_hash_delete(userTab,req.sender,NULL,NULL);
				if(ret == -1) {
					opRep = OP_NICK_UNKNOWN;
					lockAcquire(&mtx_stats_errors);
					chattyStats.nerrors++;
					lockRelease(&mtx_stats_errors);
					}
				else {
					opRep = OP_OK;	
					lockAcquire(&mtx_stats_register);
					chattyStats.nusers--;
					lockRelease(&mtx_stats_register);
					}
				setHeader(&(replyMsg.hdr),opRep,"");
				lockAcquire(&mtx_tab[req.hashVal]);
				writeHeader(reqFd,&(replyMsg.hdr));
				lockRelease(&mtx_tab[req.hashVal]);
				handleFd(reqFd);	
				} break;
			case DISCONNECT_OP: {
				if(isOn(req.sender)) {
					opRep = OP_OK;
					disconnectUsr(reqFd);
					}
				else {
					opRep = OP_FAIL;
					lockAcquire(&mtx_stats_errors);
					chattyStats.nerrors++;
					lockRelease(&mtx_stats_errors);
					}
				setHeader(&(replyMsg.hdr),opRep,"");
				lockAcquire(&mtx_tab[req.hashVal]);
				writeHeader(reqFd,&(replyMsg.hdr));
				lockRelease(&mtx_tab[req.hashVal]);
				if(closeConnection(reqFd) != 0) checkErrno("Chiusura connessione");
				} break;
			case CREATEGROUP_OP: {
				//strncpy(req.receiver,((clientMsg.data).hdr).receiver,MAX_NAME_LENGTH+1);
				printf("WORKER: CREATE GROUP\n");
				//TABELLA HASH: COME KEY HO IL NOME DEL GRUPPO SCRITTA NEL BUFFER DEL MESSAGGIO
				groupData* data = newGroupData(req.sender); //GESTIRLO CON NULL
				printf("Dati gruppo creati\n");
				char* copy = calloc(1,MAX_NAME_LENGTH+1);
				strncpy(copy,req.receiver,MAX_NAME_LENGTH+1);
				if(createGroup(copy,data) == NULL) opRep = OP_NICK_ALREADY;
				else opRep = OP_OK;
				setHeader(&(replyMsg.hdr),opRep,"");
				lockAcquire(&mtx_tab[req.hashVal]);
				writeHeader(reqFd,&(replyMsg.hdr));
				lockRelease(&mtx_tab[req.hashVal]);
				handleFd(reqFd);
				} break;
			case ADDGROUP_OP: {
				//strncpy(req.receiver,((clientMsg.data).hdr).receiver,MAX_NAME_LENGTH+1);
				printf("WORKER: ADDGROUP\n");
				if(searchMember(req.receiver,req.sender) != NULL) opRep = OP_NICK_ALREADY; //o op fail?
				else { 
					opRep = OP_OK;
					addMember(req.receiver,req.sender,reqFd);
					}
				setHeader(&(replyMsg.hdr),opRep,"");
				lockAcquire(&mtx_tab[req.hashVal]);
				writeHeader(reqFd,&(replyMsg.hdr));
				lockRelease(&mtx_tab[req.hashVal]);
				handleFd(reqFd);
				} break;
			case DELGROUP_OP: {
				//strncpy(req.receiver,((clientMsg.data).hdr).receiver,MAX_NAME_LENGTH+1);
				printf("WORKER: DELGROUP\n");
				if(deleteMember(req.receiver,req.sender) == 0) opRep = OP_FAIL;
				else opRep = OP_OK;
				setHeader(&(replyMsg.hdr),opRep,"");
				lockAcquire(&mtx_tab[req.hashVal]);
				writeHeader(reqFd,&(replyMsg.hdr));
				lockRelease(&mtx_tab[req.hashVal]);
				handleFd(reqFd);
				} break;
			default:
				break;
			}
			
		}
	lockAcquire(&mtx_term);
	if(term==1) {
		lockRelease(&mtx_term);
		printf("WORKER STA USCENDO\n");
		return (void*) 1;
		}
	lockRelease(&mtx_term);
		
	}

	  
}
