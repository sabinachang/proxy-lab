
#ifndef __CACHE_H__
#define __CACHE_H__

#include <sys/types.h>

typedef struct Cache {
    char *url;
    char *web_object;
    ssize_t block_size;
    struct Cache *next;
    struct Cache *prev;
} Cache;


Cache *cache; 
ssize_t cache_size;

Cache *get_web_object(char *request_url);
void write_cache(char *url, char *web_object, ssize_t block_size);
void init_cache();
void release_cache();


#endif