#ifndef PARSER_H_
#define PARSER_H_


#include "config.h"


/*------variabili globali di configurazione------*/
char UNIXPATH[CONFLINE];
long MAXCONNECTIONS;
long THREADSINPOOL;
long MAXMSGSIZE;
long MAXFILESIZE;
long MAXHISTMSGS;
char DIRNAME[CONFLINE];
char STATFILENAME[CONFLINE];

/*inserire specifica*/
void compNassign(char* tocomp,char* val);
int parser(char* conffile);

#endif
