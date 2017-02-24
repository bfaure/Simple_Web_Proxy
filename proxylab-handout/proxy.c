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
int parse_port_num(char ** argv);

// Global variables (hold processed request info)
char req_command[10]; // GET, POST, etc.
char req_host_short[100]; // www.yahoo.com, etc.
char req_host_long[512]; // http://www.yahoo.com/, etc.
char req_protocol[100]; // HTTP/1.1, etc.

void parse_request(char *buffer);


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
    //printl("Correct arguments","\n");
    int listening_port = parse_port_num(argv);


    printf("> Opening connection to listening port... \n");
    int listening_socket = Open_listenfd(listening_port);
    printf("> Connection established on rc = %d\n",listening_socket);
    
    printf("> Listening for requests...\n");

    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);

    while (1)
    {
        int client_socket = Accept(listening_socket, (struct sockaddr*)&client_addr, &addrlen);
        printf("> Found client at socket %d\n",client_socket);

        char buffer[1024]; // to read the socket data into
        int n = read(client_socket,buffer,1023);

        if (n<0)
        {
            printf("> Error reading from socket \n");
        }

        printf("> Socket data...\n");
        printf("%s\n",buffer);

        parse_request(buffer);
        fulfill_request();
    }
    exit(0);
}

void fulfill_request()
{
    return;
}

void parse_request(char *buffer)
{
    int request_length = strlen(buffer);
    printf("> Request length: %d\n",request_length);

    char buffer_copy[request_length+1];
    strcpy(buffer_copy,buffer);

    printf("> Iterating over lines of request...\n");

    // splitting the buffer on '\n' endline characters
    //char *line = strtok(buffer_copy,"\n");

    char *end_str;
    char *line = strtok_r(buffer_copy,"\n",&end_str);


    int line_index = 0;
    while (line != NULL)
    {
        //printf("> Line: %s\n",line);

        char *end_token;
        char *word = strtok_r(line," ",&end_token);

        int word_index = 0;
        while (word != NULL)
        {
            // Iterate over each word in line
            if (line_index==0 && word_index==0)
            {
                printf("> Request: %s\n",word);
                strcpy(req_command,word);
            }
            if (line_index==0 && word_index==1)
            {
                printf("> Host (long): %s\n",word);
                strcpy(req_host_long,word);
            }
            if (line_index==0 && word_index==2)
            {
                printf("> Protocol: %s\n",word);
                strcpy(req_protocol,word);
            }

            if (line_index==1 && word_index==1)
            {
                printf("> Host (short): %s\n",word);
                strcpy(req_host_short,word);
            }

            word = strtok_r(NULL," ",&end_token);
            word_index++;
        }
        line = strtok_r(NULL,"\n",&end_str);
        line_index++;

    }

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
    FILE *prior_log;
    prior_log = Fopen("proxy.log","r");

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
int parse_port_num(char ** argv)
{
    char * port_str = argv[1];
    int port_num = atoi(port_str);
    printf("> Listening port: %d\n",port_num);
    //printl("Listening port: ","");
    //printl(port_str," ");
    //printl("","\n");
    return port_num;
}