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

#include "stdio.h" // for printf
#include "string.h" // for strlen, etc.
#include "stdlib.h" 
#include "ctype.h" // for tolower


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

/* 
 * main - Main routine for the proxy program 
 */
int main(int argc, char **argv)
{
    init_debug_log(argc,argv); // initialize the debug.log file
    init_proxy_log(); // initialize the debugging log file
    
    /* Check arguments */
    if (argc != 2) 
    {
	    fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        fclose(debug_log);
	    exit(0);
    }
    printl("Correct arguments","\n");


    fclose(debug_log); // close debugging log
    exit(0);
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
        return;
    }
    // log the timestamp if needed
    if (strcmp(line_ending,"\r")==0 || strcmp(line_ending,"\n")==0 || strcmp(line_ending,"")==0){  log_time();  } // write out timestamp

    // write out information
    fprintf(debug_log, "%s", input_str); // write out message
    fprintf(debug_log, "%s", line_ending); // write out line ending
}

// initializes the proxy.log file, if one already exists, it carries over that data to the new instance
void init_proxy_log()
{
    printl("Initializing proxy.log file...","\n");
    // try to read in prior data
    FILE *prior_log;
    prior_log = fopen("proxy.log","r");

    char *buffer = 0;
    long length = -1; 

    if (prior_log)
    {
        printl("Found prior proxy.log file.","\n");
        // proxy.log exits, read in prior data
        fseek(prior_log,0, SEEK_END);
        length = ftell(prior_log);
        fseek(prior_log,0,SEEK_SET);
        buffer = malloc(length + 1); // make room to zero-terminate string
        if (buffer)
        {
            fread(buffer,1,length,prior_log);
        }
        buffer[length] = '\0'; // zero-terminate string
        fclose(prior_log); // close old proxy.log file
    }

    // open new instance of proxy.log (overwriting prior, if one)
    proxy_log = fopen("proxy.log","w");

    if (length != -1)
    {
        printl("Copying data from prior proxy.log instance...","\n");
        // if we read in data from a prior proxy.log file, write it back out
        // to new copy of proxy.log...
        fprintf(proxy_log, "%s", buffer);
    }
    printl("proxy.log initialized.","\n");
}

// initializes the debugging log
void init_debug_log(int argc, char **argv)
{
    debug_log = fopen("[proxy_c]-debug_log.txt","w"); // initialize debugging log
    printl("Initializing proxy.c...","\n");
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