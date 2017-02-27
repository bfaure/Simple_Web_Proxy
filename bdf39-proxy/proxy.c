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
#include "time.h"

#define BUFFER_PAGE_WIDTH 1024
#define BUFFER_PAGE_COUNT 1024

FILE *response_log;
FILE *request_log;

/*
 * Function prototypes
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);

// if 1, data will be written to debug.log file
int debugging = 1;

// file to save debugging info to, can be accessed from any function in this file 
FILE *debug_log;

// file to log each request (as per instructions)
FILE *proxy_log;

// function to initialize the proxy.log file, carries over data if already written to
void init_proxy_log();

// function to initialize the debug_log.txt file
void init_debug_log();

// parses the port number from the command line arguments
int parse_listening_port_num(char ** argv);

// set to 1 while a request is being processed to prevent concurrency problems
int PROCESSING_REQUEST = 0;

// set to 1 while a request is being written to the proxy.log file
int ALREADY_LOGGING = 0;

// set to 1 while debuggin data is being written
int *ALREADY_LOGGING_DEBUG_INFO;

// data structure to hold all essential information pertaining to a request
typedef struct request_data
{
    char req_all[1024]; // unparsed, entire contents of request

    // all data in request past the POST or GET 
    char req_after_host[24][1024]; // broken by line (max 24)
    int num_lines_after_host; // track number of lines used

    // certain portions of the request, parsed out to help with forwarding
    char req_command[10]; // GET, POST, etc.
    char req_host_domain[100]; // www.yahoo.com, etc.
    char req_host_long[1024]; // http://www.yahoo.com/, etc.
    char req_host_port[10]; // Host port, if specified
    int  req_host_port_num; // Host port, if specified
    char req_protocol[100]; // HTTP/1.1, etc.
    char req_host_path[512]; // /text/index.html from http://www.cnn.com:80/test/index.html

    char full_response[BUFFER_PAGE_COUNT][BUFFER_PAGE_WIDTH]; // line-by-line response from server
    int num_lines_response; // to hold the number of response lines

    char response_code[100]; // to hold the return code
    char sent_time[100]; // time when the request was sent to remote server
    char server_responded_time[100]; // time when the server responded

    int request_blocked;

} request_data;

// check if the req_host_long contains a port specification, if so 
// parse it out and set req_host_port appropriately, otherwise, set
// req_host_port to the default of 80
void check_for_host_port(struct request_data *req_data);

// parses through received request and fills in the above variables
char* parse_request(struct request_data *req_data, char *buffer);

// called after parse_request
int fulfill_request(struct request_data *req_data, int client_socket, struct sockaddr_in client_addr);

// count the number of occurrences of target in input
int get_match_count(const char *input, const char *target);

// remove trailing to_remove from the end of source (if exists)
void strip_trailing_char(char *source, const char to_remove);

// apply any changes we need to data before signaling to DNS
void fix_data(struct request_data *req_data);

// get the path (after the domain name in the URL)
void get_host_path_and_port(struct request_data *req_data);

// logs a request to the proxy.log file
void log_request( struct sockaddr_in *sockaddr,  char *uri,  int size);

// log debugging information
void log_information(const char *buffer, const struct request_data *req_data, const int thread_id);


void print_spinner(int *spin_index);

/* 
 * main - Main routine for the proxy program 
 */
int main(int argc, char **argv)
{

    ALREADY_LOGGING_DEBUG_INFO = mmap(NULL,sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    *ALREADY_LOGGING_DEBUG_INFO = 0;

    response_log = Fopen("response.log","w"); // initialize debugging log
    request_log = Fopen("request.log","w"); // initialize debugging log

    init_debug_log(argc,argv); // initialize the debug.log file
    init_proxy_log(); // initialize the proxy.log file
    
    /* Check arguments */
    if (argc != 2) 
    {
	    fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        Fclose(debug_log);
	    exit(0);
    }

    printf("\n===================================================================================\n");
    int listening_port = parse_listening_port_num(argv);

    printf("idx                       req. snippet                    req.size       response\n");
    printf("___________________________________________________________________________________\n");

    //printf("> Opening connection to listening port... \n");
    int listening_socket = Open_listenfd(listening_port);
    
    //printf("> Listening for requests...\n");
    int *wait_a_sec = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    *wait_a_sec = 0;

    int *thread_ct = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    *thread_ct = 0;

    int *threads_open = mmap(NULL, sizeof(int),PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    *threads_open = -1;

    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);

    int *spin_index = mmap(NULL, sizeof(int),PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    *spin_index = 0;


    while (1)
    {
        // wait for a connection from a client at listening socket
        int client_socket = Accept(listening_socket, (struct sockaddr*)&client_addr, &addrlen);

        if (Fork()==0) // spawn child thread to handle request
        {
            print_spinner(spin_index); // print out the loading spinner

            close(listening_socket); // prevent this thread from reacting to browser requests

            request_data req_data; // create new request_data struct to hold parsed data
            req_data.request_blocked = 0;

            char buffer[4096]; // to read the socket data into
            int n = Read(client_socket,buffer,4095); // read socket into buffer string
            buffer[n] = 0; // zero-terminate string

            //while (*wait_a_sec==1){  Sleep(1);  }
            //*wait_a_sec = 1;
            int thread_index = *thread_ct; // get the index of this thread
            *thread_ct = *thread_ct + 1; // increment the thread_ct integer
            *threads_open = *threads_open + 1; // increment the threads_open integer
            //*wait_a_sec = 0;

            if (n<0){  printf("> Error reading from socket \n");  }
            
            fprintf(request_log,"\nREQUEST (thread_index=%d):\n%s\n--------------------\n",thread_index,buffer);

            // parse the data from the listening socket into the req_data struct
            char *uri = parse_request(&req_data,buffer);

            //printf("\n---------------------------------------\n");
            //printf("Request length = %d\n",strlen(buffer));
            //printf("Request: ...\n%s\n...\n",buffer);
            //continue;

            //printf("                                                                                       \r");
            //printf("THREAD %d (client_socket=%d,threads_open=%d): %s %s...\n",thread_index,client_socket,*threads_open,req_data.req_command,req_data.req_host_long);
            fflush(stdout);

            int resp_size = 0;

            // check if the domain is in the list of blocked domains
            if ( strstr(req_data.req_host_domain,"ocsp.digicert.com")!=NULL || strstr(req_data.req_host_domain,"clients1.google.com")!=NULL || strstr(req_data.req_host_domain,"ocsp.godaddy.com")!=NULL )
            {
                resp_size = -2; // denote that we have skipped this request
                strcpy(req_data.response_code,"BLOCKED");
                close(client_socket);
                req_data.request_blocked = 1;
            }
            else
            {
                // forward the request to the remote server, recieve response, formward to client
                resp_size = fulfill_request(&req_data, client_socket,client_addr);
            }

            printf("                    \r"); // clear the spinner from the command line

            char req_host_long_shortened[1024];
            int max_host_length = 30;
            strcpy(req_host_long_shortened,req_data.req_host_long);
            req_host_long_shortened[max_host_length] = 0; // zero-terminate the string
            
            
            char spacer[30] = " ";
            spacer[1] = 0;
            for(int i=0; i<((max_host_length-strlen(req_host_long_shortened))); i++)
            {
                spacer[i] = ' ';
            }
            spacer[max_host_length-strlen(req_host_long_shortened)] = 0;
            

            //printf("%d\t(%d workers) > %s %s > [%s]\n",thread_index,*threads_open,req_data.req_command,req_host_long_shortened,req_data.response_code);
            printf("%d\t(%d workers) > %s %s %s %dB (total) > [%s,%dB]\n",thread_index,*threads_open,req_data.req_command,req_host_long_shortened,spacer,n,req_data.response_code,resp_size);
            //printf("%d\t(%d workers,client_socket=%d) > %s %s\t > [%s]\n",thread_index,*threads_open,client_socket,req_data.req_command,req_host_long_shortened,req_data.response_code);
            
            if ( *threads_open!=0 ){  print_spinner(spin_index);  }


            fflush(stdout);

            // log the information so long as the request wasn't blocked
            if ( req_data.request_blocked==0 )
            {
                // log the request as per Sakai pdf
                log_request((struct sockaddr_in*)&client_addr,uri,resp_size);

                if (debugging==1)
                {
                    // print out additional information (debugging)
                    log_information(buffer,&req_data,thread_index);
                }
            }
            
            *threads_open = *threads_open - 1;
            close(client_socket); // close the client socket
            if ( *threads_open==-1){  printf("*                 \n");  } // clear the command line if not loading a request
            exit(0); // close this thread (opened on Fork())
        }
        close(client_socket);
        if ( *threads_open==-1){  printf("                 \r");  } // clear the command line if not loading a request
    }
    Fclose(proxy_log);
    exit(0);
}

// print out the loading spinner with the angle associated with *spin_index
void print_spinner(int *spin_index)
{
    if (*spin_index==0){  printf("... |\r");  }
    if (*spin_index==1){  printf("... /\r");  }
    if (*spin_index==2){  printf("... -\r");  }
    if (*spin_index==3)
    {  
        printf("... \\\r");  
        *spin_index = 0;
    }
    else
    {
        *spin_index = *spin_index + 1;
    }
    fflush(stdout);
}

// log debugging information
void log_information(const char *buffer,const struct request_data *req_data, const int thread_id)
{
    if ( req_data->request_blocked==1 ){  return;  } // skip logging if this request was blocked

    while (*ALREADY_LOGGING_DEBUG_INFO==1){  Sleep(1);  }

    *ALREADY_LOGGING_DEBUG_INFO = 1;
    fprintf(debug_log,"\n=======================================\n");
    fprintf(debug_log,"\t================REQUEST (thread %d)================\n",thread_id);
    fprintf(debug_log,"%s",buffer);
    fprintf(debug_log,"\n\t==========SENT REQUEST [%s]===========\n",req_data->sent_time);
    if (strcmp(req_data->req_command,"GET")==0) {  fprintf(debug_log,"GET %s %s\r\n",req_data->req_host_path,req_data->req_protocol);  }
    if (strcmp(req_data->req_command,"POST")==0){  fprintf(debug_log,"POST %s %s\r\n",req_data->req_host_path,req_data->req_protocol);  }
    fprintf(debug_log,"Host: %s:%s\r\n",req_data->req_host_domain,req_data->req_host_port);
    for(int i=0; i<req_data->num_lines_after_host; i++)
    {
        // if this is the last line and its a POST request then we dont want to append a line feed to the end of the message...
        if (i==req_data->num_lines_after_host-1 && strcmp(req_data->req_command,"POST")==0)
        {  
            fprintf(debug_log,"%s",req_data->req_after_host[i]);  
        }
        else // otherwise we do to ensure '\r\n' at the end of each line
        {  
            fprintf(debug_log,"%s\n",req_data->req_after_host[i]);
        }
    }
    fprintf(debug_log,"\n\t==============RESPONSE [%s]=================\n",req_data->server_responded_time);
    int calculated_size = 0;
    for(int i=0; i<req_data->num_lines_response; i++)
    {   
        fprintf(debug_log,"%s\n",req_data->full_response[i]);
        calculated_size += sizeof(req_data->full_response[i]);
    }
    //fprintf(debug_log,"Calculated size = %d",calculated_size/4);
    fprintf(debug_log,"\n=======================================\n");
    *ALREADY_LOGGING_DEBUG_INFO = 0;
}

// log a single request in the format specified in Sakai pdf (using the 
// format_log_entry function to format data & writing to proxy.log)
void log_request( struct sockaddr_in *sockaddr,  char *uri,  int size)
{

    // busy wait until we can get a handle on the proxy.log file
    while (ALREADY_LOGGING==1){  Sleep(1);  }

    ALREADY_LOGGING = 1; // tell all other threads to wait
    char logstring[1024]; // to hold output of format_log_entry
    format_log_entry((char *)&logstring,sockaddr,uri,size); 
    fprintf(proxy_log,"%s",logstring); // print out formatted string
    fprintf(proxy_log,"\n"); // new line
    ALREADY_LOGGING = 0; // tell all other threads it's okay to write to proxy.log
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

// initializes the proxy.log file, if one already exists, it carries over that data to the new instance
void init_proxy_log()
{
    proxy_log = Fopen("proxy.log","w");
}

// initializes the debugging log
void init_debug_log()
{
    debug_log = Fopen("debug.log","w"); // initialize debugging log
}

// parses out the port number and returns it as an integer
int parse_listening_port_num(char ** argv)
{
    char * port_str = argv[1];
    int port_num = atoi(port_str);
    printf("> PORT: %d\n\n",port_num);
    return port_num;
}

// called after parse_request (from the main function), sends the
// data gathered in parse_request (now inside the req_data struct) 
// to the remote socket and waits for a response. After it has gotten
// all of the lines of the response, it sends it to the client socket
int fulfill_request(struct request_data *req_data, int client_socket, struct sockaddr_in client_addr)
{
    // record the time we connect to the server
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    sprintf(req_data->sent_time,"%d:%d:%d",timeinfo->tm_hour,timeinfo->tm_min,timeinfo->tm_sec);

    // get file handle to remote socket (supplying remote domain and remote port #)
    int remotefd = Open_clientfd(req_data->req_host_domain,req_data->req_host_port_num);

    // check to ensure the remote socket could be established...
    if (remotefd<0)
    {
        printf("\n> DNS could not connect to remote %s\n",req_data->req_host_long);
        return -1;
    }

    char buffer[BUFFER_PAGE_WIDTH]; // to hold the information we will be sending to remote socket
    if (strcmp(req_data->req_command,"GET")==0) {  sprintf(buffer,"GET %s %s\r\n",req_data->req_host_path,req_data->req_protocol);  }
    else
    {
        if (strcmp(req_data->req_command,"POST")==0){  sprintf(buffer,"POST %s %s\r\n",req_data->req_host_path,req_data->req_protocol);  }
        else
        {
            printf("\n> Could not identify req_command: %s\n",req_data->req_command);
            return -1;
        }
    }
    send(remotefd,buffer,strlen(buffer),0); // send first line to remote socket

    sprintf(buffer, "Host: %s:%s\r\n", req_data->req_host_domain,req_data->req_host_port); // fill buffer with remote host info
    send(remotefd,buffer,strlen(buffer),0); // send remote host info

    int send_all = 1;

    if (send_all==1)
    {
        // after the first two lines of the HTTP request, I have chosen
        // to simply save each line individually such that it can be essentially
        // forwarded directly to the remote socket. in this way, the following
        // loop iterates over the lines of the request I have parsed and sends
        // each individually.
        for (int i=0; i<req_data->num_lines_after_host; i++)
        {
            // if this is the last line and its a POST request then we dont want to append a line feed to the end of the message...
            if (i==req_data->num_lines_after_host-1 && strcmp(req_data->req_command,"POST")==0)
            {  
                sprintf(buffer,"%s",req_data->req_after_host[i]);  
            }
            else // otherwise we do to ensure '\r\n' at the end of each line
            {  
                sprintf(buffer,"%s\n",req_data->req_after_host[i]);
            }

            int m = 1;
            if ( (m = send(remotefd,buffer,strlen(buffer),0))<0 ) // send (i+2)th line to remote socket
            {
                printf("\nNot allowed to write to remotefd, m=%d\n",m);

            }
        }

        if ( strcmp(req_data->req_command,"POST")==0 )
        {
            sprintf(buffer,"\r\n");
            send(remotefd,buffer,strlen(buffer),0);
        }
    }
    else
    {
        sprintf(buffer,"\r\n");
        send(remotefd,buffer,strlen(buffer),0);
    }

    ssize_t n;
    int total_size = 0; // total size of the message received 
    req_data->num_lines_response = 0; // initialize number of lines in response
    strcpy(req_data->response_code,"NONE");

    fprintf(response_log,"\n\n--------------------------------------------------\n\n");

    int past_header = 0; // set to 1 once we are on a chunk that is past the header section of response

    while ((n = recv(remotefd,buffer,BUFFER_PAGE_WIDTH,0)) > 0)
    {
        fprintf(response_log,"%s",buffer);

        // if this is the first read (containing first line) parse out
        // the response code from the remote server
        if ( total_size==0 && sizeof(buffer)!=0 )
        {
            /*
            // record the time the server responded
            time_t rawtime;
            struct tm *timeinfo;
            time(&rawtime);
            timeinfo = localtime(&rawtime);
            sprintf(req_data->server_responded_time,"%d:%d:%d",timeinfo->tm_hour,timeinfo->tm_min,timeinfo->tm_sec);
            */
            char status_code[100];
            int status_code_index = 0;

            char protocol_buffer[100];
            int past_protocol = 0;

            for (int k=0; k<100; k++)
            {
                char current_char = buffer[k];

                if ( past_protocol==1 )
                {
                    if ( current_char=='\r' )
                    {
                        break;
                    }
                    else
                    {
                        status_code[status_code_index] = buffer[k];
                        status_code[status_code_index+1] = 0;
                        status_code_index++;
                    }
                }
                else
                {
                    protocol_buffer[k] = buffer[k];
                    //protocol_buffer[k+1] = 0;
                    if ( strstr(protocol_buffer,"HTTP/1.1 ")!=NULL )
                    {
                        past_protocol = 1;
                    }

                }
            }

            if (status_code_index!=0)
            {
                strcpy(req_data->response_code,status_code);
            }
        }

        if ( past_header==1 )
        {
            total_size += n;
        }
        else
        {
            // if we have hit an empty line
            if ( strstr(buffer,"\r\n\r\n")!=NULL )
            {
                past_header = 1;
                char *temp_char = strstr(buffer,"\r\n\r\n");
                total_size = strlen(temp_char);
            }
        }
        //total_size += n;

        int m=0;
        if ( (m = send(client_socket,buffer,n,0))<0 )
        {
            printf("\nNot allowed to write to client_socket, m=%d\n",m);
        }
        //send(client_socket,buffer,n,0); // forward data from remote host to client

        // save the line into the req_data struct
        strcpy(req_data->full_response[req_data->num_lines_response],buffer); 
        req_data->num_lines_response++; // increment number of response lines
    }

    // if we never recieved any data fill in the response time
    //if (total_size==0)
    //{
        // record the time the server responded
    time_t rawtime2;
    struct tm *timeinfo2;
    time(&rawtime2);
    timeinfo2 = localtime(&rawtime2);
    sprintf(req_data->server_responded_time,"%d:%d:%d",timeinfo2->tm_hour,timeinfo2->tm_min,timeinfo2->tm_sec);
    //}

    fprintf(response_log,"\n\n--------------------------------------------------\n\n");

    close(remotefd); // close remote socket
    close(client_socket); // close client socket after done writing

    return total_size;
}

char* parse_request(struct request_data *req_data, char *buffer)
{
    char buffer_copy[strlen(buffer)+1];
    strcpy(buffer_copy,buffer); // copy the input buffer

    int line_index_after_host = 0;

    char *end_str;
    char *line = strtok_r(buffer_copy,"\n",&end_str);

    int line_index = 0;

    // each iteration of while loop looks at a single line of the request (held in buffer_copy)
    while (line != NULL)
    {
        if (line_index>1)
        {
            // if we have already seen the first two lines we can just record the exact
            // line to the request_data struct (only first two are parsed for data)
            strcpy(req_data->req_after_host[line_index_after_host],line);
            line_index_after_host++;
        }

        if (line_index<=1)
        {
            // if either the first or second line we will parse through each word 
            // individually to gather the data we need to establish the connection
            // with the remote server when we forward the request
            char *end_token; 
            char *word = strtok_r(line," ",&end_token); // split line on " " space character

            int word_index = 0;
            while (word != NULL)
            {
                // Iterate over each word in line
                if (line_index==0 && word_index==0){  strcpy(req_data->req_command,word);  }
                if (line_index==0 && word_index==1){  strcpy(req_data->req_host_long,word);  }
                if (line_index==0 && word_index==2){  strcpy(req_data->req_protocol,word);  }
                if (line_index==1 && word_index==1){  strcpy(req_data->req_host_domain,word);  }

                word = strtok_r(NULL," ",&end_token);
                word_index++;
            }
        }
        line = strtok_r(NULL,"\n",&end_str);
        line_index++;
    }

    req_data->num_lines_after_host = line_index_after_host; // record the number of lines in request
    //check_for_host_port(req_data); // check the req_host_long string for a port specification
    fix_data(req_data); // apply any changes to the data we need before calling to DNS
    return req_data->req_host_long; // return the URI (long URL)
}

// apply any changes to the data we need before continuing
void fix_data(struct request_data *req_data)
{
    strip_trailing_char(req_data->req_host_domain,'\r'); // remove '\r' from domain name
    strip_trailing_char(req_data->req_protocol,'\r'); // remove '\r' from the protocol type
    get_host_path_and_port(req_data); // get the path specified in the URL (after the domain name) & the port (if one)
}

// get the host path from the URL
void get_host_path_and_port(struct request_data *req_data)
{
    //strcpy(req_data->req_host_path,""); // empty the path container
    //strcpy(req_data->req_host_port,""); // empty the port container

    char buffer[1024]; // fills up until we have found 'http://' portion of URL

    int past_http = 0; // if we are past the 'http://' portion of URL
    int inside_port = 0; // if we are iterating over port in URL
    int recording_path = 0; // if we are past the domain and port portion of URL
    
    int buffer_index = 0; // index to write into 'buffer'
    int path_index = 0; // index to write into 'req_data->req_host_path' string
    int port_index = 0; // index to write into 'req_data->req_host_port' string 

    int found_port = 0; // set to 1 if we parse out a port number

    for(int i=0; i<strlen(req_data->req_host_long); i++)
    {
        char current_char = req_data->req_host_long[i];

        // initial condition, if we haven't gotten past the http:// portion of the string yet
        if (past_http==0)
        {
            //printf("\n%s\n",buffer);
            // write the current character into the buffer
            buffer[buffer_index] = current_char;
            buffer_index++;    

            // check if the buffer now contains the full 'http://' string
            if (strcmp(buffer,"http://")==0)
            {  
                past_http = 1;  // denote that we are past, if so
                continue; // skip the rest of the conditions
            }

            // check if the buffer is long enough (if the above check fails)
            if (buffer_index>=7)
            {
                if (buffer[5]=='/' && buffer[6]=='/')
                {
                    past_http = 1;    
                    continue;
                }
            } 
            continue;
        }

        // if we have already started recording the path continue recording it
        if (recording_path==1)
        {
            //printf("\n recording path\n");
            // don't want to record endline or carriage return in the path name
            if ( current_char=='\r' || current_char=='\n' ){  continue;  }

            req_data->req_host_path[path_index] = current_char;
            path_index++;
            continue;
        }

        // if we are already past the http:// portion of the URL and we have not
        // gotten to a '/' or ':' character yet. If we reach a '/' it means that
        // there is not a port specified and we have started the path, if we reach
        // a ':' it means we have started the port portion of the URL
        if (past_http==1 && inside_port==0)
        {
            // if we have encountered a '/' after the http:// portion 
            // of the URL then we should start recording this as the 
            // path (at the end of the URL) 
            if ( current_char=='/' )
            {
                recording_path = 1;
                req_data->req_host_path[0] = current_char;
                path_index++;
                continue;
            }

            // if we have encountered a ':' after the http:// portion 
            // of the URL then we shouldn't include this in the path
            // because it's referring to the host port specification
            if ( current_char==':' )
            {
                inside_port = 1; // denote that we are inside of a port number
                continue;
            }
        }

        // if we have been inside of a port number specification, check if over
        if (inside_port==1)
        {
            //printf("\n recording port number\n");
            // check if the port number is done
            if ( current_char=='/' )
            {
                recording_path = 1;
                inside_port = 0;
                req_data->req_host_path[0] = current_char;
                path_index++;
                continue;
            }

            // if inside the port number, record the data into the req_data->req_host_long string
            found_port = 1;
            req_data->req_host_port[port_index] = current_char;
            port_index++;
        }
    }

    // if we were not able to parse out a port number, set equal to the default of '80'
    if ( found_port==0 )
    {  
        strcpy(req_data->req_host_port,"80");  
        req_data->req_host_port_num = 80;
    }
    // convert parsed port to integer and set appropriate member in req_data
    else
    {
        int port_num = atoi(req_data->req_host_port);
        req_data->req_host_port_num = port_num;
    }

    //printf("\n>> Found path: %s\n",req_data->req_host_path);
    //printf(">> Found port: %s\n",req_data->req_host_port);
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

// count the number of occurrences of target in input
int get_match_count(const char *input, const char *target)
{
    int count = 0;
    const char *tmp = input;
    while ( (tmp = strstr(tmp, target)) > 0)
    {
        count++;
        tmp++;
    }
    return count;
}