#include<stdlib.h>
#include<stdio.h>  
#include<poll.h>  
#include<netdb.h>
#include<sys/types.h>  
#include<sys/socket.h>  
#include<arpa/inet.h>  
#include<unistd.h>  
#include<string.h>  
#include<errno.h>
#include<assert.h>

#include "../Command.h"

#define DEBUG            0
#define BUFFER_SIZE      65536
#define MAX_NODE_COUNT   1024
#define MAX_CLIENT_COUNT 128


char      outbuf[BUFFER_SIZE];  

int commandno = 1;         //used to set the serial number for each command.

struct doom_dh_node
{
    int   id;	
    int   socket;
    char  ip[16];
    int   serverport;   
    int   nodeport;
    
    struct doom_dh_command_node *firstcommand,*lastcommand;
    char  buffer[BUFFER_SIZE];
    int   bufferlength;
};
struct    doom_dh_node*  pchildnodes[MAX_NODE_COUNT];

struct doom_dh_client
{
    int   socket;
    char  ip[16];
    int   port;
    
    struct doom_dh_command_node *firstcommand,*lastcommand;
    char  buffer[BUFFER_SIZE];
    int   bufferlength;
};
struct    doom_dh_client*  pclients[MAX_CLIENT_COUNT];


void dumpbuffer(FILE *f, char *buffer, int length)
{
    int i;
    char str[16];
    for(i = 0; i < length; i++)
    {
        sprintf(str, "%02x", buffer[i]);
        fprintf(f, "%s ", str + strlen(str) - 2);
        if((i + 1) % 32 == 0)
        {
            printf("\n");
        }
    }
    printf("\n");
};

void add_childcommand(struct doom_dh_node* pnode, struct doom_dh_command_node *pcommand)
{
    if(pnode->firstcommand == NULL)
    {
        pnode->firstcommand = pnode->lastcommand = pcommand;
    }
    else
    {
        pnode->lastcommand->next = pcommand;
        pcommand->prev = pnode->lastcommand;
        pnode->lastcommand = pcommand;
    }
}

void add_command(struct doom_dh_client* pclient, struct doom_dh_command_node *pcommand)
{
    if(pclient->firstcommand == NULL)
    {
        pclient->firstcommand = pclient->lastcommand = pcommand;
    }
    else
    {
        pclient->lastcommand->next = pcommand;
        pcommand->prev = pclient->lastcommand;
        pclient->lastcommand = pcommand;
    }
}

struct doom_dh_command_node *get_command(int commandno, struct doom_dh_client **ppcli)
{
    int i;
    struct doom_dh_command_node* pcommand = NULL;
    for(i = 0; i < MAX_CLIENT_COUNT; i++)
    {
        if(pclients[i] != NULL)
        {
            pcommand = pclients[i]->firstcommand;
            while(pcommand != NULL)
            {
                if(pcommand->command.no == commandno)
                {
                    *ppcli = pclients[i];
                    return pcommand;
                }
                pcommand = pcommand->next;
            }
        }
    }
    *ppcli = NULL;
    return NULL;
}

struct doom_dh_node* add_child_node(char* buffer, int server)
{
    int j, ret;
    char *ip;
    char *serverport, *nodeport;
    struct sockaddr_in nodeaddr;  

    ip = buffer;

    serverport = ip;
    while(*serverport != ' ') 
    {
        if(*serverport == '\0')
        {
            sprintf(outbuf + 4, "error:wrong child node format: %s\n", buffer);
            outbuf[0]              = COMMAND_ADDCHILDNODE;
            outbuf[1]              = COMMAND_ACTION_RETOUT_CLIENT;
            *(short*)(outbuf + 2)  = strlen(outbuf + 4) + 4 + 1; // + 1 for '\0'
            send(server,outbuf, *(short*)(outbuf + 2), 0);
            return NULL;
        }
        serverport++;
    }
    *serverport = '\0';
    serverport++;
    
    nodeport = serverport + 1;
    while(*nodeport != ' ') 
    {
        if(*nodeport == '\0')
        {
            sprintf(outbuf + 4, "error:wrong child node format: %s\n", buffer);
            outbuf[0]              = COMMAND_ADDCHILDNODE;
            outbuf[1]              = COMMAND_ACTION_RETOUT_CLIENT;
            *(short*)(outbuf + 2)  = strlen(outbuf + 4) + 4 + 1; // + 1 for '\0'
            send(server,outbuf, *(short*)(outbuf + 2), 0);
            return NULL;
        }
        nodeport++;
    }
    *nodeport = '\0';
    nodeport++;
    
    if(DEBUG)
    {
        FILE *f = fopen("./debug.log", "a+");
        fprintf(f, "add_child_node %s %s %s\n", ip, serverport, nodeport);
        fclose(f);
    }     

    for(j = 0; j < MAX_NODE_COUNT; j++)
    {
        if(pchildnodes[j] != NULL)
        {
            if( strcmp(pchildnodes[j]->ip, ip) == 0 && 
            	  atoi(serverport) == pchildnodes[j]->serverport ||
            	  atoi(serverport) == pchildnodes[j]->nodeport ||
            	  atoi(nodeport)   == pchildnodes[j]->serverport ||
            	  atoi(nodeport)   == pchildnodes[j]->nodeport )
            {
                break;
            }
        }
    }
    if(j < MAX_NODE_COUNT)
    {
        sprintf(outbuf + 4, "error:Failed to add child node %s %s %s, node was already connected, or port was already used", ip, serverport, nodeport);
        outbuf[0]              = COMMAND_ADDCHILDNODE;
        outbuf[1]              = COMMAND_ACTION_RETOUT_CLIENT;
        *(short*)(outbuf + 2)  = strlen(outbuf + 4) + 4 + 1; // + 1 for '\0'
        send(server,outbuf, *(short*)(outbuf + 2), 0);
        return NULL;
    }
    for(j = 0; j < MAX_NODE_COUNT; j++)
    {
        if(pchildnodes[j] == NULL)
        {
            pchildnodes[j] = (struct doom_dh_node*)malloc(sizeof(struct doom_dh_node));
            break;
        }
    }
                    
    if(j < MAX_NODE_COUNT)
    {
        strcpy(pchildnodes[j]->ip, ip);
        pchildnodes[j]->serverport    = atoi(serverport);
        pchildnodes[j]->nodeport      = atoi(nodeport);
        pchildnodes[j]->socket        = socket(AF_INET,SOCK_STREAM,0);
        pchildnodes[j]->id            = -1;
        pchildnodes[j]->firstcommand  = NULL;
        pchildnodes[j]->lastcommand   = NULL;
        pchildnodes[j]->bufferlength  = 0;
        
        nodeaddr.sin_family =AF_INET;  
        nodeaddr.sin_port   = htons(pchildnodes[j]->serverport);
        nodeaddr.sin_addr.s_addr=inet_addr(ip);
        ret = connect(pchildnodes[j]->socket,(struct sockaddr*)&nodeaddr,sizeof(nodeaddr));
        if(ret < 0)
        {
            free(pchildnodes[j]);
            pchildnodes[j] = NULL;
            sprintf(outbuf + 4, "error:Failed to add child node, %s:%s\n", ip, serverport);
            outbuf[0]              = COMMAND_ADDCHILDNODE;
            outbuf[1]              = COMMAND_ACTION_RETOUT_CLIENT;
            *(short*)(outbuf + 2)  = strlen(outbuf + 4) + 4 + 1; // + 1 for '\0'
            send(server,outbuf, *(short*)(outbuf + 2), 0);
            return NULL;
        }
        else
        {
            sprintf(outbuf + 4, "info:New node added, %s:%s\n", ip, serverport);
            outbuf[0]              = COMMAND_ADDCHILDNODE;
            outbuf[1]              = COMMAND_ACTION_RETOUT_CLIENT;
            *(short*)(outbuf + 2)  = strlen(outbuf + 4) + 4 + 1; // + 1 for '\0'
            send(server,outbuf, *(short*)(outbuf + 2), 0);
            return pchildnodes[j];
        }
    }
    else
    {
        sprintf(outbuf + 4, "error:Failed to add child node, too many nodes");
        outbuf[0]              = COMMAND_ADDCHILDNODE;
        outbuf[1]              = COMMAND_ACTION_RETOUT_CLIENT;
        *(short*)(outbuf + 2)  = strlen(outbuf + 4) + 4 + 1; // + 1 for '\0'
        send(server,outbuf, *(short*)(outbuf + 2), 0);
        return NULL;
    }
};

void handle_node_response(int index)
{
    int i, ret;
    struct doom_dh_client* pcli = NULL;
    char *start;
    struct doom_dh_command_node *pcommand;

    ret = recv(pchildnodes[index]->socket,pchildnodes[index]->buffer + pchildnodes[index]->bufferlength,BUFFER_SIZE - pchildnodes[index]->bufferlength,0);
    if(ret == 0)
    {   
        close(pchildnodes[index]->socket);
        printf("Node connection %s:%d closed!\n", pchildnodes[index]->ip, pchildnodes[index]->serverport);
        free(pchildnodes[index]);
        pchildnodes[index] = NULL;
    }
    else if(ret < 0)
    {
        printf("error recieve data, errno = %d\n", errno);
    }
    //ret > 0
    pchildnodes[index]->bufferlength += ret;
    if(DEBUG)
    {
        FILE *f = fopen("./debug.log", "a+");
        fprintf(f, "handle_node_response: pchildnodes[%d]->buffer = \n", index);
        dumpbuffer(f, pchildnodes[index]->buffer, pchildnodes[index]->bufferlength);
        fclose(f);
    }
    if(	pchildnodes[index]->buffer[0] == COMMAND_QUIT )
	  {
        close(pchildnodes[index]->socket);
        printf("Connection %s:%d closed!\n", pchildnodes[index]->ip, pchildnodes[index]->serverport);
        free(pchildnodes[index]);
        pchildnodes[index] = NULL;
    }
    
    i = 0;
    while(1)
    {
		    start  = pchildnodes[index]->buffer + i;
		    if( pchildnodes[index]->bufferlength < i + 4 ||
		    	  pchildnodes[index]->bufferlength < i + *(short*)(start + 2))
		    {//command not finished
		        break;
		    }
        
        if(DEBUG)
        {
            FILE *f = fopen("./debug.log", "a+");
            fprintf(f, "handle_node_response: start = \n");
            dumpbuffer(f, start, *(short*)(start + 2));
            fclose(f);
        }

 	      pcommand = get_command(pchildnodes[index]->firstcommand->command.parentno, &pcli);
 	      assert(pcommand != NULL);

        if(start[1] ==  COMMAND_ACTION_RETSTOP)
        {   
            //the fisrt command of this node is finished, before removing it, we need handle its parent command
            
        	  //for parent command: waitingresult-- 
        	  pcommand->command.waitingresult--;
        	  if(pcommand->command.waitingresult == 0)
        	  {

                struct timeval endtime;
                gettimeofday(&endtime, NULL);
                long int sec, usec;
                sec  = endtime.tv_sec - pcommand->command.starttime.tv_sec;
                usec = endtime.tv_usec - pcommand->command.starttime.tv_usec;
                if(usec < 0)
                {
                    sec --;
                    usec = 1000000 + usec;
                }
                sprintf(outbuf + 4, "%ldm%ldus\n", sec, usec);
                outbuf[0] = pcommand->command.id;
                outbuf[1] = COMMAND_ACTION_RETSTOP;
                *(short*)(outbuf + 2) = strlen(outbuf + 4) + 4 + 1; //+1 for "\0"
                send(pcli->socket,outbuf, *(short*)(outbuf + 2), 0);


        	      //remove pcommand;
        	      if(pcommand->prev == NULL)
        	      {
        	          pcli->firstcommand = pcommand->next;
        	          if(pcli->firstcommand != NULL)
        	          {
        	              pcli->firstcommand->prev = NULL;
        	          }
        	      }
        	      else
        	      {
        	          pcommand->prev->next = pcommand->next;
        	          if(pcommand->next != NULL) //pcommand is not last
        	          {
                        pcommand->next->prev = pcommand->prev;
        	          }
        	          else
        	          {
        	              pcli->lastcommand = pcommand->prev;
        	          }
        	      }
  	            free(pcommand);
        	  }
        	  
        	  //remove fisrt command of this node
            assert(pchildnodes[index]->firstcommand != NULL);
            pcommand =pchildnodes[index]->firstcommand;
            pchildnodes[index]->firstcommand = pchildnodes[index]->firstcommand->next;
            if(pchildnodes[index]->firstcommand != NULL)
            {
                pchildnodes[index]->firstcommand->prev = NULL;
            }
            free(pcommand);
        }
        else if(start[1] == COMMAND_ACTION_RETOUT_CLIENT)
        {
            outbuf[0] = start[0];
            outbuf[1] = start[1];
            sprintf(outbuf + 4, "(%s:%d)%s", pchildnodes[index]->ip, pchildnodes[index]->serverport, start + 4);
            *(short*)(outbuf + 2) = strlen(outbuf + 4) + 4 + 1; //+1 for '\0'
            send(pcli->socket,outbuf, *(short*)(outbuf + 2), 0);
        }
        else if(start[1] == COMMAND_ACTION_RETOUT_SERVER)
        {
            if(pchildnodes[index]->firstcommand->command.id == COMMAND_GETNODEINFO &&
        	       strncasecmp(start + 4, "ID:", strlen("ID:")) == 0 )
            {
                pchildnodes[index]->id = atoi(start + 4 + strlen("ID:"));
            }
            printf("(%s:%d)%s", pchildnodes[index]->ip, pchildnodes[index]->serverport, start + 4);
        }
        
        i += *(short*)(start + 2);
    }
    if(i < pchildnodes[index]->bufferlength)
    {
        memmove(pchildnodes[index]->buffer, pchildnodes[index]->buffer + i, pchildnodes[index]->bufferlength - i);
    }
    pchildnodes[index]->bufferlength -= i;
    
}

void handle_client_request(int index)
{
		int  i, j, ret;
    char *start;
    struct doom_dh_command_node *pcommand, *pchildcommand;    

    ret = recv(pclients[index]->socket, pclients[index]->buffer + pclients[index]->bufferlength, BUFFER_SIZE - pclients[index]->bufferlength,0);
    if(ret == 0)
    {   
        close(pclients[index]->socket);
        printf("Client connection %s:%d closed!\n", pclients[index]->ip, pclients[index]->port);
        free(pclients[index]);
        pclients[index] = NULL;
        return;
    }
    else if(ret < 0)
    {
        printf("error recieve data, errno = %d\n", errno);
        return;
    }
    //ret > 0
    pclients[index]->bufferlength += ret;
    if(DEBUG)
    {
        FILE *f = fopen("./debug.log", "a+");
        fprintf(f, "handle_client_request pclients[%d] ret = %d, bufferlength = %d, buffer =\n", index, ret, pclients[index]->bufferlength);
        dumpbuffer(f, pclients[index]->buffer, pclients[index]->bufferlength);
        fclose(f);
    }

    if( pclients[index]->buffer[0] == COMMAND_QUIT)
    {
        close(pclients[index]->socket);
        printf("Connection %s:%d closed!\n", pclients[index]->ip, pclients[index]->port);
        free(pclients[index]);
        pclients[index] = NULL;
    }
    
    i = 0;
    while(1)
    {
		    start  = pclients[index]->buffer + i;
		    if( pclients[index]->bufferlength < i + 4 ||
		    	  pclients[index]->bufferlength < i + *(short*)(start + 2))
		    {//command not finished
		        break;
		    }

        if(DEBUG)
        {
            FILE *f = fopen("./debug.log", "a+");
            fprintf(f, "handle_client_request: pclient[%d], start = \n", index);
            dumpbuffer(f, start, *(short*)(start + 2));
            dump_command_status(pclients[index]->firstcommand);
            fclose(f);
        }

        if(pclients[index]->lastcommand == NULL || //no command
        	 pclients[index]->lastcommand->command.waitinginput == 0) //finish input for last command
        {
            if(start[1] == COMMAND_ACTION_EXESTART)
            {
                //create a new command
                pcommand = (struct doom_dh_command_node *)malloc(sizeof(struct doom_dh_command_node));
                pcommand->command.no            = commandno++;
                pcommand->command.parentno      = -1; //this is already a parent command
                pcommand->command.waitinginput  = 0;
                pcommand->command.waitingresult = 0;
                pcommand->prev = pcommand->next = NULL;
                pcommand->command.id            = start[0];
                gettimeofday(&pcommand->command.starttime, NULL);
            
                if(pcommand->command.id == COMMAND_ADDCHILDNODE)
                {
                    pcommand->command.waitinginput = 1;
                    for(j = 0; j < MAX_NODE_COUNT; j++)
                    {
                        if(pchildnodes[j] != NULL)
                        {
                            outbuf[0] = COMMAND_ADDSIBLINGNODE;
                            outbuf[1] = COMMAND_ACTION_EXESTART;
                            *(short*)(outbuf+2) = 4;
                            send(pchildnodes[j]->socket,outbuf, 4, 0);

                            pchildcommand = (struct doom_dh_command_node *)malloc(sizeof(struct doom_dh_command_node));
                            pchildcommand->command.no            = commandno++;
                            pchildcommand->command.parentno      = pcommand->command.no;
                            pchildcommand->command.waitinginput  = 1;
                            pchildcommand->command.waitingresult = 1;
                            pchildcommand->prev = pchildcommand->next = NULL;
                            pchildcommand->command.id = pcommand->command.id;
                        
                            add_childcommand(pchildnodes[j], pchildcommand);
                            pcommand->command.waitingresult++;
                        }
                    }
                    add_command(pclients[index], pcommand);
                }
                else if(pcommand->command.id == COMMAND_GETNODEINFO||
                	       pcommand->command.id == COMMAND_CREATEDATABASE ||
                         pcommand->command.id == COMMAND_SETDATABASE ||
                         pcommand->command.id ==  COMMAND_EXECUTEDDL ||
                	       pcommand->command.id ==  COMMAND_EXECUTEDML ||
                	       pcommand->command.id ==  COMMAND_EXECUTEDQL ||
                	       pcommand->command.id ==  COMMAND_IMPORTCSV //import csv into [table]
                	      )
                {
                    pcommand->command.waitinginput = 1;
                    for(j = 0; j < MAX_NODE_COUNT; j++)
                    {
                        if(pchildnodes[j] != NULL)
                        {
                            outbuf[0] = pcommand->command.id;
                            outbuf[1] = COMMAND_ACTION_EXESTART;
                            *(short*)(outbuf+2) = *(short*)(start + 2);
                            memcpy(outbuf + 4, start + 4, *(short*)(start + 2) - 4);
                            send(pchildnodes[j]->socket,outbuf, *(short*)(start + 2), 0);

                            pchildcommand = (struct doom_dh_command_node *)malloc(sizeof(struct doom_dh_command_node));
                            pchildcommand->command.no            = commandno++;
                            pchildcommand->command.parentno      = pcommand->command.no;
                            pchildcommand->command.waitinginput  = 1;
                            pchildcommand->command.waitingresult = 1;
                            pchildcommand->prev = pchildcommand->next = NULL;
                            pchildcommand->command.id = pcommand->command.id;

                            add_childcommand(pchildnodes[j], pchildcommand);
                            pcommand->command.waitingresult++;
                        }
                    }
                    add_command(pclients[index], pcommand);
                }
                else
                {
                    printf("error command %d\n", pcommand->command.id);
                    free(pcommand);
                }
            }
            else
            {
                printf("the server is waiting for starting a command, but the input command action is %d\n", start[1]);
            }
        }
        else if(pclients[index]->lastcommand != NULL && pclients[index]->lastcommand->command.waitinginput == 1) //waiting for input
        {
            if(pclients[index]->lastcommand->command.id == COMMAND_ADDCHILDNODE)	
            {
                struct doom_dh_node* pnode;
                if(start[1] == COMMAND_ACTION_EXEINPUT)
                {
                    if((pnode = add_child_node(start + 4, pclients[index]->socket))!=NULL)
                    {
                        for(j = 0; j < MAX_NODE_COUNT; j++)
                        {
                            if(pchildnodes[j] != NULL)
                            {
                                if(pchildnodes[j] != pnode)
                                {//add sibling node
                                    
                                    outbuf[0] = COMMAND_ADDSIBLINGNODE;
                                    outbuf[1] = COMMAND_ACTION_EXEINPUT;
                                    sprintf(outbuf + 4, "%s:%d\n", pnode->ip, pnode->nodeport);
                                    *(short*)(outbuf + 2) = strlen(outbuf + 4) + 4 + 1;//+1 for '\0'
                                    send(pchildnodes[j]->socket,outbuf, *(short*)(outbuf + 2), 0);
                                }
                                else
                                {   
                                	  //get node id
                                    outbuf[0] = COMMAND_GETNODEINFO;
                                    outbuf[1] = COMMAND_ACTION_EXESTART;
                                    *(short*)(outbuf + 2) = 4;
                                    send(pchildnodes[j]->socket, outbuf, 4,0);

                                    pchildcommand = (struct doom_dh_command_node *)malloc(sizeof(struct doom_dh_command_node));
                                    pchildcommand->command.no            = commandno++;
                                    pchildcommand->command.parentno      = pclients[index]->lastcommand->command.no;
                                    pchildcommand->command.waitinginput  = 1;
                                    pchildcommand->command.waitingresult = 1;
                                    pchildcommand->prev = pchildcommand->next = NULL;
                                    pchildcommand->command.id = COMMAND_GETNODEINFO;
                                    add_childcommand(pchildnodes[j], pchildcommand);

                                    pcommand->command.waitingresult++;

                                    sprintf(outbuf + 4, "ID");
                                    outbuf[0] = COMMAND_GETNODEINFO;
                                    outbuf[1] = COMMAND_ACTION_EXEINPUT;
                                    *(short*)(outbuf + 2) = strlen(outbuf + 4) + 4 + 1; //+1 for '\0'
                                    send(pchildnodes[j]->socket, outbuf, *(short*)(outbuf + 2), 0);

                                    outbuf[0] = COMMAND_GETNODEINFO;
                                    outbuf[1] = COMMAND_ACTION_EXESTOP;
                                    *(short*)(outbuf + 2) = 4;
                                    send(pchildnodes[j]->socket, outbuf, 4,0);
                                	  
                                	  
                                    //start add sibling node
                                    outbuf[0] = COMMAND_ADDSIBLINGNODE;
                                    outbuf[1] = COMMAND_ACTION_EXESTART;
                                    *(short*)(outbuf + 2) = 4;
                                    send(pchildnodes[j]->socket,outbuf, *(short*)(outbuf + 2), 0);

                                    pchildcommand = (struct doom_dh_command_node *)malloc(sizeof(struct doom_dh_command_node));
                                    pchildcommand->command.no            = commandno++;
                                    pchildcommand->command.parentno      = pclients[index]->lastcommand->command.no;
                                    pchildcommand->command.waitinginput  = 1;
                                    pchildcommand->command.waitingresult = 1;
                                    pchildcommand->prev = pchildcommand->next = NULL;
                                    pchildcommand->command.id = COMMAND_ADDSIBLINGNODE;
                                    add_childcommand(pchildnodes[j], pchildcommand);

                                    pcommand->command.waitingresult++;
                                }
                            }
                        }
                    }
                }
                else //COMMAND_ACTION_EXESTOP
                {
                    outbuf[0] = COMMAND_ADDSIBLINGNODE;
                    outbuf[1] = COMMAND_ACTION_EXESTOP;
                    *(short*)(outbuf + 2) = 4;
                    for(j = 0; j < MAX_NODE_COUNT; j++)
                    {
                        if(pchildnodes[j] != NULL)
                        {
                            send(pchildnodes[j]->socket,outbuf, *(short*)(outbuf + 2), 0);
                            pchildnodes[j]->lastcommand->command.waitinginput = 0;
                        }
                    }
                    pclients[index]->lastcommand->command.waitinginput = 0; //no need to wait for input
                }
            }
            else if(pclients[index]->lastcommand->command.id == COMMAND_CREATEDATABASE ||
            	       pclients[index]->lastcommand->command.id == COMMAND_GETNODEINFO ||
                     pclients[index]->lastcommand->command.id == COMMAND_SETDATABASE ||
                     pclients[index]->lastcommand->command.id == COMMAND_EXECUTEDDL ||
                	   pclients[index]->lastcommand->command.id == COMMAND_EXECUTEDML ||
                	   pclients[index]->lastcommand->command.id == COMMAND_EXECUTEDQL ||
                	   pclients[index]->lastcommand->command.id == COMMAND_IMPORTCSV //import csv into [table]
                	   )
            {
                if(start[1]== COMMAND_ACTION_EXESTOP)
                {
                    pclients[index]->lastcommand->command.waitinginput = 0; //no need to wait for input
                    for(j = 0; j < MAX_NODE_COUNT; j++)
                    {
                        if(pchildnodes[j] != NULL)
                        {
                            send(pchildnodes[j]->socket,start, *(short*)(start+2), 0);
                        }
                    }
                }
                else if(start[1] == COMMAND_ACTION_EXEINPUT)
                {
                    for(j = 0; j < MAX_NODE_COUNT; j++)
                    {
                        if(pchildnodes[j] != NULL)
                        {
                            send(pchildnodes[j]->socket,start, *(short*)(start+2), 0);
                        }
                    }
                }
                else if(start[1] == COMMAND_ACTION_EXEINPUT_ONE)
                {
                    for(j = 0; j < MAX_NODE_COUNT; j++)
                    {
                        if(pchildnodes[j] != NULL && *(short*)(start+4) == pchildnodes[j]->id)
                        {
                            send(pchildnodes[j]->socket,start, *(short*)(start+2), 0);
                        }
                    }
                }
                else
                {
                    printf("error command id=%d, action=%d\n", pclients[index]->lastcommand->command.id, start[1]);
                }
            }
            else
            {
                printf("error request, the server is waiting for input of command is %d, however the input command is %d\n", 
                        pclients[index]->lastcommand->command.id, start[0]);
            }
        }
        else
        {
            printf("server is not ready to handle request!\n");
            dump_command_status(pclients[index]->firstcommand);
        }
        i += *(short*)(start+2);
    }
    if(i < pclients[index]->bufferlength)
    {
        memmove(pclients[index]->buffer, pclients[index]->buffer + i, pclients[index]->bufferlength - i);
    }
    pclients[index]->bufferlength -= i;
    if(DEBUG)
    {
        printf("index = %d, buffer lenth = %d,i =%d\n", index, pclients[index]->bufferlength, i);
    }
}

int main(int argc, char* argv[])  
{
    int    i, j, ret;
    int    ser, cli;
    int    port;
    
    struct sockaddr_in seraddr,cliaddr;  
    fd_set  rdfs;

    if(argc != 3)
    {
        printf("Usage: %s IP PORT\n", argv[0]);
        return 1;
    }
    
    port = atoi(argv[2]);
    
    socklen_t clilen        =sizeof(cliaddr);  
 
    seraddr.sin_family      = AF_INET;  
    //seraddr.sin_addr.s_addr = htonl(INADDR_ANY);  
    seraddr.sin_addr.s_addr = inet_addr(argv[1]);
    seraddr.sin_port        = htons(port);  
    ser    = socket(AF_INET,SOCK_STREAM,0);  
    ret    = bind(ser,(struct sockaddr*)&seraddr,sizeof(seraddr));
    
    ret    = listen(ser,5); 
    if(ret < 0)
    {
        printf("Failed to listen on port %d, errno = %d\n", port, errno);
        return 1;
    }
    printf("Listening on %s:%s...\n", argv[1], argv[2]);
    
    for( i = 0; i < MAX_NODE_COUNT; i++)
    {
        pchildnodes[i] = NULL;
    }
    for( i = 0; i < MAX_CLIENT_COUNT; i++)
    {
        pclients[i] = NULL;
    }

    while(1)  
    {   
        int max_fd = -1;
        FD_ZERO(&rdfs);
        FD_SET(ser, &rdfs);
        max_fd = ser;
        //dump client and node socket status
        if(DEBUG)
        {
            printf("=============Loop started===============\n"	);
            printf("\nclient socket status:\n");
            for(i = 0; i < MAX_CLIENT_COUNT; i++)
            {
                if(pclients[i] != NULL)
                {
                    printf("client[%d](%s:%d)\n", i, pclients[i]->ip, pclients[i]->port);
                    dump_command_status(pclients[i]->firstcommand);
                }
            }
            printf("\nchild node socket status:\n");
            for(j = 0; j < MAX_NODE_COUNT; j++)
            {
                if(pchildnodes[j] != NULL)
                {
                    printf("childnode[%d](%s:%d) ID=%d\n", j, pchildnodes[j]->ip, pchildnodes[j]->serverport, pchildnodes[j]->id);
                    dump_command_status(pchildnodes[j]->firstcommand);
                }
            }
        }
        //dump end
        
        for(i = 0; i < MAX_CLIENT_COUNT; i++)
        {
            if(pclients[i] != NULL)
            {
                FD_SET(pclients[i]->socket, &rdfs);
                max_fd = pclients[i]->socket > max_fd ? pclients[i]->socket : max_fd;
            }
        }
        for(i = 0; i < MAX_NODE_COUNT; i++)
        {
            if(pchildnodes[i] != NULL && pchildnodes[i]->firstcommand != NULL) //only receive the msg from nodes who are waiting for result. 
            {
                FD_SET(pchildnodes[i]->socket, &rdfs);
                max_fd = pchildnodes[i]->socket > max_fd ? pchildnodes[i]->socket : max_fd;
                if(DEBUG)
                {
                    printf("select from childnode[%d]\n", i);
                }
            }
        }
        ret = select(max_fd + 1,&rdfs,NULL, NULL, NULL);
        
        //dump select result
        if(DEBUG)
        {
            printf("\nselect result:\n");
            for(i = 0; i < MAX_CLIENT_COUNT; i++)
            {
                if(pclients[i]!= NULL && FD_ISSET(pclients[i]->socket, &rdfs))
                {
                    printf("client[%d] is selected\n", i);;
                }
            }
            for(j = 0; j < MAX_NODE_COUNT; j++)
            {
                if(pchildnodes[j] != NULL && FD_ISSET(pchildnodes[j]->socket, &rdfs))
                {
                    printf("childnode[%d] is selected\n", j);
                }
            }
        }

        if(ret < 0)
        {  
            printf("select error\n");  
        }
        else if(ret == 0)
        {
            printf("time out\n");
        }
        else
        {
            memset(outbuf, 0, BUFFER_SIZE);
            if(FD_ISSET(ser, &rdfs))  
            {  
                cli=accept(ser,(struct sockaddr*)&cliaddr,&clilen);
                if(cli > 0)
                {
                    for(i = 0; i < MAX_CLIENT_COUNT; i++)
                    {
                        if(pclients[i] == NULL)
                        {
                            pclients[i] = (struct doom_dh_client*)malloc(sizeof(struct doom_dh_client));
                            pclients[i]->socket        = cli;
                            strcpy(pclients[i]->ip,inet_ntoa(cliaddr.sin_addr));
                            pclients[i]->port          = ntohs(cliaddr.sin_port);
                            pclients[i]->firstcommand  = NULL;
                            pclients[i]->lastcommand   = NULL;
                            pclients[i]->bufferlength  = 0;
                            break;
                        }
                    }
                    if(i == MAX_CLIENT_COUNT)
                    {
                        sprintf(outbuf, "ERR: failed to connect to the server, too many connections\n");
                        send(cli,outbuf, strlen(outbuf), 0);
                        close(cli);
                    }
                    else
                    {
                        printf("Connected to %s:%d\n",inet_ntoa(cliaddr.sin_addr),  
                                ntohs(cliaddr.sin_port));
                    
                    }
                }
            }
            else        
            {
                for(i = 0; i < MAX_CLIENT_COUNT; i++)
                {
                    if(pclients[i]!= NULL && FD_ISSET(pclients[i]->socket, &rdfs))
                    {
                        handle_client_request(i);	
                    }
                }

                for(j = 0; j < MAX_NODE_COUNT; j++)
                {
                    if(pchildnodes[j] != NULL && FD_ISSET(pchildnodes[j]->socket, &rdfs))
                    {
                        handle_node_response(j);
                    }
                }
            }
        }
    }
    return 1;  
}