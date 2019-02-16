/*
 * server.c - a minimal HTTP server that only serves static 
 *            content with the GET method. 
 *
 *            usage: server <port>
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 1024
#define MAXERRS 16
#define HT_SIZE 10000

unsigned long fs_hashtable[HT_SIZE]; /* hash table for file size */

long counter; /* global counter for total transactions */
struct timeval start_time, curr_time;

extern char **environ; /* the environment */

/*
 * error - wrapper for perror used for bad syscalls
 */
void error(char *msg)
{
    perror(msg);
    exit(1);
}

/*
 * cerror - returns an error message to the client
 */
void cerror(FILE *stream, char *cause, char *errno,
            char *shortmsg, char *longmsg)
{
    fprintf(stream, "HTTP/1.1 %s %s\n", errno, shortmsg);
    fprintf(stream, "Content-type: text/html\n");
    fprintf(stream, "\n");
    fprintf(stream, "<html><title>ECE673 Error</title>");
    fprintf(stream, "<body bgcolor="
                    "ffffff"
                    ">\n");
    fprintf(stream, "%s: %s\n", errno, shortmsg);
    fprintf(stream, "<p>%s: %s\n", longmsg, cause);
    fprintf(stream, "<hr><em>The ECE673 Web server</em>\n");
}

unsigned long hash(char *str)
{
    unsigned long hash = 5381;
    int c;
    while (c = *str++)
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash;
}

/*
 * read_cfg - read configuration file for hash table of file size
 */
void read_cfg(char *filename)
{
    FILE *fptr;
    int i;
    unsigned long filesize;

    if ((fptr = fopen(filename, "r")) == NULL)
    {
        printf("Error! opening file");
        exit(1);
    }

    i = 0;
    while (fscanf(fptr, "%lu", &filesize) != EOF)
    {
        // printf("Debug: Data from the file: [%d] %lu\n", i, filesize);
        fs_hashtable[i] = filesize;
        i++;
    }

    fclose(fptr);
}

/*
 * load_content - returns malloced memory for filename, 
 *                based on filesize fetched from hash table 
 */
char *load_content(char *filename, long *filesize)
{
    unsigned long id;
    char *addr;
    id = hash(filename) % HT_SIZE;
    *filesize = fs_hashtable[id];
    addr = (char *)malloc(*filesize);
    return addr;
}

int main(int argc, char **argv)
{

    /* variables for connection management */
    int parentfd;                  /* parent socket */
    int childfd;                   /* child socket */
    int portno;                    /* port to listen on */
    int clientlen;                 /* byte size of client's address */
    struct hostent *hostp;         /* client host info */
    char *hostaddrp;               /* dotted decimal host addr string */
    int optval;                    /* flag value for setsockopt */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */

    /* variables for connection I/O */
    FILE *stream;           /* stream version of childfd */
    char buf[BUFSIZE];      /* message buffer */
    char method[BUFSIZE];   /* request method */
    char uri[BUFSIZE];      /* request uri */
    char version[BUFSIZE];  /* request method */
    char filename[BUFSIZE]; /* path derived from uri */
    char filetype[BUFSIZE]; /* path derived from uri */
    char cgiargs[BUFSIZE];  /* cgi argument list */
    char *p;                /* temporary pointer */
    int is_static;          /* static request? */
    struct stat sbuf;       /* file status */
    int fd;                 /* static content filedes */
    int pid;                /* process id from fork */
    int wait_status;        /* status from wait */

    long filesize; /* file size */
    long duration; /* duration from start_time to curr_time */

    /* check command line args */
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    /* read the size of files from configuration file */
    read_cfg("config_srv.txt");

    /* open socket descriptor */
    parentfd = socket(AF_INET, SOCK_STREAM, 0);
    if (parentfd < 0)
        error("ERROR opening socket");

    /* allows us to restart server immediately */
    optval = 1;
    setsockopt(parentfd, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval, sizeof(int));

    /* bind port to socket */
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);
    if (bind(parentfd, (struct sockaddr *)&serveraddr,
             sizeof(serveraddr)) < 0)
        error("ERROR on binding");

    /* get us ready to accept connection requests */
    if (listen(parentfd, 5) < 0) /* allow 5 requests to queue up */
        error("ERROR on listen");

    /*
     * main loop: wait for a connection request, parse HTTP,
     * serve requested content, close connection.
     */
    counter = 0;
    clientlen = sizeof(clientaddr);
    while (1)
    {

        /* wait for a connection request */
        childfd = accept(parentfd, (struct sockaddr *)&clientaddr, &clientlen);
        if (childfd < 0)
            error("ERROR on accept");

        if (counter == 0)
            gettimeofday(&start_time, NULL);

        char client_ipAddr[20];
        struct sockaddr_in *pV4Addr = (struct sockaddr_in *)&clientaddr;
        struct in_addr ipAddr = pV4Addr->sin_addr;
        inet_ntop(AF_INET, &ipAddr, client_ipAddr, INET_ADDRSTRLEN);
        // printf("DEBUG: host address %s\n", client_ipAddr);

        /* open the child socket descriptor as a stream */
        if ((stream = fdopen(childfd, "r+")) == NULL)
            error("ERROR on fdopen");

        /* get the HTTP request line */
        fgets(buf, BUFSIZE, stream);
        sscanf(buf, "%s %s %s\n", method, uri, version);

        /* only support the GET method */
        if (strcasecmp(method, "GET"))
        {
            cerror(stream, method, "501", "Not Implemented",
                   "ECE673 does not implement this method");
            fclose(stream);
            close(childfd);
            continue;
        }

        /* read (and ignore) the HTTP headers */
        fgets(buf, BUFSIZE, stream);
        while (strcmp(buf, "\r\n"))
        {
            fgets(buf, BUFSIZE, stream);
        }

        /* parse the uri */
        if (!strstr(uri, "cgi-bin"))
        { /* static content */
            is_static = 1;
            strcpy(cgiargs, "");
            strcpy(filename, ".");
            strcat(filename, uri);
            if (uri[strlen(uri) - 1] == '/')
                strcat(filename, "index.html");
        }
        else
        { /* ignore requests for dynamic content */
            is_static = 0;
        }

        /* make sure the file exists */

        /* serve static content */
        if (is_static)
        {

            // printf("DEBUG: filename %s\n", filename);

            /* load content in terms of filename in uri */
            p = load_content(filename, &filesize);

            /* print response header */
            fprintf(stream, "HTTP/1.0 200 OK\n");
            fprintf(stream, "Server: ECE673 Web Server\n");
            fprintf(stream, "\r\n");
            fflush(stream);

            fwrite(p, 1, filesize, stream);
        }

        /* clean up */
        fclose(stream);
        close(childfd);

        counter++;
        if (counter % 1000 == 0)
        {
            gettimeofday(&curr_time, NULL);
            duration = ((curr_time.tv_sec * 1000000 + curr_time.tv_usec) - (start_time.tv_sec * 1000000 + start_time.tv_usec));
            printf("Avg req rate: %lf per second\n", 1000000.0 * counter / duration);
        }
    }
}
