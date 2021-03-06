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
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "thpool.h"

#define BUFSIZE 1024
#define MAXERRS 16
#define HT_SIZE 10000

unsigned long fs_hashtable[HT_SIZE]; /* hash table for file size */

long counter; /* global counter for total transactions */
struct timeval start_time, curr_time;

extern char **environ; /* the environment */

pthread_mutex_t lock;

struct request_args
{
    int childfd;
    struct sockaddr_in clientaddr;
};

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
        error("Error! opening file\n");

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

void handle_zombie(int sig)
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

void *handle_request(void *request_args)
{
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
    long filesize;          /* file size */

    struct request_args *args = (struct request_args *)request_args;
    int childfd = args->childfd;
    struct sockaddr_in clientaddr = args->clientaddr;
    free(request_args);

    char client_ipAddr[20];
    struct sockaddr_in *pV4Addr = (struct sockaddr_in *)&clientaddr;
    struct in_addr ipAddr = pV4Addr->sin_addr;
    inet_ntop(AF_INET, &ipAddr, client_ipAddr, INET_ADDRSTRLEN);
    // printf("DEBUG: host address %s\n", client_ipAddr);

    /* open the child socket descriptor as a stream */
    // if ((stream = fdopen(childfd, "r+")) == NULL)
    // {
    //     perror("ERROR on fdopen");
    //     // fclose(stream);
    //     close(childfd);
    //     return 0;
    // }

    /* get the HTTP request line */
    // fgets(buf, BUFSIZE, stream);
    read(childfd, buf, BUFSIZE);
    sscanf(buf, "%s %s %s\n", method, uri, version);

    /* only support the GET method */
    if (strcasecmp(method, "GET"))
    {
        cerror(stream, method, "501", "Not Implemented",
               "ECE673 does not implement this method");
        // fclose(stream);
        close(childfd);
        return 0;
    }

    /* read (and ignore) the HTTP headers */
    // fgets(buf, BUFSIZE, stream);
    // while (strcmp(buf, "\r\n"))
    // {
    //     fgets(buf, BUFSIZE, stream);
    // }

    // printf("%s %s %s\n", method, uri, version);

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
    // printf("%s\n", uri);
    /* serve static content */
    if (is_static)
    {

        // printf("DEBUG: filename %s\n", filename);

        /* load content in terms of filename in uri */
        p = load_content(filename, &filesize);

        /* print response header */
        // fprintf(stream, "HTTP/1.0 200 OK\n");
        // fprintf(stream, "Server: ECE673 Web Server\n");
        // fprintf(stream, "\r\n");
        // fflush(stream);

        // fwrite(p, 1, filesize, stream);

        write(childfd, "HTTP/1.0 200 OK\n", strlen("HTTP/1.0 200 OK\n"));
        write(childfd, "Server: ECE673 Web Server\n", strlen("Server: ECE673 Web Server\n"));
        write(childfd, "\r\n", strlen("\r\n"));
        write(childfd, p, filesize);
    }

    /* clean up */
    // fclose(stream);
    close(childfd);
    return 0;
}

int main(int argc, char **argv)
{
    /* variables for connection management */
    int parentfd;                  /* parent socket */
    int childfd;                   /* child socket */
    int portno;                    /* port to listen on */
    char option;                   /* server option */
    int clientlen;                 /* byte size of client's address */
    struct hostent *hostp;         /* client host info */
    char *hostaddrp;               /* dotted decimal host addr string */
    int optval;                    /* flag value for setsockopt */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */

    /* variables for connection I/O */
    struct stat sbuf; /* file status */
    int fd;           /* static content filedes */
    int pid;          /* process id from fork */
    int wait_status;  /* status from wait */
    long duration;    /* duration from start_time to curr_time */

    /* variables for print statistics */
    pid_t command_pid = getpid();
    char *command = (char *)malloc(100 * sizeof(char));
    printf("Disk Utilitiy: \n");
    int status = system("cat /sys/block/sda/stat");

    /* check command line args */
    if (argc != 2 && argc != 3)
    {
        fprintf(stderr, "usage: %s <port> [-<option>]\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);
    if (argc == 3)
        option = argv[2][1];

    /* Handle Zombie Processes*/
    struct sigaction sa_child;
    sa_child.sa_handler = handle_zombie;
    sa_child.sa_flags = SA_RESTART;
    sigemptyset(&sa_child.sa_mask);
    if (sigaction(SIGCHLD, &sa_child, NULL) == -1)
    {
        perror("sigchild action");
        exit(1);
    }

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
    threadpool thpool;
    if (option == 't')
        thpool = thpool_init(8);

    while (1)
    {

        /* wait for a connection request */
        childfd = accept(parentfd, (struct sockaddr *)&clientaddr, &clientlen);
        if (childfd < 0)
        {
            // perror("ERROR on accept");
            if (option == 't') 
                thpool_wait(thpool);
            continue;
        }

        if (counter == 0)
            gettimeofday(&start_time, NULL);

        struct request_args *args = malloc(sizeof(struct request_args));
        args->childfd = childfd;
        args->clientaddr = clientaddr;

        if (option == 'f') /* multi-processing */
        {
            if (fork() == 0)
            { // child
                handle_request(args);
                close(childfd);
                exit(0);
            }
            // parent
            close(childfd);
        }
        else if (option == 't')
        {
            // pthread_t tid;
            // pthread_attr_t attr;

            // pthread_attr_init(&attr);
            // pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

            // pthread_create(&tid, &attr, handle_request, args);
            // if (pthread_join(tid, NULL))
            //     close(childfd);
            thpool_add_work(thpool, (void *)handle_request, args);
        }
        else
        {
            handle_request(args);
            close(childfd);
        }
        counter++;
        if (counter % 10000 == 0)
        {
            gettimeofday(&curr_time, NULL);
            duration = ((curr_time.tv_sec * 1000000 + curr_time.tv_usec) - (start_time.tv_sec * 1000000 + start_time.tv_usec));
            printf("Request Count:  %ld\n", counter);
            printf("Avg req rate: %lf per second\n", 1000000.0 * counter / duration);
            sprintf(command, "ps -p %d -o pcpu,pmem", command_pid);
            status = system(command);
            printf("\n");

            if (counter % 100000 == 0)
            {
                printf("Disk Utilitiy: \n");
                status = system("cat /sys/block/sda/stat");
                printf("Context Switches:\n");
                sprintf(command, "cat /proc/%d/status | grep ctxt", command_pid);
                status = system(command);
                printf("Total Seconds: %lf\n", duration / 1000000.0);
                // if (option == 't')
                //     thpool_destroy(thpool);
                // return 0;
            }
        }
    }
}
