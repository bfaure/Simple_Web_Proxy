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
FILE *proxy_log;

// function to write the current time out to the log file
void log_time()
{
    time_t rawtime;
    struct tm *timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    fprintf(proxy_log, "[%d %d %d %d:%d:%d]> ",timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
}

// function to write to the log file, writes out the timestamp before input string
// input_str: string to write to log
// line_ending: [0,1,2,3] = ['\n','\r','',' ']
void printl(const char *input_str, const char *line_ending)
{
    log_time(); // write out timestamp
    fprintf(proxy_log, "%s", input_str);
    fprintf(proxy_log, "%s", line_ending);

    //if (line_ending==0){  fprintf(proxy_log, "%s\n",input_str);  }
    //if (line_ending==1){  fprintf(proxy_log, "%s\r",input_str);  }
    //if (line_ending==2){  fprintf(proxy_log, "%s",  input_str);  }
    //if (line_ending==3){  fprintf(proxy_log, "%s ", input_str);  }
}


/* 
 * main - Main routine for the proxy program 
 */
int main(int argc, char **argv)
{
    proxy_log = fopen("[proxy_c]-debug_log.txt","w"); // initialize debugging log
    printl("Initializing proxy.c...","\n");
    
    if (argc==1){  printl("No arguments passed.","\n");  }
    else
    {
        printl("Arguments: ",""); 
        for (int i=1; i<argc; i++)
        {
            if(i!=argc-1){  printl(argv[i]," ");  }
            else         {  printl(argv[i],"\n"); }
        }
    }

    /* Check arguments */
    if (argc != 2) 
    {
	    fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        fclose(proxy_log);
	    exit(0);
    }
    printl("Correct arguments","\n");


    fclose(proxy_log); // close debugging log
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


