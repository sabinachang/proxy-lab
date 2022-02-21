/*
 * Andrew Id: enhanc
 * This is a concurrent proxy that handles incomming connection on each
 * thread. A cache helps to improve proxy performance. Data that is less
 * than MAX_OBJECT_SIZE is stored in cache with uri as key. If client 
 * requests a cached data, serve data from cache directly. Else, build
 * request header based on client's requests headers and connect to
 * server to recieve data. Send the received data back to client.
 */


#include "csapp.h"
#include "cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <assert.h>

#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

/*
 * Debug macros, which can be enabled by adding -DDEBUG in the Makefile
 * Use these if you find them useful, or delete them if not
 */
#ifdef DEBUG
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define dbg_assert(...)
#define dbg_printf(...)
#endif

/*
 * Max cache and object sizes
 * You might want to move these to the file containing your cache implementation
 */
#define MAX_CACHE_SIZE (1024*1024)
#define MAX_OBJECT_SIZE (100*1024)

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "Mozilla/5.0"
                                    " (X11; Linux x86_64; rv:3.10.0)"
                                    " Gecko/20190801 Firefox/63.0.1";

typedef struct sockaddr SA;
pthread_mutex_t cache_mutex;

void forward_request(int fd);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void build_requesthdrs(rio_t *rio, char *buf, char *hostname, char *port, char *path);
void connect_server(int connfd, char *requst_hdrs, char *url, char *hostname, char *port);
void *thread (void *vargp);
void serve_cache(int connfd, Cache *entry);

/*
 * Open connection at designated port. Wait for client and handle connection 
 * concurrently on each thread. Ignore SIGIPE to prevent proxy from closing.
 * Initialize cache and thread mutex before accepting connections. Clean up
 * cache and thread mutex before proxy shuts down.
 * Use tiny.c as template from CS:APP3e 11.6
 */
int main(int argc, char** argv) {

    int listenfd, *connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    Signal(SIGPIPE, SIG_IGN);
    pthread_mutex_init(&cache_mutex, NULL);
    init_cache();
    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = open_listenfd(argv[1]);
    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen on port: %s\n", argv[1]);
        exit(1);
    }
    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = malloc(sizeof(int));
        *connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        
        if (connfd < 0) {
            fprintf(stderr, "Accept failed");
            continue;
        }
        pthread_create(&tid, NULL, thread, connfd);
    }
    release_cache();
    pthread_mutex_destroy(&cache_mutex);
    return 0;
}

/*
 * Handle client connection in each thread. Detach the thread before 
 * continue to process the connection.
 * 
 * vargp: connection to client
 */
void *thread(void *vargp) {

    int connfd = *((int*) vargp);
    pthread_detach(pthread_self());
    free(vargp);
  
    forward_request(connfd);
    close(connfd);
    return NULL;
}

/*
 * Forward client request to server. If cache contains data already, serve the
 * data to client. Else, parse uri to get hostname, path, port. Then build
 * request header based on client headers and connect to server to get data. 
 * Use tiny.c as template from CS:APP3e 11.6.
 * 
 * fd: connection with client 
 */
void forward_request(int fd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE], requesthdrs[MAXLINE];
    Cache *entry;
    rio_t rio;

    rio_readinitb(&rio, fd);
    
    if (rio_readlineb(&rio, buf, MAXLINE) < 0) {
        printf("request line rio_readlineb error\n");
        return;
    };

    if (sscanf(buf, "%s %s %s", method, uri, version) != 3){
        printf("Request line not properly formed\n");
        fflush(stdout);

        return;
    };

    if (strcasecmp(method, "GET")) {
        printf("Method %s not supported\n", method);
        fflush(stdout);
        return;
    }

    // Check if cache contains requested data
    pthread_mutex_lock(&cache_mutex);
    entry = get_web_object(uri);
    pthread_mutex_unlock(&cache_mutex);

    // Serve from cache if data exist
    if (entry != NULL) {
        serve_cache(fd,entry);

    } else {
        parse_uri(uri, hostname, port, path);
        build_requesthdrs(&rio, requesthdrs, hostname, port, path);
        connect_server(fd, requesthdrs, uri, hostname, port);
    }
    return;
}

/*
 * Serve content from cache to client
 * 
 * connfd: connection to client
 * entry: cache entry that contains data requested by client
 */
void serve_cache(int connfd, Cache *entry) {
    ssize_t writen;
    if ((writen = rio_writen(connfd, entry->web_object, entry->block_size)) == -1) {
        if (errno == EPIPE) {
            printf("write error\n");
            return;
        }
    };
    return;
}

/*
 * Parse a uri into hostname, port, path
 * 
 * uri: input uri to be parsed
 * hostname: place to store parsed hostname
 * port: place to store parsed port
 * path: place to store parsed path
 */
void parse_uri(char *uri, char *hostname, char *port, char *path) {

    char *ptrhost;
    char *ptrport;
    char *ptrpath;

    // Try to find if uri includes http://
    // if yes, move pointer to start of hostname
    // if no, do nothing
    ptrhost = strstr(uri,"//");

    if(ptrhost == NULL) {
        ptrhost = uri;
    } else {
        ptrhost = ptrhost + 2;

    }

    // Try to see if 'hostname:port/path' structure exists
    // if yes, store path and change to 'hostname:port/0path'
    // if no, store '/' as path
    ptrpath = strstr(ptrhost, "/");

    if (ptrpath == NULL) {
        *path = '/';
    } else {
        strcpy(path, ptrpath);
        *ptrpath = '\0';

    }

    // Try to see if 'hostname:port/0path exists
    // if yes, store port and change to 'hostname/0port/0path'
    // if no, store '80' as port
    ptrport = strstr(ptrhost,":");
    
    if(ptrport == NULL) {
        strcpy(port,"80");
    } else {
        strcpy(port, ptrport+1);
        *ptrport = '\0';
    }

    // Store hostname
    strcpy(hostname, ptrhost);

    // Reset to 'hostname/0port/path'
    if (ptrpath != NULL) {
        *ptrpath = '/';
    }

    // Reset to 'hostname:port/path'
    if (ptrport != NULL) {
        *ptrport = ':';
    }
    return;
}

/*
 * Construct request headers based on client's header information. Always
 * send request with HTTP/1.0, 'Host' include hostname and port (if port is
 * not default 80), 'User-Agent' is fixed, 'Connection' and 'Proxy-Connection'
 * set to close. Append rest of headers from client.
 * 
 * rio: poitner to client request headers
 * headers: place to store the constructed headers
 * hostname: connection host name that client requested
 * port: port number that client requested
 * path: path that client requested
 */
void build_requesthdrs(rio_t *rio, char *headers, char *hostname, char *port, char *path) {
    char tmp[MAXLINE];
    char request_buf[MAXLINE];

    char hdrs_host[MAXLINE];
    strcpy(hdrs_host, hostname);

    // Append port number to hostname if 
    // client specified a port
    if (atoi(port) != 80) {
        strcat(hdrs_host, ":");
        strcat(hdrs_host, port);
    }
    
    // Create must have headers
    snprintf(headers, MAXLINE, "GET %s HTTP/1.0\r\n", path);
    strcpy(request_buf, headers);

    snprintf(headers, MAXLINE, "%sHost: %s\r\n", request_buf, hdrs_host);
    strcpy(request_buf, headers);

    snprintf(headers,MAXLINE,  "%sUser-Agent: %s\r\n", request_buf, header_user_agent);
    strcpy(request_buf, headers);

    snprintf(headers,MAXLINE, "%sConnection: close\r\n", request_buf);
    strcpy(request_buf, headers);

    snprintf(headers, MAXLINE, "%sProxy-Connection: close\r\n", request_buf);
    strcpy(request_buf, headers);


    // Append the rest of headers from client
    int n;
    while ((n = rio_readlineb(rio, tmp, MAXLINE)) != -1) {
        if (strcmp(tmp, "\r\n") == 0) {
            snprintf(headers, MAXLINE,"%s\r\n", request_buf);
            return;
        }

        if (strstr(tmp, "Host") != NULL) {
            continue;
        }
        if (strstr(tmp, "User-Agent") != NULL) {
            continue;
        }

        if (strstr(tmp, "Connection") != NULL) {
            continue;
        }

        if(strstr(tmp, "Proxy-Connection") != NULL) {
            continue;
        }

        snprintf(headers, MAXLINE, "%s%s", request_buf, tmp);
        strcpy(request_buf, headers);
    }
    return;
}

/*
 * Establish connection with server on behalf of client. Receive data from 
 * server then pass down to client. If data is within MAX_OBJECT_SIZE, write
 * the data into cache.
 * 
 * connfd: connection with client
 * request_hdrs: headers that client want to send to server
 * url: key to store in cache
 * hostname: server's socket host name
 * port: sever's socket port number
 */
void connect_server(int connfd, char *requst_hdrs, char *url, char *hostname, char *port) {
    int proxy_clientfd;
    rio_t rio;
    char buf[MAXLINE];
    char object[MAX_OBJECT_SIZE];
    ssize_t buflen;
    ssize_t obj_size = 0;
   
    proxy_clientfd = open_clientfd(hostname, port);

    
    if (proxy_clientfd < 0) {
        fprintf(stderr, "Proxy failed to connect to server\n");
        fflush(stdout);
        return;
    }

    rio_readinitb(&rio, proxy_clientfd);
    rio_writen(proxy_clientfd, requst_hdrs, strlen(requst_hdrs));

    while((buflen = rio_readnb(&rio, buf, MAXLINE))) {
        
        // copy data into local array if total size does 
        // not exceed MAX_OBJECT_SIZE
        if ((obj_size + buflen) <= MAX_OBJECT_SIZE) {
            memcpy(object + obj_size, buf, buflen);
        }
        obj_size += buflen;

        if (rio_writen(connfd, buf, buflen) == -1) {
            if (errno == EPIPE) {
                break;
            }
        };
    }

    if (obj_size <= MAX_OBJECT_SIZE && errno != EPIPE) {
        // allow one thread accessing cache each time
        pthread_mutex_lock(&cache_mutex);
        write_cache(url, object, obj_size);
        pthread_mutex_unlock(&cache_mutex);
    }

    close(proxy_clientfd);
    return;
}