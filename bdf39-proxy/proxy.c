/*
 * proxy.c - A Simple Sequential Web proxy
 *
 * Course Name: 14:332:456-Network Centric Programming
 * Assignment 2
 * Student Name: Brian Faure
 * 
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.

When the program is first started, the main function will attempt to make
a connection to the local socket, if able to, it will enter a loop and begin
waiting for requests. When a request is received, a new thread is started to
manage the processing of the request while the initial thread is left to return
and continue listening for anything coming in on the client socket. The worker
thread that is tasked with handling the request will first (on a high level) call
parse_request to transform the buffer char array (read directly from local socket)
into a completed request_data struct. Once this is complete it will call
fulfill_request to manage sending forwarding the request to the server, waiting
for a response, and finally forwarding the response to the client.

While the process is running the command line window will be constantly updated
with information pertinent to the status of the execution. For each completed 
request, a line will be printed with data such as the time taken, the request
URL, the response size, etc. If there is no request currently being handled, the
CLI will show an animated line traversing the current line to represent the fact
that it is idle at this moment.

The following is a simplified trace of a standard use case:

--> main() [received a request]
    --> parse_request()
        --> parse_size()
        --> get_host_port_and_path()
    --> fulfill_request()
        --> forward_request_to_remote()
        --> receive_response_from_remote()
        --> forward_response_to_client()
 */ 

#include "csapp.h"
#include "time.h"

#define BUFFER_PAGE_WIDTH 4096  // maximum size of each read/write buffer
#define BUFFER_PAGE_COUNT 12000 // maximum number of dynamically size buffers (per response)
#define STATUS_BUFFER_COUNT 100 // maximum number of prior status updates held in status_buffer struct
#define MAXIMUM_THREAD_COUNT 100 // does not control thread count, just size of thread id buffer in status_buffer struct

// handle separate naming for Windows, Apple, and Unix machines
#ifdef _WIN32
    int MAP_A = MAP_ANONYMOUS;
#elif defined __APPLE__
    int MAP_A  = MAP_ANON;
#else
    int MAP_A = MAP_ANONYMOUS;
#endif

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////

FILE *RESPONSE_LOG; // file to log all responses from remote clients
FILE *REQUEST_LOG; // file to log all requests sent to remote clients
FILE *STATUS_LOG; // file to write status update to (also sent to current CLI line)
FILE *DEBUG_LOG; // file to save debugging info to, can be accessed from any function in this file 
FILE *PROXY_LOG; // file to log each request (as per instructions)

// list of domains I have added to the list to block
const int num_blocked_domains = 0; // number of items in list below
const char *blocked_domains[1] = {""}; // empty (not blocking anything)

// character sequence used to wipe the current line of the terminal
const char *UI_WIPE[1] = {"                                                                                                       "};

char    *CURRENT_STATUS; // to hold what we are currently processing
int     PROCESSING_REQUEST = 0; // set to 1 while a request is being processed to prevent concurrency problems
int     *ALREADY_LOGGING; // set to 1 while a request is being written to the proxy.log file
int     *ALREADY_LOGGING_DEBUG_INFO; // set to 1 while debuggin data is being written
int     *ENABLE_TIME_SPINNER; // if 0 then show waiting line, 1 show loading spinner, 2 show nothing
int     *REFRESHING_UI_HEADER; // 0 if not refreshing table header right now, 1 otherwise

int     *USER_EXIT;  // set to 1 if user hits Ctrl+C and 'Y', causes all threads to exit
int     *USER_PAUSE; // set to 1 if the user hits Ctrl+C, pauses all threads but the top parent

int     CUR_SERVER_SOCKET; // thread-specific global variable (for handling thread exiting) 
int     CUR_CLIENT_SOCKET; // thread-specific global variable (for handling thread exiting)

// Used to control what status update is shown on the CLI, needed because sometimes
// threads that were already done would have sent their updates later on than other
// worker threads that were still processing a certain aspect and that message isn't
// being shown even though that is the thread that is still running.
typedef struct status_buffer
{
    char prior_status[STATUS_BUFFER_COUNT][300]; // Holds the last STATUS_BUFFER_COUNT status updates that sent by all threads together
    int prior_status_thread[STATUS_BUFFER_COUNT]; // single entry for each item in prior_status, associated thread

    int open_threads[MAXIMUM_THREAD_COUNT]; // Has the thread_id of every running worker thread

} status_buffer;

struct status_buffer *stat_buf; // Thread-global variable
int *stat_buf_lock; // 

// Data structure to hold all essential information pertaining to a request. This is the
// main vehicle used in the two main processes (parse_request and fulfill_request) to hold
// and transfer data among functions.
typedef struct request_data
{
    char request_buffer[BUFFER_PAGE_WIDTH]; // unparsed, entire contents of client request
    int request_buffer_size; // size of the request_buffer, as reported by socket read
    char req_after_host[BUFFER_PAGE_WIDTH]; // portion of request after second line (host specification line)
    int req_after_host_size; // size of the above req_after_host array
    int specified_req_size; // the size the client says the request content is (if one)

    // certain portions of the request, parsed out to help with forwarding
    char req_command[10]; // GET, POST, etc.
    char req_host_domain[100]; // www.yahoo.com, etc.
    char req_host_long[1024]; // http://www.yahoo.com/, etc.
    char req_host_port[10]; // Host port, if specified
    int  req_host_port_num; // Host port, if specified
    char req_protocol[100]; // HTTP/1.1, etc.
    char req_host_path[512]; // /text/index.html from http://www.cnn.com:80/test/index.html

    char *full_response_dynamic[BUFFER_PAGE_COUNT]; // buffered server response
    int resp_length[BUFFER_PAGE_COUNT]; // hold size of each element of response buffer
    int num_lines_response; // to hold the number of response lines

    char response_code[100]; // to hold the return code
    char sent_time[100]; // time when the request was sent to remote server
    char server_responded_time[100]; // time when the server responded
    int specified_resp_size; // the size the server says the response will be (if one)

    int request_blocked; // 1 if blocked, 0 otherwise
    int thread_id; // ID of the thread processing this request

} request_data;

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
// see comments above function definitions for more detail

// Request Handling...
char*   parse_request(struct request_data *req_data, char *buffer);
int     fulfill_request(struct request_data *req_data, int client_socket);
int     forward_request_to_remote(struct request_data *req_data, int remote_socket);
int     receive_response_from_remote(struct request_data *req_data, int remote_socket);
int     forward_response_to_client(struct request_data *req_data, int client_socket);
void    free_request_data(struct request_data *req_data);

// Logging...
void    log_request( struct sockaddr_in *sockaddr,  char *uri,  int size);
void    log_information(const char *buffer, const struct request_data *req_data, const int thread_id);
void    format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);

// HTTP Parsing Utilities...
int     parse_listening_port_num(char ** argv);
int     parse_size(const char *buffer);
void    parse_response_code(const char *buffer, struct request_data *req_data);
void    get_host_path_and_port(struct request_data *req_data);

// Command-Line Interface management...
void    init_ui_header(const int listening_port, const int listening_socket);
void    refresh_ui_header();
void    handle_time_spinner();
void    update_cli(struct request_data *req_data, int thread_index, int threads_open, int resp_size, int request_size, int total_time);
void    set_current_status(const char *input_str);
void    set_current_status_id(const char *input_str, const int thread_id);
void    init_stat_buf();
void    add_thread_id_to_stat_buf(int thread_id);
void    remove_thread_id_from_stat_buf(int thread_id);
char*   stat_buf_top();
void    add_status_to_stat_buf(char *status, int thread_id);

// Misc...
void    init_global_variables();
char*   get_time_string();
int     elapsed_time(time_t t1, time_t t2);
char*   get_spacer_string(const int size);
char*   shorten_string(const char *input_str, const int new_size);
int     is_blocked_domain(struct request_data *req_data);
void    strip_trailing_char(char *source, const char to_remove);

// Threading...
void    on_thread_exit(int signal);
void    on_user_command(int signal);
void    on_child_exit(int signal);
void    ignore_signal(int signal);
void    synchronize_thread();

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////

// Called when the program is run from the command line. Initializes
// global variables, opens connection to provided port and listens until
// a request is sent. Once a request is received a new thread is created
// using Fork() to process the request while the parent thread returns to
// keep listening to the port for another request.
int main(int argc, char **argv)
{
    /* Check arguments */
    if (argc != 2) 
    {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

    init_global_variables();    // initialize cross-thread global variables

    if (Fork() == 0){  handle_time_spinner();  } // start the time spinner thread

    int listening_port      = parse_listening_port_num(argv); // parse the listening port number
    int listening_socket    = Open_listenfd(listening_port); // initialize the listening socket

    if (listening_socket<0)
    {
        fprintf(stderr,"Listening port is already in use");
        exit(0);
    }

    init_ui_header(listening_port,listening_socket); // draw out the table header for the CLI
    *ENABLE_TIME_SPINNER = 0; // start the waiting line
    
    // initialize variables used across worker threads
    int *thread_ct  = mmap(NULL,sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_A, -1, 0);
    *thread_ct      = 0; // unique id for each thread (thread index)

    int *threads_open   = mmap(NULL,sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_A, -1, 0);
    *threads_open       = -1; // number of working theads executing

    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);

    signal(SIGTERM, on_thread_exit); // call 'on_thread_exit' when a thread receives a signal to terminate
    signal(SIGINT, on_user_command); // call 'on_user_command' when a user hits Ctrl+C in terminal
    signal(SIGCHLD, on_child_exit); // call 'on_child_exit' if child process is terminated

    while (1)
    {
        // wait for a connection from a client at listening socket
        int client_socket   = Accept(listening_socket, (struct sockaddr*)&client_addr, &addrlen);
        int new_pid         = Fork(); // spawn child thread to handle request

        switch (new_pid)
        {
            case 0: // new child
                set_current_status(""); // denote that we have started a new request in status.log file

                // prevent this thread from reacting to 'Ctrl+C' other than setting the USER_PAUSE variable to 1
                if ( signal(SIGINT,SIG_IGN)!= SIG_IGN){  signal(SIGINT,ignore_signal);  }
                
                CUR_CLIENT_SOCKET = client_socket; // if thread is killed, we want to be able to close the socket
                CUR_SERVER_SOCKET = -1; // default, will be set in fulfill_request

                close(listening_socket);
                request_data req_data; // create new request_data struct to hold parsed data

                req_data.request_blocked    = 0; // initialize int to denote that request has not yet been blocked
                req_data.thread_id          = *thread_ct; // save the index of this thread to req_data
                *thread_ct                  = *thread_ct + 1; // increment the thread_ct integer
                *threads_open               = *threads_open + 1; // increment the threads_open integer
                *ENABLE_TIME_SPINNER        = 1; // tell handle_time_spinner to start showing status updates

                add_thread_id_to_stat_buf(req_data.thread_id); // add this thread index to the stat_buf

                close(listening_socket); // prevent this thread from reacting to browser requests
                close(listening_port); // prevent this thread from reacting to browser requests

                set_current_status_id("Reading socket",req_data.thread_id);
                char buffer[BUFFER_PAGE_WIDTH]; // to read the socket data into
                
                int request_size        = Read(client_socket,buffer,BUFFER_PAGE_WIDTH-1); // read socket into buffer string
                buffer[request_size]    = 0; // zero-terminate string

                memcpy(req_data.request_buffer,buffer,request_size); // copy the buffer into req_data
                req_data.request_buffer_size = request_size; // save the size of the socket read

                char temp_buffer3[50];
                //char *temp_buffer3 = malloc(sizeof(char)*50);
                sprintf(temp_buffer3,"Request size = %d",request_size);
                set_current_status_id(temp_buffer3,req_data.thread_id);

                if (request_size<0){  printf("> Error reading from socket \n");  }
                
                fprintf(REQUEST_LOG,"\nREQUEST (thread_index=%d):\n%s\n--------------------\n",req_data.thread_id,buffer);
                fflush(REQUEST_LOG);

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
                else{  resp_size = fulfill_request(&req_data, client_socket);  }

                // record the end time of the request
                time_t t2;
                time(&t2);
                int request_time = elapsed_time(t1,t2);

                // update the command line interface UI
                update_cli(&req_data,req_data.thread_id,*threads_open,resp_size,request_size,request_time);
                fflush(stdout);

                // To prevent the handle_time_spinner from selecting a status update for a request
                // that has already been processed we need to mark that this thread id is finished...
                remove_thread_id_from_stat_buf(req_data.thread_id); // remove this thread_id from the stat_buf

                // log the information so long as the request wasn't blocked
                if ( req_data.request_blocked==0 )
                {
                    // log the request as per Sakai pdf
                    log_request((struct sockaddr_in*)&client_addr,uri,resp_size);
                    // print out additional information (debugging)
                    log_information(buffer,&req_data,req_data.thread_id);
                }
                
                *threads_open = *threads_open - 1;
                close(client_socket); // close the client socket

                // tell the handle_time_spinner thread to show the waiting animation
                if ( *threads_open==-1) {  *ENABLE_TIME_SPINNER = 0;  }

                if ( (req_data.thread_id % 50 == 0) && (req_data.thread_id!=0) ){  refresh_ui_header();  } // refresh the UI header table

                free_request_data(&req_data); // free the memory allocated to req_data
                exit(0); // close this thread (opened on Fork())

            case -1: // error when creating new thread
                printf("\nERROR: Could not spawn new thread (%d)\n",new_pid);
                close(client_socket);
                return -1;

            default: // this is the parent thread
                close(client_socket); // close the socket we left to the child
        }
        // clear the command line if not loading a request
        if ( *threads_open==-1)
        {  
            *ENABLE_TIME_SPINNER = 0; 
            printf("%s\r",UI_WIPE[0]);
            fflush(stdout);
        } 
    }
    Fclose(PROXY_LOG);
    exit(0);
}

// Called from the main function directly after a request has been read from the
// client. At this point the buffer contains the full request and the main job of
// this function is to parse through said request and fill in certain items of the
// req_data struct such as req_command (i.e. POST, GET), req_host_long (the URI),
// req_protocol (i.e. http/1.1), and req_host_domain. Returns the URL of the request.
char* parse_request(struct request_data *req_data, char *buffer)
{
    set_current_status_id("Parsing request",req_data->thread_id); // update CLI and write to status.log

    int content_size = parse_size(buffer); // check if 'Content-Length' is in the request
    if ( content_size!=-1 ) // if a size is specified
    {
        req_data->specified_req_size = content_size;
        char *temp_buffer = malloc(sizeof(char)*50); 
        sprintf(temp_buffer,"Specified request size = %d",req_data->specified_req_size);
        set_current_status_id(temp_buffer,req_data->thread_id); // update CLI and write to status.log
        free(temp_buffer);
    }
    // if there is no 'Content-Length' specified in request
    else{   set_current_status_id("No request size specified",req_data->thread_id);  }

    // copy over the request to a new char array so we can use strtok_r without
    // effecting the copy in the req_data struct
    char buffer_copy[req_data->request_buffer_size];
    memcpy(buffer_copy,req_data->request_buffer,req_data->request_buffer_size);

    // split the buffer_copy into lines (split on '\n')
    char *end_str;
    char *line = strtok_r(buffer_copy,"\n",&end_str);

    int line_index = 0; // to hold the index of the current line in overall request

    // each iteration of while loop looks at a single line of the request (held in buffer_copy),
    // for the first two lines we will parse and save the data we need whereas for everything
    // past that point we will just save into the req_data req_after_host buffer
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

            // now we need to get the size of the request prior to the third line (the size of the first 2 lines)
            char *first_line_CRENDL = strstr(req_data->request_buffer,"\r\n"); 
            char *second_line_CRENDL = strstr(first_line_CRENDL+4,"\r\n"); // end of second line

            int start_snip = (int)(second_line_CRENDL - req_data->request_buffer)+2; // end of second line
            int snip_length = (int)(req_data->request_buffer_size - start_snip); // until end of body of request

            // copy everything after the first two lines into the req_after_host member of req_data
            memcpy(req_data->req_after_host,&req_data->request_buffer[start_snip],snip_length);
            req_data->req_after_host_size = snip_length; // set the correct size of everything after the 1st 2 lines
            break;
        }
        line = strtok_r(NULL,"\n",&end_str); // get next line of request
        line_index++;
    }

    strip_trailing_char(req_data->req_host_domain,'\r'); // remove '\r' from domain name
    strip_trailing_char(req_data->req_protocol,'\r'); // remove '\r' from the protocol type
    get_host_path_and_port(req_data); // get the path specified in the URL (after the domain name) & the port (if one)

    set_current_status_id("Finished parsing request",req_data->thread_id);
    return req_data->req_host_long; // return the URI (long URL)
}

// Called from the main function after the request has been fully parsed using
// parse_request. Calls forward_request_to_remote to handle the forwarding of the 
// request to the server, after which it will call receive_response_from_remote to 
// handle the parsing of the server response and finally forward_response_to_client 
// to forward on this response to the local client (web browser).
int fulfill_request(struct request_data *req_data, int client_socket)
{
    set_current_status_id("Connecting to remote socket",req_data->thread_id); // update CLI and status.log file
    int remote_socket = Open_clientfd(req_data->req_host_domain,req_data->req_host_port_num); // try to open socket on remote machine
    CUR_SERVER_SOCKET = remote_socket; // save this as a global variable (in case of premature thread exiting, see on_thread_exit)

    int send_success = forward_request_to_remote(req_data,remote_socket); // send the request to remote machine

    if (send_success == -1) // if we were not able to send the request
    {
        set_current_status_id("Could not connect to remote",req_data->thread_id);
        Sleep(1); // allow time for writing to log
        close(client_socket); // close connection to client
        close(remote_socket); // close attempted connection to remote
        exit(0); // exit the thread
    }

    int response_size = receive_response_from_remote(req_data,remote_socket); // get the response from the remote machine
    set_current_status_id("Forwarding response to client",req_data->thread_id); // update CLI and status.log file

    int forwarding_success = forward_response_to_client(req_data,client_socket); // forward to response to client
    if (forwarding_success == -1) // if we were not able to forward the response
    {
        set_current_status_id("Could not forward response to client",req_data->thread_id);
        Sleep(1); // allow time for writing to log
        close(client_socket); // close connection to client
        close(remote_socket); // close attempted connection to remote
        exit(0); // exit the thread 
    }
    return response_size; // return the size of response
}

// Called from fulfill_request after a connection has been established with
// the remote socket. Forwards the parsed request on to the remote socket.
// Returns -1 if it was not possible and 1 if success.
int forward_request_to_remote(struct request_data *req_data, int remote_socket)
{
    strcpy(req_data->sent_time, get_time_string()); // record the time we started to send data to remote
    set_current_status_id("Forwarding request",req_data->thread_id); // update CLI and status.log file

    if (remote_socket<0) // check to ensure the remote socket could be established...
    {
        printf("\n> DNS could not connect to remote %s\n",req_data->req_host_long);
        return -1; // return error code
    }

    char buffer[BUFFER_PAGE_WIDTH]; // to hold the information we will be sending to remote socket
    sprintf(buffer,"%s %s %s\r\n",req_data->req_command,req_data->req_host_path,req_data->req_protocol);
    int m = send(remote_socket,buffer,strlen(buffer),0); // send the first line to remote socket
    if (m<0){  printf("\nERROR: could not write to remote socket\n");  } // check for failure

    sprintf(buffer, "Host: %s:%s\r\n", req_data->req_host_domain,req_data->req_host_port); 
    m = send(remote_socket,buffer,strlen(buffer),0); // send the second line to remote socket
    if (m<0){  printf("\nERROR: could not write to remote socket\n");  } // check for failure

    m = send(remote_socket, req_data->req_after_host, req_data->req_after_host_size, 0); // send the rest of request to remote socket
    if (m<0){  printf("\nERROR: could not write to remote socket\n");  } // check for failure
    
    return 1; // denote success
}

// Called from fulfill_request after the request has been forwarded to the
// remote socket. Reads in the response from the server and returns the total
// size of the message received.
int receive_response_from_remote(struct request_data *req_data, int remote_socket)
{
    char buffer[BUFFER_PAGE_WIDTH];

    ssize_t n;
    int total_size = 0; // total size of the message received 
    req_data->num_lines_response = 0; // initialize number of lines in response
    strcpy(req_data->response_code,"NONE");

    fprintf(RESPONSE_LOG,"\n\n--------------------------------------------------\n\n");
    fflush(RESPONSE_LOG);

    int past_header = 0; // set to 1 once we are on a chunk that is past the header section of response

    set_current_status_id("Waiting for response",req_data->thread_id); // update UI
    buffer[1] = 0; // zero-terminate buffer before reading
    req_data->specified_resp_size = -1;

    int actual_total_size = -1;

    // Continue iterating until we have read all of the data in the remote socket,
    // for the first read we will check the actual buffer to see if the size of the
    // response is specified, if so we will break out of this loop prematurely if we
    // have read up to the size specified. If there is no specified size, we will break
    // the loop prematurely if we reach a '\r\n\r\n' string.
    while ((n = recv(remote_socket,buffer,BUFFER_PAGE_WIDTH-1,0)) > 0)
    {
        buffer[n] = 0; // zero-terminate buffer

        fprintf(RESPONSE_LOG,"%s",buffer); // write the response to response.log file
        fflush(RESPONSE_LOG);

        // if this is the first read (containing first line) parse out
        // the response code from the remote server
        if ( total_size==0 && sizeof(buffer)!=0 )
        {  
            set_current_status_id("Got first response",req_data->thread_id);
            parse_response_code(buffer,req_data);

            int response_size = parse_size(buffer);
            if ( response_size != -1)
            {
                req_data->specified_resp_size = response_size;
                char *temp_buffer = malloc(sizeof(char)*50);
                sprintf(temp_buffer,"Found response size (%d)",req_data->specified_resp_size);
                set_current_status_id(temp_buffer,req_data->thread_id);
                free(temp_buffer);
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
                total_size = n - delim_index - 4;
            }
        }

        // if we have parsed out a specified response size
        if ( req_data->specified_resp_size != -1)
        {
            // if we have read at least as much as the specified resp size
            if ( total_size >= req_data->specified_resp_size )
            {
                actual_total_size = total_size-n; // adjust total_size value
                n = (req_data->specified_resp_size - (total_size-n)); // trim current buffer read
                actual_total_size = actual_total_size+n; // set correct total size
            }
        }

        // if we have not parsed out a specified size
        else
        {
            char *delim_start = strstr(buffer,"\r\n\r\n");

            // check if we are past the header section, if so, and there is no
            // specified size, we are done reading the response
            if ( delim_start != NULL )
            {
                int delim_index = (int)(delim_start - buffer)+4;
                actual_total_size = total_size-n+delim_index;
                n = delim_index;
            }
        }

        // copy the data we just gathered into the req_data struct
        req_data->full_response_dynamic[req_data->num_lines_response] = malloc(sizeof(char)*(int)(n+1));
        memcpy(req_data->full_response_dynamic[req_data->num_lines_response],buffer,n);
        req_data->resp_length[req_data->num_lines_response] = (int)n; // record the length of buffered response
        req_data->num_lines_response++; // increment number of response lines

        // check if we have found the entire message
        if (req_data->specified_resp_size!=-1)
        {
            if (total_size>=(req_data->specified_resp_size))
            {
                char *temp_buffer4 = malloc(sizeof(char)*100);
                sprintf(temp_buffer4,"Reached end of message (size=%d)",actual_total_size);
                set_current_status_id(temp_buffer4,req_data->thread_id);
                free(temp_buffer4); // free memory used in buffer
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

    fprintf(RESPONSE_LOG,"\n\n--------------------------------------------------\n\n");
    fflush(RESPONSE_LOG);

    close(remote_socket); // close the connection to remote machine
    if (actual_total_size!=-1){  return actual_total_size;  }
    return total_size;
}

// Called from fulfill request after the response has been received from the
// remote machine, forwards the response on to the local client. Returns -1 if
// failure, 1 if success.
int forward_response_to_client(struct request_data *req_data, int client_socket)
{
    for (int i=0; i<req_data->num_lines_response; i++)
    {
        int m=0;
        if ( (m=send(client_socket,req_data->full_response_dynamic[i],req_data->resp_length[i],0))<0 )
        {
            printf("\nNot allowed to write to client_socket, m=%d\n",m);
            close(client_socket);
            break;
        }
    }
    set_current_status_id("Finished forwarding response",req_data->thread_id); // update CLI and status.log file
    strcpy(req_data->server_responded_time, get_time_string()); // record the time we finished sending
    close(client_socket);
    return 1;
}

// Called from main after a request_data struct is done finished being used (after the
// response from the server has already been passed on to the client). Frees all of the
// memory allocated to the struct via malloc.
void free_request_data(struct request_data *req_data)
{
    // clear the response data (if any)
    for (int i=0; i<req_data->num_lines_response; i++)
    {
        free(req_data->full_response_dynamic[i]);
    }
}

// Called from the main function (same time frame as log_information), writes out
// the request in the format specified in assignment (making use of format_log_entry)
// to the proxy_log global variable (proxy.log file).
void log_request( struct sockaddr_in *sockaddr,  char *uri,  int size)
{
    time_t refresh_seconds = 0; // refresh the waiter/timer after this many seconds
    typedef struct timespec // struct used to specify how much time to use in nanosleep
    {
        time_t tv_sec;
        long tv_nsec;   
    } timespec;
    timespec t;
    t.tv_sec = refresh_seconds;
    t.tv_nsec = (long)10000.0; // once per second

    // busy wait until we can get a handle on the proxy.log file
    while (*ALREADY_LOGGING==1){  nanosleep(&t,NULL);  }
    *ALREADY_LOGGING = 1; // tell all other threads to wait
    char logstring[1024]; // to hold output of format_log_entry
    format_log_entry((char *)&logstring,sockaddr,uri,size); 
    fprintf(PROXY_LOG,"%s",logstring); // print out formatted string
    fprintf(PROXY_LOG,"\n"); // new line
    fflush(PROXY_LOG);
    *ALREADY_LOGGING = 0; // tell all other threads it's okay to write to proxy.log
}

// Called from the main function after a request has been fully processed, writes
// out request, processed request (request sent to server), and server response to
// the global debug_log (debug.log file).
void log_information(const char *buffer,const struct request_data *req_data, const int thread_id)
{
    if ( req_data->request_blocked==1 ){  return;  } // skip logging if this request was blocked

    time_t refresh_seconds = 0; // refresh the waiter/timer after this many seconds
    typedef struct timespec // struct used to specify how much time to use in nanosleep
    {
        time_t tv_sec;
        long tv_nsec;   
    } timespec;
    timespec t;
    t.tv_sec = refresh_seconds;
    t.tv_nsec = (long)10000.0; // once per second

    while (*ALREADY_LOGGING_DEBUG_INFO==1){  nanosleep(&t,NULL);  }

    *ALREADY_LOGGING_DEBUG_INFO = 1;
    fprintf(DEBUG_LOG,"\n\n=============================================================\n");
    fprintf(DEBUG_LOG,"====================REQUEST (thread %d)====================\n",thread_id);
    fprintf(DEBUG_LOG,"%s",buffer);
    fprintf(DEBUG_LOG,"\n==================SENT REQUEST [%s]===================\n",req_data->sent_time);
    if (strcmp(req_data->req_command,"GET")==0) {  fprintf(DEBUG_LOG,"GET %s %s\r\n",req_data->req_host_path,req_data->req_protocol);  }
    if (strcmp(req_data->req_command,"POST")==0){  fprintf(DEBUG_LOG,"POST %s %s\r\n",req_data->req_host_path,req_data->req_protocol);  }
    fprintf(DEBUG_LOG,"Host: %s:%s\r\n",req_data->req_host_domain,req_data->req_host_port);
    fprintf(DEBUG_LOG,"%s",req_data->req_after_host);
    fprintf(DEBUG_LOG,"\n===================RESPONSE [%s]======================\n",req_data->server_responded_time);
    for(int i=0; i<req_data->num_lines_response; i++)
    {   
        fprintf(DEBUG_LOG,"%s\n",req_data->full_response_dynamic[i]);
    }
    fprintf(DEBUG_LOG,"\n=============================================================\n");
    fflush(DEBUG_LOG);
    *ALREADY_LOGGING_DEBUG_INFO = 0;
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

// Called from the main function at program launch (so long as the correct) number
// of arguments are provided, returns the listening port in integer format.
int parse_listening_port_num(char ** argv)
{
    char * port_str = argv[1];
    int port_num = atoi(port_str);
    return port_num;
}

// Checks the buffer array for the string 'Content-Length' which is used
// in http to specify the size of the body of the response. If it is able
// to find said attribute it will return that value in integer format. If
// it is not able to find 'Content-Length' than -1 will be returned by default.
// Called by both parse_request and receive_response_from_remote.
int parse_size(const char *buffer)
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
            return -1;
        }

        if ( i>=strlen(buffer) ) // if we have reached the end of buffer string
        {
            //printf("\nERROR: parse_request_size(), buffer=%s\ncur_line_buffer=%s\n",buffer,cur_line_buffer);
            return -1;
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
                            return atoi(size_buffer); // return the size
                        }
                        printf("\rERROR: parse_size(), cur_line_buffer=%s\n",cur_line_buffer);
                        return -1;
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
                return -1; // return false because we have reached the end of header
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

// Called from receive_response_from_remote, the provided buffer is the first buffer
// read in from the remote-facing socket (first BUFFER_PAGE_WIDTH data read of server response).
// Parses through the response until it finds the response code (i.e. 200 OK) and sets the
// response_code element of the input req_data struct equal to said code. If no code is found
// nothing will be written to req_data but this is handled by the caller by setting it to
// "NONE" before calling to allow for difference checking to see if there exists a response code.
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

// Called from parse_request, parses through req_data->req_host_long to fill in several more
// items in the req_data struct including req_host_path and req_host_port_num.
void get_host_path_and_port(struct request_data *req_data)
{

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
        set_current_status_id("No request port specified",req_data->thread_id); // update CLI and status.log
    }
    // convert parsed port to integer and set appropriate member in req_data
    else
    {
        int port_num = atoi(req_data->req_host_port);
        req_data->req_host_port_num = port_num;

        char temp_buffer6[100];
        sprintf(temp_buffer6,"Specified request port = %d",port_num);
        set_current_status_id(temp_buffer6,req_data->thread_id);
    }
}

// Called from the main function at start of program, prints out the header for the table
// used to align the items which are printed out in update_cli.
void init_ui_header(const int listening_port, const int listening_socket)
{
    printf("\n\n=====================================================================================================\n");
    printf("=====================================================================================================\n");
    printf("> Listening: {PORT: %d SOCKET: %d}\n",listening_port,listening_socket);
    printf("=====================================================================================================\n");
    printf("[   Thread Info    ][           Client Request Info            ][    Response Info    ][ Total Time ]\n");
    printf("_____________________________________________________________________________________________________\n");
    fflush(stdout);
}

// Called from the main function after a certain number of requests have been processed.
// Function re-draws the UI header that was initially drawn with init_ui_header.
void refresh_ui_header()
{
    if (*REFRESHING_UI_HEADER){  return;  } // if another thread already doing it, skip

    *REFRESHING_UI_HEADER = 1;
    printf("%s\r",UI_WIPE[0]); // wipe the current line
    printf("=====================================================================================================\n");
    printf("[   Thread Info    ][           Client Request Info            ][    Response Info    ][ Total Time ]\n");
    printf("_____________________________________________________________________________________________________\n");
    fflush(stdout);
    *REFRESHING_UI_HEADER = 0;
}

// Responsible for spinning the loading spinner and pushing updates to CLI,
// this function is called by the main process and is run in its own thread.
// While the other worker threads are processing requests or waiting idle, 
// this thread will continue to update the console window with the current
// status. If there are no requests being processed it will output an animated
// line which travels across the bottom line of the console window. If there
// is a request being processed, it will output the current status and a 
// spinning pinwheel to signify that it is loading.
void handle_time_spinner()
{
    // prevent this thread from reacting to SIGINT requests, other than setting
    // USER_PAUSE equal to true to prevent other threads from continuing to process
    if (signal(SIGINT,SIG_IGN) != SIG_IGN){  signal(SIGINT,ignore_signal);  }

    int time_spin_index = 0; // the state of the time spinner (range [0,3])
    int waiting_spin_index = 0; // the state of the waiting spinner (range [-bar_distance,max_distance])
    
    time_t refresh_seconds = 0; // refresh the waiter/timer after this many seconds
    
    typedef struct timespec // struct used to specify how much time to use in nanosleep
    {
        time_t tv_sec;
        long tv_nsec;   
    } timespec;

    timespec t;
    t.tv_sec = refresh_seconds;
    t.tv_nsec = (long)100000000.0; // once per second
    //t.tv_nsec = (long)20000000.0; // once per second

    int max_distance = 101; // the width to run across for the waiting line 
    int bar_distance = 3; // the length of the waiting line (5 default)
    char left_wall[1] = ""; // character to represent left bound of waiting line
    char right_wall[1] = ""; // character to represent right bound of waiting line
    char bar_reg_char[1] = "-"; // character used in line ('-----')
    char empty_reg_char[1] = " "; // character used if line not present

    while(1)
    {
        synchronize_thread();  // check for USER_PAUSE and USER_EXIT
        nanosleep(&t,NULL); // sleep for a second
        synchronize_thread();

        printf("%s\r",UI_WIPE[0]); // wipe the display
        fflush(stdout);

        // if a request is being processed (at any stage) the *ENABLE_TIME_SPINNER will be set to 1
        // by the main event loop of the worker thread, if this is 1 then we enter the first case below
        // and output a spinning widget to show that loading is taking place, the CURRENT_STATUS will also
        // be appended after the spinner which will include some information such as the ID of the
        // worker thread and the event taking place
        if (*ENABLE_TIME_SPINNER==1)
        {
            char temp_buffer5[300];
            temp_buffer5[1] = 0;

            char *prior_status_update = stat_buf_top(); // get a status update from an open thread

            // depending in the current state of time_spin_index we will output a different character
            if (time_spin_index==0){  sprintf(temp_buffer5,"... |   %s ",prior_status_update);  }
            if (time_spin_index==1){  sprintf(temp_buffer5,"... /   %s ",prior_status_update);  }
            if (time_spin_index==2){  sprintf(temp_buffer5,"... -   %s ",prior_status_update);  }
            if (time_spin_index==3){  sprintf(temp_buffer5,"... \\   %s ",prior_status_update); }
            printf("%s\r",temp_buffer5);
            fflush(stdout);
            //free(temp_buffer5);

            time_spin_index++; // increment time_spin_index
            if ( time_spin_index>=4 ){  time_spin_index=0;  }
            continue;
        }

        if (*ENABLE_TIME_SPINNER==0) // show line that floats across screen to denote waiting
        {
            char *temp_buffer5 = malloc(sizeof(char)*300);
            temp_buffer5[1] = 0;
            int temp_buffer_index = 0;

            for (int i=0; i<max_distance; i++)
            {
                if (i>=(waiting_spin_index) && i<=(waiting_spin_index+(bar_distance)*(0.1*waiting_spin_index)))
                {
                    temp_buffer5[temp_buffer_index] = bar_reg_char[0]; // no line at this index
                    temp_buffer_index++;
                    temp_buffer5[temp_buffer_index] = 0;
                }
                else
                {
                    temp_buffer5[temp_buffer_index] = empty_reg_char[0]; // line at this index
                    temp_buffer_index++;
                    temp_buffer5[temp_buffer_index] = 0;
                }
            }
            printf("%s%s%s\r",left_wall,temp_buffer5,right_wall);
            fflush(stdout);
            //free(temp_buffer5);

            waiting_spin_index++; // increment waiting spin index
            if ( waiting_spin_index==max_distance ){  waiting_spin_index=-1*bar_distance;  }
            continue;
        }
        synchronize_thread(); // check for USER_PAUSE and USER_EXIT
    }
}

// Called from the main function after a request has been completed (i.e.
// the request has been read, sent to server, server responded, response sent
// to client). Responsible for clearing anything being printed by handle_time_spinner
// and writing out the details of the request-response communication. Most of the
// logic in this function is simply to align the items so they fit into the command
// line window table (set up by init_ui_header).
void update_cli(struct request_data *req_data, int thread_index, int threads_open, int resp_size, int request_size, int total_time)
{
    synchronize_thread(); // check for USER_PAUSE and USER_EXIT global variables

    // initialize some variables to help with printing 
    printf("%s\r",UI_WIPE[0]);
    fflush(stdout);
    int max_host_length = 30;
    char *req_host_long_shortened = shorten_string(req_data->req_host_long, max_host_length);
    char *spacer = get_spacer_string(max_host_length-strlen(req_host_long_shortened));

    char spacer_after_response_info[10];
    if (strlen(req_data->response_code)<=7)
    {
        sprintf(spacer_after_response_info,"\t\t");
    }
    else
    {
        sprintf(spacer_after_response_info,"\t");
    }

    char size_based_spacer[10];
    if ( resp_size<100 )
    {
        sprintf(size_based_spacer,"    ");
    }
    else
    {
        sprintf(size_based_spacer," ");
    }

    if ( strstr(req_data->req_command,"POST")!=NULL )
    {
        printf("%d\t(%d workers) > %s %s %s %d B > [%s,%d B] %s%s %d s\n",thread_index,threads_open,req_data->req_command,req_host_long_shortened,spacer,request_size,req_data->response_code,resp_size,size_based_spacer,spacer_after_response_info,total_time);
    }
    else
    {
        printf("%d\t(%d workers) > %s  %s %s %d B > [%s,%d B] %s%s %d s\n",thread_index,threads_open,req_data->req_command,req_host_long_shortened,spacer,request_size,req_data->response_code,resp_size,size_based_spacer,spacer_after_response_info,total_time);
    }
    fflush(stdout);
}

// Same as set_current_status_id but does not require a thread id number.
void set_current_status(const char *input_str)
{
    char *temp = malloc(sizeof(char)*300);
    sprintf(temp,"%s - %s",get_time_string(),input_str);
    strcpy(CURRENT_STATUS,temp);
    fprintf(STATUS_LOG,"%s\n",temp);
    fflush(STATUS_LOG);
}

// Called when a function wants to update the command line with a status update,
// the actual update won't be processed until the handle_time_spinner function 
// wakes up but the status update will be written to status.log immediately.
void set_current_status_id(const char *input_str, const int thread_id)
{
    char *temp = malloc(sizeof(char)*300);
    sprintf(temp,"%s - (id=%d) - %s",get_time_string(),thread_id,input_str);
    strcpy(CURRENT_STATUS,temp);
    add_status_to_stat_buf(temp,thread_id); // add this status update to the stat_buf struct
    fprintf(STATUS_LOG,"%s\n",temp);
    fflush(STATUS_LOG);
}

// Initializes the stat_buf thread-global struct
void init_stat_buf()
{
    typedef struct timespec // struct used to specify how much time to use in nanosleep
    {
        time_t tv_sec;
        long tv_nsec;   
    } timespec;
    timespec t;
    t.tv_sec = 0;
    t.tv_nsec = (long)1000.0; // once per second
    while ( *stat_buf_lock ){  nanosleep(&t,NULL);  }

    *stat_buf_lock = 1;
    // initialize all thread index values to -1 to denote that they are not yet set
    for (int i=0; i<STATUS_BUFFER_COUNT; i++)
    {
        stat_buf->prior_status_thread[i] = -1;
    }
    *stat_buf_lock = 0;
}

// Add a thread id to the stat_buf struct
void add_thread_id_to_stat_buf(int thread_id)
{
    typedef struct timespec // struct used to specify how much time to use in nanosleep
    {
        time_t tv_sec;
        long tv_nsec;   
    } timespec;
    timespec t;
    t.tv_sec = 0;
    t.tv_nsec = (long)1000.0; // once per second
    while ( *stat_buf_lock ){  nanosleep(&t,NULL);  }

    *stat_buf_lock = 1;
    for (int i=0; i<STATUS_BUFFER_COUNT; i++)
    {
        if ( stat_buf->prior_status_thread[i]==-1 )
        {  
            stat_buf->prior_status_thread[i] = thread_id;
            break;
        }
    }
    *stat_buf_lock = 0;
}

// Remove a thread id from stat_buf struct
void remove_thread_id_from_stat_buf(int thread_id)
{    
    typedef struct timespec // struct used to specify how much time to use in nanosleep
    {
        time_t tv_sec;
        long tv_nsec;   
    } timespec;
    timespec t;
    t.tv_sec = 0;
    t.tv_nsec = (long)1000.0; // once per second
    while ( *stat_buf_lock ){  nanosleep(&t,NULL);  }

    *stat_buf_lock = 1;
    for (int i=0; i<STATUS_BUFFER_COUNT; i++)
    {
        if ( stat_buf->prior_status_thread[i]==thread_id ){  stat_buf->prior_status_thread[i]=-1;  }
    }
    *stat_buf_lock = 0;
}

// Gets a status related to an open thread from the stat_buf struct
char* stat_buf_top()
{
    typedef struct timespec // struct used to specify how much time to use in nanosleep
    {
        time_t tv_sec;
        long tv_nsec;   
    } timespec;
    timespec t;
    t.tv_sec = 0;
    t.tv_nsec = (long)1000.0; // once per second
    while ( *stat_buf_lock ){  nanosleep(&t,NULL);  }

    *stat_buf_lock = 1;
    for (int i=0; i<STATUS_BUFFER_COUNT; i++)
    {
        if ( stat_buf->prior_status_thread[i]!=-1 )
        {
            char *temp_buffer = malloc(sizeof(char)*300);
            strcpy(temp_buffer,stat_buf->prior_status[i]);
            *stat_buf_lock = 0;
            return temp_buffer;
        }
    }
    *stat_buf_lock = 0;
    printf("\nNothing in the stat_buf\n");
    return CURRENT_STATUS;
}

// Adds a status to the stat_buf struct and takes note of the 
// calling thread there as well to allow for accessing using
// the stat_buf_top function later on
void add_status_to_stat_buf(char *status, int thread_id)
{
    typedef struct timespec // struct used to specify how much time to use in nanosleep
    {
        time_t tv_sec;
        long tv_nsec;   
    } timespec;
    timespec t;
    t.tv_sec = 0;
    t.tv_nsec = (long)1000.0; // once per second
    while ( *stat_buf_lock ){  nanosleep(&t,NULL);  }

    *stat_buf_lock = 1;
    for (int i=0; i<STATUS_BUFFER_COUNT; i++)
    {
        if ( stat_buf->prior_status_thread[i]==-1 || stat_buf->prior_status_thread[i]==thread_id)
        {
            strcpy(stat_buf->prior_status[i],status);
            stat_buf->prior_status_thread[i] = thread_id;
            break;
        }
    }
    *stat_buf_lock = 0;
}

// Called from main at the start of the program execution. Initializes global
// variables which are used across all threads. Also creates file descriptors
// for all of the .log files which can then, also, be accessed from all threads.
void init_global_variables()
{
    stat_buf_lock =  mmap(NULL,sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_A, -1, 0);
    *stat_buf_lock = 0;

    stat_buf = mmap(NULL,sizeof(status_buffer),PROT_READ|PROT_WRITE, MAP_SHARED|MAP_A, -1, 0);
    init_stat_buf();

    USER_PAUSE =  mmap(NULL,sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_A, -1, 0);
    *USER_PAUSE = 0;

    USER_EXIT =  mmap(NULL,sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_A, -1, 0);
    *USER_EXIT = 0;

    ALREADY_LOGGING = mmap(NULL,sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_A, -1, 0);
    *ALREADY_LOGGING = 0; // 1 if writing to proxy.log 

    ALREADY_LOGGING_DEBUG_INFO = mmap(NULL,sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_A, -1, 0);
    *ALREADY_LOGGING_DEBUG_INFO = 0; // 1 if writing to debug.log 

    REFRESHING_UI_HEADER = mmap(NULL,sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_A, -1, 0);
    *REFRESHING_UI_HEADER = 0; // 1 if refreshing UI table header

    PROXY_LOG = Fopen("proxy.log","w"); // as per instructions
    DEBUG_LOG = Fopen("debug.log","w"); // logs request-response pairs
    RESPONSE_LOG = Fopen("response.log","w"); // logs all responses
    REQUEST_LOG = Fopen("request.log","w"); // logs all requests
    STATUS_LOG = Fopen("status.log","w"); // logs all status updates

    // variable to hold current status
    CURRENT_STATUS = mmap(NULL,sizeof(char), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_A,-1,0);
    set_current_status(" ");

    ENABLE_TIME_SPINNER = mmap(NULL,sizeof(int),PROT_READ|PROT_WRITE, MAP_SHARED|MAP_A, -1, 0);
    *ENABLE_TIME_SPINNER = 2; // dont show either spinner or waiting line
}

// Called from the two functions handling current_status updates (set_current_status
// and set_current_status_id) as well as several other functions which write to .log
// files and require timestamps. Returns a formatted string (H:M:S) representing
// the current time.
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

// Called from the main function to get the total time taken by a request.
// Provided two time_t structs, it computes the difference.
int elapsed_time(time_t t1, time_t t2)
{
    double diff = difftime(t2,t1);
    return (int)diff;
}

// Called from update_cli, returns a char array of length 'size' where
// each item is a space character (" "). This is used to align the table items
// in the command line window interface.
char* get_spacer_string(const int size)
{
    char *spacer = malloc(sizeof(char)*30);
    spacer[1] = 0;
    for(int i=0; i<size; i++)
    {
        spacer[i] = ' ';
    }
    spacer[size] = 0;
    return spacer;
}

// Called from update_cli, returns a copy of input_str but shortened
// in size to 'new_size' length. Items are trimmed off the right side.
char* shorten_string(const char *input_str, const int new_size)
{
    char *shortened = malloc(sizeof(char)*1024);
    strcpy(shortened,input_str);
    shortened[new_size] = 0; // zero-terminate the string
    return shortened;
}

// Called from the main function, checks if the domain name being processed currently
// (req_data->req_host_domain) is in the list of specifically blocked domains (see the
// global variable blocked_domains at the top of the file). This was used mostly for
// debugging to allow for blocking of certain-formatted message types to figure out
// why they weren't being processed correctly.
int is_blocked_domain(struct request_data *req_data)
{
    // iterate over each blocked domain and check if it matches the input domain name
    for ( int i=0; i<num_blocked_domains; i++)
    {
        // we have a match
        if ( strstr(req_data->req_host_domain,blocked_domains[i])!=NULL ){  return 1; }
    }
    return 0; // the domain is not blocked
}

// Called from parse_request to remove trailing '\r' chars from req_host_domain & req_protocol
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

// Gets user input on the command line until they hit enter
char* get_user_input()
{
    char *input = malloc(sizeof(char)*100);
    int buffer_max = 20;

    while (1) { /* skip leading whitespace */
        int c = getchar();
        if (c == EOF) break; /* end of file */
        if (!isspace(c)) {
             ungetc(c, stdin);
             break;
        }
    }

    int i = 0;
    while (1) {
        int c = getchar();
        if (isspace(c) || c == EOF) { /* at end, add terminating zero */
            input[i] = 0;
            break;
        }
        input[i] = c;
        if (i == buffer_max - 1) { /* buffer full */
            buffer_max += buffer_max;
            input = (char*) realloc(input, buffer_max); /* get a new and larger buffer */
        }
        i++;
    }
    return input;
}

// Called if the child of a process has exited, prevents the child from 
// becoming a zombie by freeing its pid for later use by other threads.
void on_child_exit(int signal)
{
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

// Cleans up if a thread is terminated.
void on_thread_exit(int signal)
{
    close(CUR_SERVER_SOCKET);
    close(CUR_CLIENT_SOCKET);
    exit(0);
}

// Called from the main parent thread if the user hits 'Ctrl+C', this will prompt the
// user to enter 'Y' or 'N' to denote whether or not they want to exit the program.
void on_user_command(int signal)
{
    *USER_PAUSE = 1; // tell other threads to pause
    set_current_status("USER");

    fflush(stdout);
    printf("                                                                                                     \r");
    fflush(stdout);
    printf("QUIT [y/n]: ");
    fflush(stdout);
    char *answer = get_user_input();
    if ( strcmp(answer,"y")==0 || strcmp(answer,"Y")==0 )
    {
        *USER_EXIT = 1;
        printf("\n");
        exit(0);
    }
    else
    {
        printf("\r                                                                                           \r");

        sigset_t block_sigint, prev_mask;
        sigemptyset(&block_sigint);
        sigaddset(&block_sigint, SIGINT);
        if ( sigprocmask(SIG_SETMASK, &block_sigint, &prev_mask) < 0)
        {
            perror("Couldn't block SIGINT");
            exit(0);
        }

        struct sigaction ign_sigint, prev;
        ign_sigint.sa_handler = SIG_IGN;
        ign_sigint.sa_flags = 0;
        sigemptyset(&ign_sigint.sa_mask);
        if ( sigaction(SIGINT, &ign_sigint, &prev) < 0)
        {
            perror("Couldn't ignore SIGINT");
            exit(0);
        }
        if ( sigaction(SIGINT, &prev, NULL) < 0)
        {
            perror("Couldn't restore default SIGINT behavior");
            exit(0);
        }
        if ( sigprocmask(SIG_SETMASK, &prev_mask, NULL) < 0)
        {
            perror("Couldn't restore original process sigmask");
            exit(0);
        }
        *USER_PAUSE = 0; // tell other threads to stop pausing
    }
}

// Ignores the signal provided but sets the global variable USER_PAUSE equal to 1. This
// is the SIGINT signal handler for all threads but the main parent one.
void ignore_signal(int signal)
{
    *USER_PAUSE = 1;
    set_current_status("USER");
}

// Holds a thread until the USER_PAUSE variable is set to 0, if USER_EXIT is set to 1 the
// thread will exit the program.
void synchronize_thread()
{
    if (*USER_EXIT)
    {
        close(CUR_CLIENT_SOCKET);
        close(CUR_SERVER_SOCKET);
        exit(0);
    }
    while (*USER_PAUSE==1 && *USER_EXIT==0){  Sleep(1);  }
    if (*USER_EXIT)
    {
        close(CUR_CLIENT_SOCKET);
        close(CUR_SERVER_SOCKET);
        exit(0);
    }
}