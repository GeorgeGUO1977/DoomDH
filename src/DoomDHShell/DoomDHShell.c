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
#include<pthread.h> 

#include "../Command.h"

#define DEBUG            0
//#define BUFFER_SIZE      32760
#define BUFFER_SIZE      2048

char      inbuf[BUFFER_SIZE];
int       inbuflength;
char      outbuf[BUFFER_SIZE];  
char      command[BUFFER_SIZE];

int       nodeids[1024];
int       nodecount;


pthread_t ntid; 

int  command_status; // >0--executing, 0--idle
char ip[16];
int  port;
int  ser;

int split_string(char* buffer, char** field)
{
    int m = 0;
    int i = 0;
    
    //skip space;
    while(buffer[i] == '\t' || buffer[i] == ' ')
    {
        i++;
    }
    if(buffer[i] == '\0')
    {
        return 0;
    }
    
    field[m] = buffer + i;
    m++;
    while(buffer[i] != '\0')
    {
        if(buffer[i] == ' ')
        {
            buffer[i] = '\0';
            field[m] = buffer + i + 1;
            m++;
        }
        i++;
        //skip space
		    while(buffer[i] == '\t' || buffer[i] == ' ')
		    {
		        i++;
		    }
    }
    return m;
}

void *handle_response(void *arg)
{
    fd_set  rdfs;
    int ser;
    int t, ret;
    int max_fd;
    
    ser = *(int*)arg;
	  memset(inbuf, 0, BUFFER_SIZE);
    inbuflength = 0;
    while(1)
    {
        FD_ZERO(&rdfs);
        //FD_SET(0, &rdfs);
        //FD_SET(1, &rdfs);
        FD_SET(ser, &rdfs);
        max_fd = ser;
        ret = select(max_fd + 1,&rdfs,NULL, NULL, NULL);
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
            if(FD_ISSET(ser, &rdfs)) //from server
            {
                ret = recv(ser,inbuf + inbuflength,BUFFER_SIZE,0);
                if(ret == 0)
                {
                    printf("Connection closed!\n");
                    close(ser);
                    ser = -1;
                }
                else if(ret < 0)
                {
                    printf("Error receive data \n");
                }
                else
                {
                    inbuflength += ret;
                    while(inbuflength > 4)
                    {
                        t = *(short*)(inbuf + 2);
                        if(t > inbuflength)
                        {
                            break;
                        }
                        printf("%d,%d: %s\n", inbuf[0], inbuf[1], inbuf + 4);
                        memmove(inbuf, inbuf + t, inbuflength - t);
                        inbuflength -= t;
                        if(inbuf[1] == COMMAND_ACTION_RETSTOP)
                        {
                            command_status --;
                        }
                        if(inbuf[0] == COMMAND_GETNODEINFO && inbuf[1] == COMMAND_ACTION_RETOUT_CLIENT)
                        {
                            char *p = inbuf + 4; // (ip:port)ID:XXX
                            p = strstr(p, ":");
                            p = strstr(p, ":");
                            nodeids[nodecount] = atoi(p + 1); 
                            nodecount++;
                        }
                    }
                }
            }
        }
    }
    return (void*)0;
}

int get_node_info()
{
    command_status++;

    outbuf[0] = COMMAND_GETNODEINFO;
    outbuf[1] = COMMAND_ACTION_EXESTART;
    *(short*)(outbuf + 2) = 4;
    send(ser, outbuf, 4,0);

    sprintf(outbuf + 4, "ID");
    outbuf[0] = COMMAND_GETNODEINFO;
    outbuf[1] = COMMAND_ACTION_EXEINPUT;
     *(short*)(outbuf + 2) = strlen(outbuf + 4) + 4 + 1; //+1 for '\0'
    send(ser, outbuf, *(short*)(outbuf + 2), 0);

    outbuf[0] = COMMAND_GETNODEINFO;
    outbuf[1] = COMMAND_ACTION_EXESTOP;
    *(short*)(outbuf + 2) = 4;
    send(ser, outbuf, 4,0);
    
    while(command_status > 0)
    {
        usleep(1000);
    }
}

int connect_to_server(char *ip, int port)
{
    struct sockaddr_in seraddr;  
    seraddr.sin_family =AF_INET;  
    seraddr.sin_port   = htons(port);
    seraddr.sin_addr.s_addr=inet_addr(ip);
    ser = socket(AF_INET,SOCK_STREAM,0);  
    if(connect(ser,(struct sockaddr*)&seraddr,sizeof(seraddr)) < 0)
    {
        printf("error:Failed to connect to server\n");
        ser = -1;
    }
    return ser;
}

int import_csv(char *csvfile, char* table)
{
    int i, j, ret;
    int flen, readlen;
    FILE *f;
    
    if(!get_node_info())
    {
        return 0;
    }
    
    sprintf(outbuf + 4, "%s", table);
    outbuf[0] = COMMAND_IMPORTCSV;
    outbuf[1] = COMMAND_ACTION_EXESTART;
    *(short*)(outbuf + 2) = strlen(outbuf + 4) + 4 + 1; //+1 for '\0'
    send(ser, outbuf, *(short*)(outbuf + 2), 0);

    f = fopen(csvfile, "r");
    
    outbuf[0] = COMMAND_IMPORTCSV;
    outbuf[1] = COMMAND_ACTION_EXEINPUT_ONE;
    
    if(f != NULL)
    {
        fseek(f, 0, SEEK_END);
        flen = ftell(f);
        fseek(f, 0, SEEK_SET);
        readlen = 0;
        j = 0;
        while(!feof(f))
        {
            memset(outbuf + 2, 0, BUFFER_SIZE - 2);
            ret = fread(outbuf + 6, 1, BUFFER_SIZE - 6, f);
            i = 0;
            readlen += ret;
            if(readlen < flen)
            {
                while(outbuf[6 + ret] != '\n' && outbuf[6 + ret] != '\r')
                {
                    ret --;
                    i++;
                }
                outbuf[6 + ret] = '\0';
            }
            else
            {
                outbuf[6 + ret + 1] = '\0';
            }
            *(short*)(outbuf + 2) = ret + 4 + 2 + 1;
            *(short*)(outbuf + 4) = nodeids[(j % nodecount)];
            send(ser, outbuf, *(short*)(outbuf + 2),0); 
            if(readlen == flen)
            {
                break;
            }
            fseek(f, -i,SEEK_CUR);
            readlen -= i;
            j++;
        }
        fclose(f);
	  }
	  	  
    outbuf[0] = COMMAND_IMPORTCSV;
    outbuf[1] = COMMAND_ACTION_EXESTOP;
    *(short*)(outbuf + 2) = 4;
    send(ser, outbuf, 4,0);
}
int handle_command(char *buffer)
{
    int i, m;
    char *param[256];
    if(strncasecmp(buffer, ".connect ", strlen(".connect ")) == 0)
    {
        buffer += strlen(".connect ");
        m = split_string(buffer, param);
        if(m != 2)
        {
            printf("Please use commmand \".connect ip port\" to connect to the server!\n");
            return 0;
        }
        printf("Connecting to %s:%s ... \n", param[0], param[1]);
        if(ser != -1)
        {
            close(ser);
            ser = -1;
        }
        ser = connect_to_server(ip, port);
        if(ser != -1)
        {
           strcpy(ip, param[0]);
           port = atoi(param[i]);
        
        }
        
    }
    else if(strncasecmp(buffer, ".addchildnode ", strlen(".addchildnode ")) == 0)
    {
        buffer += strlen(".addchildnode ");
        m = split_string(buffer, param);
        if(m != 3)
        {
            printf("Please use commmand \".addchildnode ip serverport nodeport\" to add a child node to the server!\n");
            return 0;
        }
    
        command_status++;

        outbuf[0] = COMMAND_ADDCHILDNODE;
        outbuf[1] = COMMAND_ACTION_EXESTART;
        *(short*)(outbuf + 2) = 4;
        send(ser, outbuf, 4,0);

        sprintf(outbuf + 4, "%s %s %s", param[0], param[1], param[2]);
        outbuf[0] = COMMAND_ADDCHILDNODE;
        outbuf[1] = COMMAND_ACTION_EXEINPUT;
        *(short*)(outbuf + 2) = strlen(outbuf + 4) + 4 + 1; //+1 for '\0'
        send(ser, outbuf, *(short*)(outbuf + 2), 0);

        outbuf[0] = COMMAND_ADDCHILDNODE;
        outbuf[1] = COMMAND_ACTION_EXESTOP;
        *(short*)(outbuf + 2) = 4;
        send(ser, outbuf, 4,0);
    }
    else if(strncasecmp(buffer, ".createdatabase ", strlen(".createdatabase ")) == 0)
    {
        buffer += strlen(".createdatabase ");
        m = split_string(buffer, param);
        if(m != 1)
        {
            printf("Please use commmand \".createdatabase name\" to create a database!\n");
            return 0;
        }
        
        command_status++;
        
        outbuf[0] = COMMAND_CREATEDATABASE;
        outbuf[1] = COMMAND_ACTION_EXESTART;
        *(short*)(outbuf + 2) = 4;
        send(ser, outbuf, 4,0);

        sprintf(outbuf + 4, "%s", param[0]);
        outbuf[0] = COMMAND_CREATEDATABASE;
        outbuf[1] = COMMAND_ACTION_EXEINPUT;
         *(short*)(outbuf + 2) = strlen(outbuf + 4) + 4 + 1; //+1 for '\0'
        send(ser, outbuf, *(short*)(outbuf + 2), 0);

        outbuf[0] = COMMAND_CREATEDATABASE;
        outbuf[1] = COMMAND_ACTION_EXESTOP;
        *(short*)(outbuf + 2) = 4;
        send(ser, outbuf, 4,0);

    }
    else if(strncasecmp(buffer, ".listdatabase ", strlen(".listdatabase ")) == 0)
    {
        buffer += strlen(".listdatabase ");
        m = split_string(buffer, param);
        if(m != 0)
        {
            printf("Please use commmand \".list database\" to get database list!\n");
            return 0;
        }

        command_status++;

        outbuf[0] = COMMAND_LISTDATABASE;
        outbuf[1] = COMMAND_ACTION_EXESTART;
        *(short*)(outbuf + 2) = 4;
        send(ser, outbuf, 4,0);

        outbuf[0] = COMMAND_LISTDATABASE;
        outbuf[1] = COMMAND_ACTION_EXEINPUT;
         *(short*)(outbuf + 2) = 4;
        send(ser, outbuf, 4, 0);

        outbuf[0] = COMMAND_LISTDATABASE;
        outbuf[1] = COMMAND_ACTION_EXESTOP;
        *(short*)(outbuf + 2) = 4;
        send(ser, outbuf, 4,0);
        
    }
    else if(strncasecmp(buffer, ".setdatabase ", strlen(".setdatabase ")) == 0)
    {
        buffer += strlen(".setdatabase ");
        m = split_string(buffer, param);
        if(m != 1)
        {
            printf("Please use commmand \".setdatabase name\" to set the current database!\n");
            return 0;
        }
        
        command_status++;
        
        outbuf[0] = COMMAND_SETDATABASE;
        outbuf[1] = COMMAND_ACTION_EXESTART;
        *(short*)(outbuf + 2) = 4;
        send(ser, outbuf, 4,0);

        sprintf(outbuf + 4, "%s", param[0]);
        outbuf[0] = COMMAND_SETDATABASE;
        outbuf[1] = COMMAND_ACTION_EXEINPUT;
         *(short*)(outbuf + 2) = strlen(outbuf + 4) + 4 + 1; //+1 for '\0'
        send(ser, outbuf, *(short*)(outbuf + 2), 0);

        outbuf[0] = COMMAND_SETDATABASE;
        outbuf[1] = COMMAND_ACTION_EXESTOP;
        *(short*)(outbuf + 2) = 4;
        send(ser, outbuf, 4,0);
    }
    else if(strncasecmp(buffer, ".importcsv ", strlen(".importcsv ")) == 0)
    {
        buffer += strlen(".importcsv ");
        m = split_string(buffer, param);
        if(m != 2)
        {
            printf("Please use commmand \".importcsv csvfile table\" to import a csv file to database!\n");
            return 0;
        }
        
    }
    else
    {
        printf("Wrong command %s\n", buffer);
    }
    return 1;
}

int handle_sql(char * buffer)
{
    char command;
    if(strncasecmp(buffer, "SELECT", strlen("SELECT")) == 0)
    {
        command = COMMAND_EXECUTEDQL;
    }
    else if(strncasecmp(buffer, "CREATE", strlen("CREATE")) == 0 ||
    	       strncasecmp(buffer, "CREATE", strlen("ALTER")) == 0)
    {
        command = COMMAND_EXECUTEDDL;
    }
    else if(strncasecmp(buffer, "INSERT", strlen("INSERT")) == 0 ||
    	       strncasecmp(buffer, "UPDATE", strlen("UPDATE")) == 0)
    {
        command = COMMAND_EXECUTEDML;
    }
    else
    {
        printf("Error SQL sentence!");
    }
    
    command_status++;
    
    outbuf[0] = command;
    outbuf[1] = COMMAND_ACTION_EXESTART;
    *(short*)(outbuf + 2) = 4;
    send(ser, outbuf, 4,0);

    sprintf(outbuf + 4, "%s", buffer);
    outbuf[0] = command;
    outbuf[1] = COMMAND_ACTION_EXEINPUT;
     *(short*)(outbuf + 2) = strlen(outbuf + 4) + 4 + 1; //+1 for '\0'
    send(ser, outbuf, *(short*)(outbuf + 2), 0);

    outbuf[0] = command;
    outbuf[1] = COMMAND_ACTION_EXESTOP;
    *(short*)(outbuf + 2) = 4;
    send(ser, outbuf, 4,0);
    
    return 1;
}

int main(int argc, char* argv[])  
{
    size_t len;
    ssize_t ret;
    char* line = NULL;
    
    command[0] = '\0';
    command_status = 0;
    ser            = -1;
    if(argc != 1 && argc != 3)
    {
        printf("Usage: %s IP Port\n", argv[0]);
        printf("       %s\n", argv[0]);
    }
    
    if(argc == 3)
    {
        strncpy(ip, argv[1], 15);
        port = atoi(argv[2]);
        ser  = connect_to_server(ip, port);
    }
    
    int temp;
    if((temp=pthread_create(&ntid,NULL,handle_response,(void*)&ser))!= 0)  
    {  
        printf("can't create thread: %s\n",strerror(temp));  
        return 1;  
    }  

    if(ser == -1)
    {
        printf("DoomDH(Not connected)>");
    }
    else
    {
        get_node_info();
        printf("DoomDH(%s:%d)(%d nodes)>", ip, port, nodecount);
    }
    while ((ret = getline(&line, &len, stdin)) != -1) 
    {
        //remove \r \n at the end
        line[ret - 1] = '\0';
        ret --;
        strcat(command, line);
        if(strlen(command) == 0)
        {
            //do nothing
        }
        else if(command[0] == '.' || command[strlen(command)-1] == ';') // command is singal line, SQL should end as ';'
        {
            if(strcasecmp(line, ".quit") == 0 || strcasecmp(line, ".q") == 0)
            {
                break;
            }
            if(ser == -1)//not connected
            {
                if(strncasecmp(line, ".connect", strlen(".connect")) != 0)
                {
                    printf("Please connect to server first!\n.connect ip port\n");
                }
                else
                {
                    handle_command(line);
                }
            }
            else if(ret > 0 && line[0] == '.')
            {
                handle_command(line);
                while(command_status > 0)
                {
                    usleep(1000);
                }
            }
            else if(ret > 0)
            {
                handle_sql(line);
                while(command_status > 0)
                {
                    usleep(1000);
                }
            }
        }
        else
        {
            command[strlen(command)-1] = '\0';
        }
        if(ser == -1)
        {
            printf("DoomDH(Not connected)>");
        }
        else
        {
            get_node_info();
            printf("DoomDH(%s:%d)(%d nodes)>", ip, port, nodecount);
        }
    }  
    close(ser);    
}