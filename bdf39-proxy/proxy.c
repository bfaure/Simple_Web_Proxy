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

#define BUFFER_PAGE_WIDTH 4096 // 1024
#define BUFFER_PAGE_COUNT 10   // 1024

// handle separate naming for Windows, Apple, and Unix machines
#ifdef _WIN32
    MAP_A = MAP_ANONYMOUS;
#elif defined __APPLE__
    MAP_A  = MAP_ANON;
#else
    MAP_A = MAP_ANONYMOUS;
#endif


FILE *response_log;
FILE *request_log;
FILE *status_log;

/*
 * Function prototypes
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);

// if 1, data will be written to debug.log file
int debugging = 1;

// list of domains I have added to the list to block
const int num_blocked_domains = 0;
const char *blocked_domains[5] =    {  
                                    "ocsp.digicert.com",
                                    "clients1.google.com",
                                    "ocsp.godaddy.com",
                                    "tg.symcd.com",
                                    "gp.symcd.com"
                                    };

FILE *debug_log; // file to save debugging info to, can be accessed from any function in this file 
FILE *proxy_log; // file to log each request (as per instructions)

char *current_status; // to hold what we are currently processing

// function to initialize the proxy.log file, carries over data if already written to
void init_proxy_log();

// function to initialize the debug_log.txt file
void init_debug_log();

// parses the port number from the command line arguments
int parse_listening_port_num(char ** argv);

int PROCESSING_REQUEST = 0; // set to 1 while a request is being processed to prevent concurrency problems
int ALREADY_LOGGING = 0; // set to 1 while a request is being written to the proxy.log file
int *spin_index; // can be in range [0,3], denotes the current orientation of the loading spinner
int *ALREADY_LOGGING_DEBUG_INFO; // set to 1 while debuggin data is being written

// data structure to hold all essential information pertaining to a request
typedef struct request_data
{
    char request_buffer[4096]; // unparsed, entire contents of request
    int request_buffer_size; // size of the request_buffer, as reported by socket read

    // all data in request past the POST or GET 
    char req_after_host[24][1024]; // broken by line (max 24)
    int num_lines_after_host; // track number of lines used

    int specified_req_size; // the size the client says the request content is (if one)

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
    int specified_resp_size; // the size the server says the response will be (if one)

    int request_blocked; // 1 if blocked, 0 otherwise
    int thread_id; // ID of the thread processing this request

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

// prints the spinner to CLI according to spin index (in range [0,3]),
// also updates the spin_index
void print_spinner();

// prints the spinner to CLI according to spin index, NO UPDATING of index
void reprint_spinner();

// initialize the header lines of the table for the CLI
void init_ui_header(const int listening_port, const int listening_socket);

// check if input is in the list of blocked domains (blocked_domains)
int is_blocked_domain(struct request_data *req_data);

// returns a shorter version of the input string
char* shorten_string(const char *input_str, const int new_size);

// returns a char array of spaces (" ") of length 'size' (must be < 30)
char* get_spacer_string(const int size);

// prints out the current state of the process
void update_cli(struct request_data *req_data, int thread_index, int threads_open, int resp_size, int request_size, int total_time);

// parses the response code (e.g. 200 OK) from the server response
void parse_response_code(const char *buffer, struct request_data *req_data);

// get the elapsed time between two intervals (clock() inputs)
long elapsed_time(clock_t t1, clock_t t2);

// get the elapsed time between two intervals (time() inputs, threadsafe)
int elapsed_time2(time_t t1, time_t t2);

// sets the global variable current_status
void set_current_status(const char *input_str);

// sets the global variable current_status and appends the threadid
void set_current_status_id(const char *input_str, const int thread_id);

// get a formatted string representing the current time (H:M:S)
char* get_time_string();

// initialize global, cross-thread variables
void init_global_variables();

/* 
 * main - Main routine for the proxy program 
 */
int main(int argc, char **argv)
{
    init_global_variables();    // initialize cross-thread global variables
    init_debug_log(argc,argv);  // initialize the debug.log file
    init_proxy_log();           // initialize the proxy.log file
    
    /* Check arguments */
    if (argc != 2) 
    {
	    fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        Fclose(debug_log);
	    exit(0);
    }

    int listening_port      = parse_listening_port_num(argv); // parse the listening port number
    int listening_socket    = Open_listenfd(listening_port); // initialize the listening socket

    init_ui_header(listening_port,listening_socket); // draw out the table header for the CLI
    
    // initialize cross-thread variables...
    int *thread_ct  = mmap(NULL,sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_A, -1, 0);
    *thread_ct      = 0; // unique id for each thread (thread index)

    int *threads_open   = mmap(NULL,sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_A, -1, 0);
    *threads_open       = -1; // number of working theads executing

    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);

    while (1)
    {
        // wait for a connection from a client at listening socket
        int client_socket   = Accept(listening_socket, (struct sockaddr*)&client_addr, &addrlen);
        int new_pid         = Fork(); // spawn child thread to handle request

        switch (new_pid)
        {
            case 0: // new child
                reprint_spinner(); // print out the spinner
                request_data req_data; // create new request_data struct to hold parsed data

                req_data.request_blocked    = 0; // initialize int to denote that request has not yet been blocked
                req_data.thread_id          = *thread_ct; // save the index of this thread to req_data
                *thread_ct                  = *thread_ct + 1; // increment the thread_ct integer
                *threads_open               = *threads_open + 1; // increment the threads_open integer

                print_spinner(); // print out the loading spinner
                close(listening_socket); // prevent this thread from reacting to browser requests
                close(listening_port); // prevent this thread from reacting to browser requests

                set_current_status_id("Reading socket",req_data.thread_id);
                char buffer[4096]; // to read the socket data into

                int request_size        = Read(client_socket,buffer,4095); // read socket into buffer string
                buffer[request_size]    = 0; // zero-terminate string

                memcpy(req_data.request_buffer,buffer,request_size); // copy the buffer into req_data
                req_data.request_buffer_size = request_size; // save the size of the socket read

                char *temp_buffer3 = malloc(sizeof(char)*50);
                sprintf(temp_buffer3,"request_size=%d, strlen(buffer)=%d",request_size,strlen(buffer));
                set_current_status_id(temp_buffer3,req_data.thread_id);

                if (request_size<0){  printf("> Error reading from socket \n");  }
                
                fprintf(request_log,"\nREQUEST (thread_index=%d):\n%s\n--------------------\n",req_data.thread_id,buffer);

                // record the start time of the request
                time_t t1;
                time(&t1);

                // parse the data from the listening socket into the req_data struct
                char *uri = parse_request(&req_data,buffer);

                int resp_size = 0;

                if ( is_blocked_domain(&req_data) ) // check if the domain is in the list of blocked domains
                {
                    strcpy(req_data.response_code,"BLOCKED");
                    close(client_socket);
                    req_data.request_blocked = 1; // denote that we have skipped this request
                    set_current_status_id("Request blocked",req_data.thread_id); 
                }

                // forward the request to the remote server, recieve response, formward to client
                else{  resp_size = fulfill_request(&req_data, client_socket,client_addr);  }

                // record the end time of the request
                time_t t2;
                time(&t2);
                int request_time = elapsed_time2(t1,t2);

                // update the command line interface UI
                update_cli(&req_data,req_data.thread_id,*threads_open,resp_size,request_size,request_time);
                if ( *threads_open!=0 ){  print_spinner();  }
                fflush(stdout);

                // log the information so long as the request wasn't blocked
                if ( req_data.request_blocked==0 )
                {
                    // log the request as per Sakai pdf
                    log_request((struct sockaddr_in*)&client_addr,uri,resp_size);

                    if (debugging==1)
                    {
                        // print out additional information (debugging)
                        log_information(buffer,&req_data,req_data.thread_id);
                    }
                }
                
                *threads_open = *threads_open - 1;
                close(client_socket); // close the client socket

                 // clear the command line if not loading a request
                if ( *threads_open==-1){  printf("*                                                                                          \n");  }
                exit(0); // close this thread (opened on Fork())

            case -1: // error when creating new thread
                printf("\nERROR: Could not spawn new thread (%d)\n",new_pid);
                close(client_socket);
                return -1;

            default: // this is the parent thread
                close(client_socket); // close the socket we left to the child
        }
        // clear the command line if not loading a request
        if ( *threads_open==-1){  printf("                                                                                              \r");  } 
    }
    Fclose(proxy_log);
    exit(0);
}

// initialize global, cross-thread variables
void init_global_variables()
{
    ALREADY_LOGGING_DEBUG_INFO = mmap(NULL,sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_A, -1, 0);
    *ALREADY_LOGGING_DEBUG_INFO = 0;

    spin_index = mmap(NULL,sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_A, -1, 0);
    *spin_index = 0;

    response_log = Fopen("response.log","w"); // initialize debugging log
    request_log = Fopen("request.log","w"); // initialize debugging log
    status_log = Fopen("status.log","w"); // initialize debugging log

    // variable to hold current status
    current_status = mmap(NULL,sizeof(char), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_A,-1,0);
    set_current_status(" ");
}

// get a formatted string representing the current time (H:M:S)
char* get_time_string()
{
    char *time_str = malloc(sizeof(char)*20);
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    sprintf(time_str,"%d:%d:%d",timeinfo->tm_hour,timeinfo->tm_min,timeinfo->tm_sec);
    return time_str;
}


// sets the global variable current_status and appends the threadid
void set_current_status_id(const char *input_str, const int thread_id)
{
    char temp[100];
    sprintf(temp,"(idx=%d) - %s",thread_id,input_str);
    strcpy(current_status,temp);
    fprintf(status_log,"%s\t>%s\n",get_time_string(),temp);
    reprint_spinner(); // reprint the spinner with new status
}

// sets the global variable current_status
void set_current_status(const char *input_str)
{
    strcpy(current_status,input_str);
    fprintf(status_log,"%s\t>%s\n",get_time_string(),input_str);
    reprint_spinner(); // reprint the spinner with new status
}


// get the elapsed time between two intervals (time() inputs, threadsafe)
int elapsed_time2(time_t t1, time_t t2)
{
    double diff = difftime(t2,t1);
    return (int)diff;
}

// get the elapsed time between two intervals (clock() inputs)
long elapsed_time(clock_t t1, clock_t t2)
{
    long elapsed;
    elapsed = ((double)t2 - (double)t1) / CLOCKS_PER_SEC * 1000;
    return elapsed;
}


// prints out the current state of the process to the CLI
void update_cli(struct request_data *req_data, int thread_index, int threads_open, int resp_size, int request_size, int total_time)
{
    // initialize some variables to help with printing 
    printf("                    \r"); // clear the spinner from the command line
    int max_host_length = 30;
    char *req_host_long_shortened = shorten_string(req_data->req_host_long, max_host_length);
    char *spacer = get_spacer_string(max_host_length-strlen(req_host_long_shortened));

    if ( strstr(req_data->req_command,"POST")!=NULL )
    {
        printf("%d\t(%d workers) > %s %s %s %dB > [%s,%dB] \t %d s\n",thread_index,threads_open,req_data->req_command,req_host_long_shortened,spacer,request_size,req_data->response_code,resp_size,total_time);
    }
    else
    {
        printf("%d\t(%d workers) > %s  %s %s %dB > [%s,%dB] \t %d s\n",thread_index,threads_open,req_data->req_command,req_host_long_shortened,spacer,request_size,req_data->response_code,resp_size,total_time);
    }
}

// returns a char array of spaces (" ") of length 'size' (must be < 30)
char* get_spacer_string(const int size)
{
    //char spacer[30] = " ";
    char *spacer = malloc(sizeof(char)*30);
    spacer[1] = 0;
    for(int i=0; i<size; i++)
    {
        spacer[i] = ' ';
    }
    spacer[size] = 0;
    return spacer;
}

// returns a shorter version of the input string
char* shorten_string(const char *input_str, const int new_size)
{
    char *shortened = malloc(sizeof(char)*1024);
    //char shortened[1024];
    strcpy(shortened,input_str);
    shortened[new_size] = 0; // zero-terminate the string
    return shortened;
}

// check if the domain name of the req_data struct is in the list of blocked domains
int is_blocked_domain(struct request_data *req_data)
{
    // iterate over each blocked domain and check if it matches the input domain name
    for ( int i=0; i<num_blocked_domains; i++)
    {
        if ( strstr(req_data->req_host_domain,blocked_domains[i])!=NULL )
        {
            return 1; // we have a match
        }
    }
    return 0; // the domain is not blocked
}

// print out the header lines for the CLI UI table
void init_ui_header(const int listening_port, const int listening_socket)
{
    printf("\n=====================================================================================================\n");
    printf("=====================================================================================================\n");
    printf("> PORT: %d SOCKET: %d\n\n",listening_port,listening_socket);
    printf("idx                         req. snippet                  req.size  response              total time\n");
    printf("_____________________________________________________________________________________________________\n");
}

// print out the loading spinner with the angle associated with *spin_index
void print_spinner()
{
    if (*spin_index==0){  printf("... | %s\r",current_status);  }
    if (*spin_index==1){  printf("... / %s\r",current_status);  }
    if (*spin_index==2){  printf("... - %s\r",current_status);  }
    if (*spin_index==3)
    {  
        printf("... \\ %s\r",current_status);  
        *spin_index = 0;
    }
    else
    {
        *spin_index = *spin_index + 1;
    }
    fflush(stdout);
}

// same as print_spinner but does not update spin_index (called by status updating
// functions so that the CLI status can be updated w/o changing the spinner)
void reprint_spinner()
{
    printf("                                                                                          \r");
    if (*spin_index==0){  printf("... | %s\r",current_status);  }
    if (*spin_index==1){  printf("... / %s\r",current_status);  }
    if (*spin_index==2){  printf("... - %s\r",current_status);  }
    if (*spin_index==3){  printf("... \\ %s\r",current_status); }
    fflush(stdout);
}

// log debugging information
void log_information(const char *buffer,const struct request_data *req_data, const int thread_id)
{
    if ( req_data->request_blocked==1 ){  return;  } // skip logging if this request was blocked

    while (*ALREADY_LOGGING_DEBUG_INFO==1){  Sleep(1);  }

    *ALREADY_LOGGING_DEBUG_INFO = 1;
    fprintf(debug_log,"\n\n=============================================================\n");
    fprintf(debug_log,"====================REQUEST (thread %d)====================\n",thread_id);
    fprintf(debug_log,"%s",buffer);
    fprintf(debug_log,"\n==================SENT REQUEST [%s]===================\n",req_data->sent_time);
    if (strcmp(req_data->req_command,"GET")==0) {  fprintf(debug_log,"GET %s %s\r\n",req_data->req_host_path,req_data->req_protocol);  }
    if (strcmp(req_data->req_command,"POST")==0){  fprintf(debug_log,"POST %s %s\r\n",req_data->req_host_path,req_data->req_protocol);  }
    fprintf(debug_log,"Host: %s:%s\r\n",req_data->req_host_domain,req_data->req_host_port);
    for(int i=0; i<req_data->num_lines_after_host; i++)
    {
        fprintf(debug_log,"%s",req_data->req_after_host[i]);
    }
    fprintf(debug_log,"\n===================RESPONSE [%s]======================\n",req_data->server_responded_time);
    int calculated_size = 0;
    for(int i=0; i<req_data->num_lines_response; i++)
    {   
        fprintf(debug_log,"%s\n",req_data->full_response[i]);
        calculated_size += sizeof(req_data->full_response[i]);
    }
    //fprintf(debug_log,"Calculated size = %d",calculated_size/4);
    fprintf(debug_log,"\n=============================================================\n");
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
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size)
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
    return port_num;
}

// parses the response code (e.g. 200 OK) from the server response
void parse_response_code(const char *buffer, struct request_data *req_data)
{
    char status_code[100];
    int status_code_index = 0;

    char protocol_buffer[100];
    int past_protocol = 0;

    // iterate over first 100 chars in response buffer
    for (int k=0; k<100; k++)
    {
        char current_char = buffer[k];

        if ( past_protocol==1 )
        {
            if ( current_char=='\r' ){  break;  }
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
            if ( strstr(protocol_buffer,"HTTP/1.1 ")!=NULL ){  past_protocol = 1;  }
        }
    }
    if (status_code_index!=0){  strcpy(req_data->response_code,status_code);  }
}

// parses the specified response size (Content-Length), if one, returns
// 1 if it finds a specified size and sets req_data->specified_resp_size 
// equal to it, returns 0 if none found
int parse_response_size(const char *buffer, struct request_data *req_data)
{
    char cur_line_buffer[1000];
    int cur_line_buffer_index = 0;

    char size_buffer[20];
    int size_buffer_index = 0;

    int on_content_length_line = 0;

    int farthest_possible_content_length_location = 900;

    int i=0;
    while (1) // iterate over each character in buffer
    {
        if ( i>=farthest_possible_content_length_location )
        {
            return 0;
        }

        if ( i>=strlen(buffer) ) // if we have reached the end of buffer string
        {
            //printf("\nERROR: parse_request_size(), buffer=%s\ncur_line_buffer=%s\n",buffer,cur_line_buffer);
            return 0;
        }

        if (buffer[i]=='\n') // if we have reached the end of a line
        {
            if ( on_content_length_line ) // if this is the Content-Length line
            {
                int found_space = 0; 
                for (int j=0; j<strlen(cur_line_buffer); j++) // iterate over characters in Content-Length line
                {
                    if ( cur_line_buffer[j]=='\r') // if this is the last character in the line
                    {
                        if (found_space)
                        {
                            size_buffer[size_buffer_index] = 0;
                            req_data->specified_resp_size = atoi(size_buffer);
                            return 1;
                        }
                        printf("\rERROR: parse_response_size(), cur_line_buffer=%s\n",cur_line_buffer);
                        return 0;
                    }
                    if ( found_space ) // if we have already found ' ' after 'Content-Length'
                    {
                        size_buffer[size_buffer_index] = cur_line_buffer[j];
                        size_buffer_index++;
                        continue;
                    }
                    if (cur_line_buffer[j]==' ')
                    {
                        found_space = 1;
                        continue;
                    }
                }
            }

            if ( buffer[0]=='\r') // if the only thing in the buffer is '\r'
            {
                return 0; // return false because we have reached the end of header
            }

            // if the only thing in the buffer wasn't '\r' then we can continue
            // iterating through the header items individually after clearing cur_line_buffer

            // flush the cur_line_buffer array
            //memset(&cur_line_buffer[0],0,sizeof(cur_line_buffer));
            cur_line_buffer[0] = 0;
            cur_line_buffer_index = 0;
            i+=1;
            continue; // skip to the next character
        }

        // if we get here then we should record the current character into the cur_line_buffer
        cur_line_buffer[cur_line_buffer_index] = buffer[i];
        cur_line_buffer[cur_line_buffer_index+1] = 0;
        cur_line_buffer_index++;

        // if the buffer now contains 'Content-Length' we should set 'on_content_length_line'
        // to 1 such that, upon reaching the next '\n', we will enter the case above
        if ( strstr(cur_line_buffer,"Content-Length")!=NULL ) // if this line contains 'Content-Length'
        {
            on_content_length_line = 1;
        }
        i+=1;
    }
}

// called after parse_request (from the main function), sends the
// data gathered in parse_request (now inside the req_data struct) 
// to the remote socket and waits for a response. After it has gotten
// all of the lines of the response, it sends it to the client socket
int fulfill_request(struct request_data *req_data, int client_socket, struct sockaddr_in client_addr)
{
    strcpy(req_data->sent_time, get_time_string()); // record the time we connect to the server
    set_current_status_id("Connecting to remote socket",req_data->thread_id); // update UI

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

    set_current_status_id("Forwarding request",req_data->thread_id); // update UI

    // forward the rest of the data (data after first two lines)
    for (int i=0; i<req_data->num_lines_after_host; i++)
    {
        int m = send( remotefd, req_data->req_after_host[i], sizeof(char)*strlen(req_data->req_after_host[i]), 0);
        if (m<0){  printf("\nERROR: could not write to remote socket\n");  }
    }
    int m = send( remotefd, "\r\n", 2,0);

    ssize_t n;
    int total_size = 0; // total size of the message received 
    req_data->num_lines_response = 0; // initialize number of lines in response
    strcpy(req_data->response_code,"NONE");

    fprintf(response_log,"\n\n--------------------------------------------------\n\n");

    int past_header = 0; // set to 1 once we are on a chunk that is past the header section of response
    int resp_length[BUFFER_PAGE_COUNT] = {0}; // initialize array to hold buffered response lengths

    set_current_status_id("Waiting for response",req_data->thread_id); // update UI
    buffer[1] = 0; // zero-terminate buffer before reading
    req_data->specified_resp_size = -1;

    int actual_total_size = -1;

    while ((n = recv(remotefd,buffer,BUFFER_PAGE_WIDTH-1,0)) > 0)
    {
        buffer[n] = 0; // zero-terminate buffer

        fprintf(response_log,"%s",buffer); // write the response to response.log file

        // if this is the first read (containing first line) parse out
        // the response code from the remote server
        if ( total_size==0 && sizeof(buffer)!=0 )
        {  
            set_current_status_id("Got first response",req_data->thread_id);
            parse_response_code(buffer,req_data);

            int has_response_size = parse_response_size(buffer,req_data);
            if (has_response_size)
            {
                char *temp_buffer = malloc(sizeof(char)*50);
                sprintf(temp_buffer,"Found response size (%d)",req_data->specified_resp_size);
                set_current_status_id(temp_buffer,req_data->thread_id);
            }
            else // mark that we have no specified size
            {
                req_data->specified_resp_size = -1;
            }  
        }

        // if we are past the header section of the response
        if ( past_header==1 ){  total_size += n;  } // increment the recieved size
        
        // if we are not past the header section yet
        else
        {
            // if we have hit an empty line
            if ( strstr(buffer,"\r\n\r\n")!=NULL )
            {
                past_header = 1; // denote that we have past the header portion
                char *delim = strstr(buffer,"\r\n\r\n");
                int delim_index = (int)(delim - buffer);
                //total_size = n - strlen(temp);
                total_size = n - delim_index - 4;
            }
        }

        if ( req_data->specified_resp_size != -1)
        {
            if ( total_size >= req_data->specified_resp_size )
            {
                actual_total_size = total_size-n;
                n = (req_data->specified_resp_size - (total_size-n));
                actual_total_size = actual_total_size+n;
            }
        }

        else
        {
            char *delim_start = strstr(buffer,"\r\n\r\n");

            if ( delim_start != NULL )
            {
                int delim_index = (int)(delim_start - buffer)+4;
                actual_total_size = total_size-n+delim_index;
                n = delim_index;
            }
        }

        // copy the data we just gathered into the req_data struct
        memcpy(req_data->full_response[req_data->num_lines_response],buffer,n); 
        resp_length[req_data->num_lines_response] = (int)n; // record the length of buffered response
        req_data->num_lines_response++; // increment number of response lines

        // check if we have found the entire message
        if (req_data->specified_resp_size!=-1)
        {
            if (total_size>=(req_data->specified_resp_size))
            {
                char *temp_buffer4 = malloc(sizeof(char)*100);
                sprintf(temp_buffer4,"Reached end of message (size=%d)",actual_total_size);
                set_current_status_id(temp_buffer4,req_data->thread_id);
                break;
            }
        }
        // if there is no specified size im assuming we can break if we find a '\r\n\r\n' sequence
        // because, if no Content-Size, there will be no body (only a header) so we can break
        // if we find the standard separator between header and body
        else
        {
            if ( strstr(buffer,"\r\n\r\n")!=NULL )
            {
                set_current_status_id("Reached end of message (no size)",req_data->thread_id);
                break;
            }
        }
    }
    
    set_current_status_id("Forwarding response to client",req_data->thread_id);
    
    // send the data we collected to the web browser (client)
    for (int i=0; i<req_data->num_lines_response; i++)
    {
        int m=0;
        if ( (m=send(client_socket,req_data->full_response[i],resp_length[i],0))<0 )
        {
            printf("\nNot allowed to write to client_socket, m=%d\n",m);
        }
    }

    set_current_status_id("Finished forwarding response",req_data->thread_id);
    // record the time the server responded
    strcpy(req_data->server_responded_time, get_time_string());

    fprintf(response_log,"\n\n--------------------------------------------------\n\n");

    close(remotefd); // close remote socket
    close(client_socket); // close client socket after done writing
    if (actual_total_size!=-1)
    {
        return actual_total_size;
    }
    return total_size;
}

// parses the specified request size (Content-Length), if one, returns
// 1 if it finds a specified size and sets req_data->specified_req_size 
// equal to it, returns 0 if none found
int parse_request_size(const char *buffer, struct request_data *req_data)
{
    char cur_line_buffer[1000];
    int cur_line_buffer_index = 0;

    char size_buffer[20];
    int size_buffer_index = 0;

    int on_content_length_line = 0;

    int farthest_possible_content_length_location = 900;

    int i=0;
    while (1) // iterate over each character in buffer
    {
        if ( i>=farthest_possible_content_length_location )
        {
            return 0;
        }

        if ( i>=strlen(buffer) ) // if we have reached the end of buffer string
        {
            //printf("\nERROR: parse_request_size(), buffer=%s\ncur_line_buffer=%s\n",buffer,cur_line_buffer);
            return 0;
        }

        if (buffer[i]=='\n') // if we have reached the end of a line
        {
            if ( on_content_length_line ) // if this is the Content-Length line
            {
                int found_space = 0; 
                for (int j=0; j<strlen(cur_line_buffer); j++) // iterate over characters in Content-Length line
                {
                    if ( cur_line_buffer[j]=='\r') // if this is the last character in the line
                    {
                        if (found_space)
                        {
                            size_buffer[size_buffer_index] = 0;
                            req_data->specified_req_size = atoi(size_buffer);
                            return 1;
                        }
                        printf("\rERROR: parse_request_size(), cur_line_buffer=%s\n",cur_line_buffer);
                        return 0;
                    }
                    if ( found_space ) // if we have already found ' ' after 'Content-Length'
                    {
                        size_buffer[size_buffer_index] = cur_line_buffer[j];
                        size_buffer_index++;
                        continue;
                    }
                    if (cur_line_buffer[j]==' ')
                    {
                        found_space = 1;
                        continue;
                    }
                }
            }

            if ( buffer[0]=='\r') // if the only thing in the buffer is '\r'
            {
                return 0; // return false because we have reached the end of header
            }

            // if the only thing in the buffer wasn't '\r' then we can continue
            // iterating through the header items individually after clearing cur_line_buffer

            // flush the cur_line_buffer array
            //memset(&cur_line_buffer[0],0,sizeof(cur_line_buffer));
            cur_line_buffer[0] = 0;
            cur_line_buffer_index = 0;
            i+=1;
            continue; // skip to the next character
        }

        // if we get here then we should record the current character into the cur_line_buffer
        cur_line_buffer[cur_line_buffer_index] = buffer[i];
        cur_line_buffer[cur_line_buffer_index+1] = 0;
        cur_line_buffer_index++;

        // if the buffer now contains 'Content-Length' we should set 'on_content_length_line'
        // to 1 such that, upon reaching the next '\n', we will enter the case above
        if ( strstr(cur_line_buffer,"Content-Length")!=NULL ) // if this line contains 'Content-Length'
        {
            on_content_length_line = 1;
        }
        i+=1;
    }
}

// parses the request sent by the web browser (called before fulfill_request)
char* parse_request(struct request_data *req_data, char *buffer)
{
    set_current_status_id("Parsing request",req_data->thread_id); // update UI

    int content_size_specified = parse_request_size(buffer,req_data);
    if (content_size_specified)
    {
        // if there is a 'Content-Length' specified in request
        char *temp_buffer = malloc(sizeof(char)*50);
        sprintf(temp_buffer,"Found request size (%d)",req_data->specified_req_size);
        set_current_status_id(temp_buffer,req_data->thread_id);
    }
    else
    {
        // if there is no 'Content-Length' specified in request
        set_current_status_id("No request size",req_data->thread_id);
    }


    //char buffer_copy[strlen(buffer)+1];
    //strcpy(buffer_copy,buffer); // copy the input buffer

    char buffer_copy[req_data->request_buffer_size];
    memcpy(buffer_copy,req_data->request_buffer,req_data->request_buffer_size);

    int line_index_after_host = 0;

    char *end_str;
    char *line = strtok_r(buffer_copy,"\n",&end_str);

    int line_index = 0;

    // each iteration of while loop looks at a single line of the request (held in buffer_copy)
    while (line != NULL)
    {
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

        if (line_index==1)
        {
            // if this is the second line then we want to exit (we have all the data we need)
            // and save the rest of the data into the first spot in the req_after_host array
            // in the req_data struct

            char *delim = strstr(req_data->request_buffer,"\r\n\r\n");
            int delim_index = (int)(delim-req_data->request_buffer);
            int delim_size = 4;

            int post_delim_data_size = req_data->request_buffer_size-delim_index-delim_size;

            char *first_line_CRENDL = strstr(req_data->request_buffer,"\r\n");
            char *second_line_CRENDL = strstr(first_line_CRENDL+4,"\r\n");

            int start_snip = (int)(second_line_CRENDL - req_data->request_buffer)+2;
            int snip_length = (int)(req_data->request_buffer_size - start_snip);

            char *temp_buffer2 = malloc(4096);
            sprintf(temp_buffer2,"Content-Length (request) = %d, snip length = %d",post_delim_data_size,snip_length);
            set_current_status_id(temp_buffer2,req_data->thread_id);

            memcpy(req_data->req_after_host[0],&req_data->request_buffer[start_snip],snip_length);
            req_data->num_lines_after_host = 1;
            break;
        }

        line = strtok_r(NULL,"\n",&end_str);
        line_index++;
    }

    fix_data(req_data); // apply any changes to the data we need before calling to DNS

    set_current_status_id("Finished parsing request",req_data->thread_id);
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