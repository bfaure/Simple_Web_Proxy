/*
 * proxy.c - A Simple Sequential Web proxy
 *
 * Course Name: 14:332:456-Network Centric Programming
 * Assignment 2
 * Student Name:______________________
 * 
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */ 

#include "csapp.h"

//#include "stdio.h" // for printf
//#include "string.h" // for strlen, etc.
//#include "stdlib.h" 
//#include "ctype.h" // for tolower


#include "time.h"

/*
 * Function prototypes
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);

// file to save debugging info to, can be accessed from any function in this file 
FILE *debug_log;

// file to log each request (as per instructions)
FILE *proxy_log;

// function to log the timestamp to the debug log file
void log_time();

// function to write to the debug log file
void printl(const char *input_str, const char *line_ending);

// function to initialize the proxy.log file, carries over data if already written to
void init_proxy_log();

// function to initialize the debug_log.txt file
void init_debug_log(int argc, char ** argv);

// parses the port number from the command line arguments
int parse_listening_port_num(char ** argv);

// set to 1 while a request is being processed to prevent concurrency problems
int PROCESSING_REQUEST = 0;

// set to 1 while a request is being written to the proxy.log file
int ALREADY_LOGGING = 0;

typedef struct request_data
{
    char req_all[1024]; // unparsed, entire contents of request
    char req_command[10]; // GET, POST, etc.
    char req_host_domain[100]; // www.yahoo.com, etc.
    char req_host_long[512]; // http://www.yahoo.com/, etc.
    char req_host_port[10]; // Host port, if specified
    int  req_host_port_num; // Host port, if specified
    char req_protocol[100]; // HTTP/1.1, etc.

    char req_connection_type[256]; // keep-alive, etc.
    char req_user_agent[512]; // Mozilla/5.0, etc.
    char req_accept[512]; // accepted return file types
    char req_accept_language[256]; // en, en-US, etc.
    char req_host_path[512]; // /text/index.html from http://www.cnn.com:80/test/index.html

    // to hold the string we will use as the overall request to the server,
    // this string will be filled after we have parsed out everything else
    // from the browser request. Emulates the 'Typical Request Message' format
    // on page 15 of the lec7_8_client_http.pdf slides on Sakai
    char constructed_request[1024];
} request_data;

request_data req_data;

// check if the req_host_long contains a port specification, if so 
// parse it out and set req_host_port appropriately, otherwise, set
// req_host_port to the default of 80
void check_for_host_port();

// parses through received request and fills in the above variables
char* parse_request(char *buffer);

// called after parse_request
int fulfill_request(int client_socket, struct sockaddr_in client_addr);

// count the number of occurrences of target in input
int get_match_count(const char *input, const char *target);

// remove trailing to_remove from the end of source (if exists)
void strip_trailing_char(char *source, const char to_remove);

// apply any changes we need to data before signaling to DNS
void fix_data();

// get the path (after the domain name in the URL)
void get_host_path();

// logs a request to the proxy.log file
void log_request( struct sockaddr_in *sockaddr,  char *uri,  int size);

/* 
 * main - Main routine for the proxy program 
 */
int main(int argc, char **argv)
{
    //init_debug_log(argc,argv); // initialize the debug.log file
    init_proxy_log(); // initialize the debugging log file
    
    /* Check arguments */
    if (argc != 2) 
    {
	    fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        Fclose(debug_log);
	    exit(0);
    }
    int listening_port = parse_listening_port_num(argv);

    printf("> Opening connection to listening port... \n");
    int listening_socket = Open_listenfd(listening_port);
    printf("> Connection established on rc = %d\n",listening_socket);
    
    printf("> Listening for requests...\n");



    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);

        // wait for a connection from a client at listening socket
        int client_socket = Accept(listening_socket, (struct sockaddr*)&client_addr, &addrlen);
        //printf("> Found client at socket %d\n",client_socket);

        if (Fork()==0)
        {
            //printf("> FORKED a new process\n");

            char buffer[1024]; // to read the socket data into
            int n = read(client_socket,buffer,1023); // read socket into buffer string

            if (n<0)
            {
                printf("> Error reading from socket \n");
            }

            //printf("> Socket data...\n");
            //printf("%s\n",buffer);
            //fflush(stdout);
            
            char *uri = parse_request(buffer);
            int resp_size = fulfill_request(client_socket,client_addr);

            log_request((struct sockaddr_in*)&client_addr,uri,resp_size);
            close(client_socket);
            exit(0);
        }
        close(client_socket);
    }
    exit(0);
    Fclose(proxy_log);
}

void log_request( struct sockaddr_in *sockaddr,  char *uri,  int size)
{
    // busy wait until we can get a handle on the proxy.log file
    while (ALREADY_LOGGING==1){  Sleep(1);  }

    ALREADY_LOGGING = 1;
    char logstring[1024];
    format_log_entry((char *)&logstring,sockaddr,uri,size);

    //char fixed_logstring[1024];
    //sprintf(fixed_logstring,"%s",logstring);

    //sprintf(logstring,"[%s]",logstring);
    printf("> %s\n",logstring);
    fprintf(proxy_log,"%s",logstring);
    fprintf(proxy_log,"\n");
    ALREADY_LOGGING = 0;
    //fprintf(proxy_log,"TEST\n");
}


/*
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
		      char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /* 
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;


    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s", time_str, a, b, c, d, uri);
}

// function to write the current time out to the log file
void log_time()
{
    time_t rawtime;
    struct tm *timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    fprintf(debug_log, "[%d %d %d %d:%d:%d]> ",timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    printf("[%d %d %d %d:%d:%d]> ",timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
}

// function to write to the log file, writes out the timestamp before input string
// input_str: string to write to log
// line_ending: [0,1,2,3] = ['\n','\r','',' ']
void printl(const char *input_str, const char *line_ending)
{
    // If just a line feed input for line ending and no input, write out line ending and return
    // without posting a timestamp
    if (strcmp(input_str,"")==0 && strcmp(line_ending,"\n")==0)
    {
        fprintf(debug_log, "\n");
        printf("\n");
        return;
    }
    // log the timestamp if needed
    if (strcmp(line_ending,"\r")==0 || strcmp(line_ending,"\n")==0 || strcmp(line_ending,"")==0)
    {  
        log_time(); // add timestamp
    } 

    // write out information
    fprintf(debug_log, "%s", input_str); // write out message
    fprintf(debug_log, "%s", line_ending); // write out line ending
    
    // print out to command window (if one exists)
    printf("%s", input_str);
    printf("%s", line_ending);
}

// initializes the proxy.log file, if one already exists, it carries over that data to the new instance
void init_proxy_log()
{
    //printl("Initializing proxy.log file...","\n");
    // try to read in prior data
    //FILE *prior_log;
    //prior_log = Fopen("proxy.log","r");
    proxy_log = Fopen("proxy.log","w");
    return;
    /*
    char *buffer = 0;
    long length = -1; 

    if (prior_log)
    {
       // printl("Found prior proxy.log file.","\n");
        // proxy.log exits, read in prior data
        fseek(prior_log,0, SEEK_END);
        length = ftell(prior_log);
        fseek(prior_log,0,SEEK_SET);
        buffer = malloc(length + 1); // make room to zero-terminate string
        if (buffer)
        {
            Fread(buffer,1,length,prior_log);
        }
        buffer[length] = '\0'; // zero-terminate string
        Fclose(prior_log); // close old proxy.log file
    }

    // open new instance of proxy.log (overwriting prior, if one)
    proxy_log = Fopen("proxy.log","w");

    if (length != -1)
    {
        //printl("Copying data from prior proxy.log instance...","\n");
        // if we read in data from a prior proxy.log file, write it back out
        // to new copy of proxy.log...
        fprintf(proxy_log, "%s", buffer);
    }
    //printl("proxy.log initialized.","\n");
    */
}

// initializes the debugging log
void init_debug_log(int argc, char **argv)
{
    debug_log = Fopen("[proxy_c]-debug_log.txt","w"); // initialize debugging log
    return;
    //printl("Initializing proxy.c...","\n");
    if (argc==1){  printl("No arguments passed.","\n");  }
    else
    {
        printl("Arguments: ",""); 
        for (int i=1; i<argc; i++)
        {  
            printl(argv[i]," ");  
        }
        printl("","\n");
    }
}

// parses out the port number and returns it as an integer
int parse_listening_port_num(char ** argv)
{
    char * port_str = argv[1];
    int port_num = atoi(port_str);
    printf("> Listening port: %d\n",port_num);
    return port_num;
}


int fulfill_request(int client_socket, struct sockaddr_in client_addr)
{
    int remotefd = Open_clientfd(req_data.req_host_domain,req_data.req_host_port_num);
    //printf("> remotefd = %d\n",remotefd);

    
    // If a GET request...
    if (strcmp(req_data.req_command,"GET")==0)
    {
        //printf("> Processing GET request...\n");
        
        char buffer[1024];
        sprintf(buffer, "GET %s %s\r\n", req_data.req_host_path, req_data.req_protocol);
        //printf("> Writing first line to buffer...\n");
        //Rio_writen(remotefd,buffer,strlen(buffer));
        send(remotefd,buffer,strlen(buffer),0); // send first line to remote socket
        //printf("> Wrote first line to buffer.\n");

        sprintf(buffer, "Host: %s\r\n", req_data.req_host_domain);
        //printf("> Writing second line to buffer...\n");
        //Rio_writen(remotefd,buffer,strlen(buffer));
        send(remotefd,buffer,strlen(buffer),0);
        //printf("> Wrote second line to buffer.\n");

        sprintf(buffer, "\r\n"); 
        //printf("> Writing final empty line to buffer...\n");
        send(remotefd,buffer,strlen(buffer),0);
        //printf("> Wrote final empty line to buffer.\n");

        //printf("> Checking remote socket for response...\n");

        ssize_t n;
        int total_size = 0;
        while ((n = recv(remotefd,buffer,1024,0)) > 0)
        {
            total_size += n;
            //printf("> Received %s from remote socket, sending to client socket\n",buffer);
            send(client_socket,buffer,n,0);
        }
        //printf("> Finished reading from remote socket\n");
        close(remotefd);
        //printf("> Closed remotefd\n");
        return total_size;
    }

    // If a POST request...
    if (strcmp(req_data.req_command,"POST")==0)
    {
        //printf("> Processing POST request...\n");
        
        char buffer[1024];
        sprintf(buffer, "POST %s %s\r\n", req_data.req_host_path, req_data.req_protocol);
        //printf("> Writing first line to buffer...\n");
        //Rio_writen(remotefd,buffer,strlen(buffer));
        send(remotefd,buffer,strlen(buffer),0); // send first line to remote socket
        //printf("> Wrote first line to buffer.\n");

        sprintf(buffer, "Host: %s\r\n", req_data.req_host_domain);
        //printf("> Writing second line to buffer...\n");
        //Rio_writen(remotefd,buffer,strlen(buffer));
        send(remotefd,buffer,strlen(buffer),0);
        //printf("> Wrote second line to buffer.\n");

        sprintf(buffer, "\r\n"); 
        //printf("> Writing final empty line to buffer...\n");
        send(remotefd,buffer,strlen(buffer),0);
        //printf("> Wrote final empty line to buffer.\n");

        //printf("> Checking remote socket for response...\n");

        ssize_t n;
        int total_size = 0;
        while ((n = recv(remotefd,buffer,1024,0)) > 0)
        {
            total_size += n;
            //printf("> Received %s from remote socket, sending to client socket\n",buffer);
            send(client_socket,buffer,n,0);
        }
        //printf("> Finished reading from remote socket\n");

        close(remotefd);
        //printf("> Closed remotefd\n");
        return total_size;
    }
    return -1;
}

char* parse_request(char *buffer)
{
    int request_length = strlen(buffer);
    //printf("> Request length: %d\n",request_length);

    char buffer_copy[request_length+1];
    strcpy(buffer_copy,buffer);

    //printf("> Iterating over lines of request...\n");

    // splitting the buffer on '\n' endline characters

    char *end_str;
    char *line = strtok_r(buffer_copy,"\n",&end_str);

    int line_index = 0;
    while (line != NULL)
    {

        char *end_token;
        char *word = strtok_r(line," ",&end_token);

        int word_index = 0;
        while (word != NULL)
        {
            // Iterate over each word in line
            if (line_index==0 && word_index==0)
            {
                //printf("> Request: %s\n",word);
                strcpy(req_data.req_command,word);
            }
            if (line_index==0 && word_index==1)
            {
                //printf("> Host (long): %s\n",word);
                strcpy(req_data.req_host_long,word);
            }
            if (line_index==0 && word_index==2)
            {
                //printf("> Protocol: %s\n",word);
                strcpy(req_data.req_protocol,word);
            }
            if (line_index==1 && word_index==1)
            {
                //printf("> Host (short): %s\n",word);
                strcpy(req_data.req_host_domain,word);
            }

            word = strtok_r(NULL," ",&end_token);
            word_index++;
        }
        line = strtok_r(NULL,"\n",&end_str);
        line_index++;
    }
    check_for_host_port(); // check the req_host_long string for a port specification
    fix_data(); // apply any changes to the data we need before calling to DNS

    //char uri[1024];
    //strcpy(uri,req_data.req_host_long);
    //strip_trailing_char((char*)&uri,'\r');
    return req_data.req_host_long;
}

// apply any changes to the data we need before continuing
void fix_data()
{
    strip_trailing_char(req_data.req_host_domain,'\r'); // remove '\r' from domain name
    strip_trailing_char(req_data.req_protocol,'\r'); // remove '\r' from the protocol type
    get_host_path(); // get the path specified in the URL (after the domain name)
}

// get the host path from the URL
void get_host_path()
{
    //printf("> Getting the host path...\n");

    strcpy(req_data.req_host_path,""); // empty the container

    int past_http = 0; // if we are past the 'http://' portion of URL
    int inside_port = 0; // if we are iterating over port in URL
    char buffer[1024]; // fills up until we have found 'http://' portion of URL
    int buffer_index = 0; // index to write into 'buffer'
    int path_index = 0; // index to write into 'req_data.req_host_path'
    int recording_path = 0; // if we are past the domain and port portion of URL

    for(int i=0; i<strlen(req_data.req_host_long); i++)
    {
        // initial condition, if we haven't gotten past the http:// portion of the string yet
        if (past_http==0)
        {
            buffer[buffer_index] = req_data.req_host_long[i];
            buffer_index++;    
        }

        // if we have already started recording the path continue recording it
        if (recording_path==1)
        {
            // don't want to record endline or carriage return in the path name
            if ( req_data.req_host_long[i]=='\r' || req_data.req_host_long[i]=='\n'){  continue;  }

            req_data.req_host_path[path_index] = req_data.req_host_long[i];
            path_index++;
            continue;
        }

        // if we are already past the http:// portion of the URL and we are
        // not inside a port number specification
        if (past_http==1 && inside_port==0)
        {
            // if we have encountered a '/' after the http:// portion 
            // of the URL then we should start recording this as the 
            // path (at the end of the URL) 
            if ( req_data.req_host_long[i] == '/' )
            {
                recording_path = 1;
                req_data.req_host_path[0] = req_data.req_host_long[i];
                path_index++;
                continue;
            }

            // if we have encountered a ':' after the http:// portion 
            // of the URL then we shouldn't include this in the path
            // because it's referring to the host port specification
            if ( req_data.req_host_long[i] == ':' )
            {
                inside_port = 1; // denote that we are inside of a port number
                continue;
            }
        }

        // if we have been inside of a port number specification, check if over
        if (inside_port==1)
        {
            // check if the port number is done
            if (req_data.req_host_long[i] == '/')
            {
                recording_path = 1;
                inside_port = 0;
                req_data.req_host_path[0] = req_data.req_host_long[i];
                path_index++;
                continue;
            }
        }

        if (strcmp(buffer,"http://")==0)
        {
            past_http = 1;
            continue;
        }
    }
    //printf("> Parsed %s as the path name\n",req_data.req_host_path);
}

// remove trailing '\r' from data items parsed
void strip_trailing_char(char *source, const char to_remove)
{
    int src_len = strlen(source);

    // iterate over characters of source in reverse
    for (int i=src_len-1; i>0; i--)
    {
        // if the end null-terminated character of string
        if (source[i]=='\0')
        {  
            continue;  
        }
        // if the character we want to remove set equal to null-terminating character
        if (source[i]==to_remove)
        {  
            //printf("> Stripped to_remove from source\n");
            source[i]='\0';  
            continue;
        }
        // else, break because we are done
        //printf("> Initial source length = %d, after stripping = %d\n",src_len,strlen(source));
        return;
    }
}

// check if req_host_long contains a port specification, if so remove it from
// that string and place it in req_host_port, if not, set req_host_port equal to 80
void check_for_host_port()
{
    int num_colons = get_match_count(req_data.req_host_long,":");
    //printf("> Found %d colons in req_host_long\n",num_colons);

    if ( num_colons==1 )
    {
        //printf("> Using default port number of 80\n");
        req_data.req_host_port_num = 80;
        strcpy(req_data.req_host_port,"80");
        return;
    }
    if ( num_colons==2 )
    {
        // need to parse specified port
        char *p = strrchr(req_data.req_host_long,':');
        strcpy(req_data.req_host_port,"");
        char *char_ptr;
        int i=0;
        for (char_ptr = p; *char_ptr != '\0'; char_ptr++)
        {

            if (*char_ptr == '/'){  break;  }
            if (*char_ptr == ' '){  break;  }

            req_data.req_host_port[i] = *char_ptr;
            i++;
        }
        //printf("> Using %s for host port number\n", req_data.req_host_port);
        return;
    }
    //printf("> Undefined number of colons in req_host_long\n");
    req_data.req_host_port_num = 80;
    strcpy(req_data.req_host_port,"80");
}

// count the number of occurrences of target in input
int get_match_count(const char *input, const char *target)
{
    int count = 0;
    const char *tmp = input;
    while ( tmp = strstr(tmp, target) )
    {
        count++;
        tmp++;
    }
    return count;
}