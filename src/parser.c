#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "parser.h"

//metto prima le stringhe
void compNassign(char* tocomp,char* val) {
	long lval; //per convertire la stringa in long
	if((strcmp(tocomp,"UnixPath"))==0) strncpy(UNIXPATH,val,strlen(val)+1);
	else if((strcmp(tocomp,"DirName"))==0) strncpy(DIRNAME,val,strlen(val)+1);
	else if((strcmp(tocomp,"StatFileName"))==0) strncpy(STATFILENAME,val,strlen(val)+1);
	else if((strcmp(tocomp,"MaxConnections"))==0) { 
		lval=strtol(val,NULL,10); 
		MAXCONNECTIONS=lval;
		}
	else if((strcmp(tocomp,"ThreadsInPool"))==0) {
		lval=strtol(val,NULL,10);
		THREADSINPOOL=lval;
		}
	else if((strcmp(tocomp,"MaxMsgSize"))==0) {
		lval=strtol(val,NULL,10);
		MAXMSGSIZE=lval;
		}
	else if ((strcmp(tocomp,"MaxFileSize"))==0) {
		lval=strtol(val,NULL,10);
		MAXFILESIZE=lval;
		}
	else if((strcmp(tocomp,"MaxHistMsgs"))==0) {
		lval=strtol(val,NULL,10);
		MAXHISTMSGS=lval;
		}
	}

int parser(char* conffile) {
	FILE* f;
	if((f=fopen(conffile,"r"))==NULL) return -1; //check di errno in main
	char buf[CONFLINE+2]; /*N+2: uno per \n, uno per \0*/
	char string[LENPAR]; //nome parametro di configurazione
	char val[LENPAR]; //valore parametro di configurazione
	while(fgets(buf,CONFLINE+2,f)!=NULL) { 
		if(buf[0]!='#' && buf[0]!='\n' && buf[0]!=' ') {
			sscanf(buf,"%s %*[=] %s",string,val); //non considero '='
			compNassign(string,val);
			}
		}
	if(fclose(f)!=0) return -1; //check di errno nel main
	return 0;
	}
