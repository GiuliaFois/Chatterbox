/*
 * chatterbox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include "global_funcs.h"
#include "message.h"
#include "config.h"
#include "connections.h"


/**
 * @file  connections.c
 * @brief Contiene l'implementazione delle funzioni contenute
 *	  in connections.h
 */


int openConnection(char* path, unsigned int ntimes, unsigned int secs) {
	printf("Client apre la connessione\n");
	if(ntimes > MAX_RETRIES || secs > 3) {
		printf("Parametri non ammessi\n");
		exit(EXIT_FAILURE);
		}
	int fd_c; //descrittore per il socket usato dal client
	struct sockaddr_un adsock;
	strncpy(adsock.sun_path,path,UNIX_PATH_MAX);
	adsock.sun_family=AF_UNIX;
	if((fd_c=socket(AF_UNIX,SOCK_STREAM,0))==-1) {
		perror("Apertura del socket");
		exit(EXIT_FAILURE); 
		}
	for(int i=0; i<ntimes; i++) {
		if((connect(fd_c, (struct sockaddr*) &adsock,sizeof(adsock)))==-1) {
			if(errno == ENOENT) //socket non esistente
				sleep(secs);
			else return -1; //in caso di errore
			}
		else break;
		}
	return fd_c; //ritorna il descrittore associato alla connessione
	printf("Apertura della connessione: riuscita\n");
	}

// -------- server side ----- 


int readHeader(long connfd, message_hdr_t *hdr) {
	size_t btr = sizeof(message_hdr_t); //numero di bytes da leggere
	//printf("Byte da leggere: %d\n", btr);
	ssize_t br = 0; //bytes letti
	message_hdr_t *original = hdr;
	while(btr > 0) {
		if((br = read(connfd, (void*) hdr,btr)) <= 0) {
			if(errno == ECONNRESET) return 0; //cancellare ignora SIGPIPE
			if(errno != EINTR) return br;
			}
		else {
			//printf("Byte letti: %d\n", br);
			hdr += br;
			btr -= (size_t) br;
			}
		}
	hdr = original;
	//printf("hdr-> op è: %d e hdr->sender è: %s\n", hdr->op, hdr->sender);
	return 1;
}

int readDataHdr(long fd, message_data_hdr_t* data) {
	printf("Sono in readdatahdr\n");
	size_t btr = sizeof(message_data_hdr_t); //numero di bytes da leggere (inizialmente solo l'header dati)
	ssize_t br = 0; //bytes letti
	ssize_t bret = 0; //valore di ritorno della read
	size_t len = 0; //lunghezza del messaggio
	//char* buf; //buffer per allocare il messaggio
	while(btr > 0) {
		if((bret = read(fd, (&(data) + br) ,btr)) <= 0) {
			if(errno != EINTR) return bret;
			}
		else {
			printf("READDATAHDR: Byte header letti %d\n", bret);
			br += bret;
			btr -= (size_t) bret;
			}

		}
	return 1;
}

int readData(long fd, message_data_t *data) {
	size_t btr = sizeof(message_data_hdr_t); //numero di bytes da leggere (inizialmente solo l'header dati)
	ssize_t br = 0; //bytes letti
	ssize_t bret = 0; //valore di ritorno della read
	size_t len = 0; //lunghezza del messaggio
	//char* buf; //buffer per allocare il messaggio
	while(btr > 0) {
		if((bret = read(fd,(&(data->hdr) + br) ,btr)) <= 0) {
			if(errno != EINTR) return bret;
			}
		else {
			printf("READDATA: Byte header letti %d\n", bret);
			br += bret;
			btr -= (size_t) bret;
			}

		}
	printf("Sono pronto a leggere il buffer\n");
	/*ora in hdr-> len ho la lunghezza del messaggio*/
	len = data->hdr.len;
	printf("READDATA: LEN E' %d\n", len);
	data->buf = calloc(1,len);
	char* original = data->buf;
	btr = len;
	bret = 0;
	//printf("Byte da leggere in READDATA/buffer: %d\n",btr);
	while(btr > 0) {
		if((bret = read(fd, (void*) data->buf,btr)) <= 0) {
			//printf("Byte letti (ramo if): %d\n", bret);
			if(errno != EINTR) return bret;
			else printf("Interrotta\n");
			}
		else {
			//printf("READDATA: Byte buffer letti %d\n", bret);
			data->buf += bret;
			btr -= (size_t) bret;
			}
		}
	data->buf = original;
	return 1;
}

int readMsg(long fd, message_t *msg) {
	ssize_t ret;
	if((ret = readHeader(fd,&(msg->hdr))) <= 0) return ret;
	if((ret = readData(fd,&(msg->data))) <= 0) return ret;
	return 1;
}

//DA SPECIFICARE NEL FILE.H
int writeHeader(long fd, message_hdr_t* hdr) {
	size_t btw = sizeof(message_hdr_t); //numero di bytes da scrivere
	ssize_t bw = 0; //bytes scritti
	while(btw > 0) {
		if((bw = write(fd,hdr,btw)) <= 0) {
			if(errno != EINTR) return bw;
			}
		else {
			hdr += bw;
			btw -= (size_t) bw;
			//printf("WRITEHEADER: BYTE SCRITTI %d\n", bw);
			}
		}
	return 1;
}

int writeData(long fd, message_data_t* data) {
	size_t btw = sizeof(message_data_hdr_t); //numero di bytes da scrivere (inizialmente l'header)
	ssize_t bw = 0; //bytes letti
	ssize_t bret = 0; //valore di ritorno della write
	size_t len = data->hdr.len; //lunghezza del messaggio
	//printf("La lunghezza dei dati è %d\n", len);
	while(btw > 0) {
		if((bret = write(fd,(&(data->hdr) + bw) ,btw)) <= 0) {
			if(errno != EINTR) return bret;
			}
		else {
			bw += bret;
			btw -= (size_t) bw;
			}
		}
	btw = len;
	bret = 0;
	//printf("WRITEDATA: BUFFER E' %d\n", len);
	while(btw > 0) {
		if((bret = write(fd,(void*) data->buf,btw)) <= 0) {
			if(errno != EINTR) return bret;
			}
		else {
			//printf("WRITEDATA: DATI SCRITTI %d\n", bret);
			data->buf += bret;
			btw -= (size_t) bret;
			}
		}
	//printf("WRITEDATA TERMINA\n");
	return 1;
}

int writeMsg(long fd, message_t* msg) {
	ssize_t ret;
	if((ret = writeHeader(fd,&(msg->hdr))) <= 0) return ret;
	//printf("Scritto header\n");
	if((ret = writeData(fd,&(msg->data))) <= 0) return ret;
	//printf("Scritti dati\n");
	return 1;	
}

// ------- client side ------

int sendRequest(long fd, message_t *msg) {
	size_t btow = sizeof(message_hdr_t); //numero di bytes da scrivere
	ssize_t bw = 0; //numero di bytes scritti
	ssize_t bret = 0;
	while(btow > 0) {
		if((bret = write(fd, (&(msg->hdr) + bw) ,btow)) <= 0) { 
			if(errno != EINTR) return bret;
			}
		else {
			//printf("SEND REQUEST: BYTE SCRITTI %d\n",bret);
			btow -= (size_t) bret;
			bw += bret;
		}
	}
	switch(msg->hdr.op) {
	case POSTTXT_OP:
	case POSTTXTALL_OP:
	case POSTFILE_OP: 
	case GETFILE_OP: 
	case CREATEGROUP_OP:
	case ADDGROUP_OP:
	case DELGROUP_OP: {
		sendData(fd,&(msg->data));
		} break;
	default:
		break;
	} 
	return 1;
}

int sendData(long fd, message_data_t *msg) {
	//printf("CLIENT ENTRA IN SENDDATA\n");
	int btow = sizeof(message_data_hdr_t); //numero di bytes da scrivere
	int bw = 0; //numero di bytes scritti
	int bret;
	size_t len = msg->hdr.len;
	printf("SENDDATA: 1 LEN E' %d\n", len);
	while(btow > 0) {
		if((bret = write(fd, (void*) (&(msg->hdr)+bw) ,btow)) <= 0) {
			if(errno != EINTR) return bret;
			}
		else {
			//printf("SENDDATA: HO MANDATO %d DATI\n", bret);
			btow -= (size_t) bret;
			bw += bret;
		}	
	}
	btow = len;
	printf("SENDDATA: LEN E' %d\n", btow);
	bret = 0;
	//printf("SENDDATA: MANDO %d DATI\n", btow);
	//char* newbuf = calloc(len,sizeof(char));
	//char* tofree = newbuf;
	//strncpy(newbuf,msg->buf,len);
	//newbuf[len-1] = msg->buf[len-1];
	char* original = msg->buf;
	while(btow > 0) {
		if((bret = write(fd, msg->buf,btow)) <=0) {
			if(errno != EINTR) return bret;
			}
		else {
			printf("SENDDATA INTERROTTA: NE HO MANDATI %d\n", bret);
			btow -= (size_t) bret;
			msg->buf += bret;
		}
	}
	msg->buf = original;
	//free(tofree);
	//printf("SENDDATA TERMINA\n");
	return 1;
}




