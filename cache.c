
/*
 * Andrew Id: enhanc
 * cache.c contains helper functions to manipulate cache. The cache is
 * implemented with a doubly linked list. Each entry contains url, 
 * web_object, block_size (size of web_object), prev and next ptr. The
 * size of web_object cannot exceed MAX_OBJECT_SIZE and the total web_object
 * in the list cannot exceed MAX_CACHE_SIZE. Every time a entry is referenced, 
 * it is moved to the front of the list. When the cache does not have enough
 * free space to hold a new entry, LRU data at the end of the list is evicted.
 */

#include "csapp.h"
#include "cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define MAX_CACHE_SIZE (1024*1024)
#define MAX_OBJECT_SIZE (100*1024)

/*
 * Move an entry to the front of the cache.
 * 
 * entry: entry to be moved
 */
static void move_to_head(Cache *entry) {

    //entry at theh head already, do nothing
    if (entry == cache) {
        return;
    }

    //entry in the middle
    if (entry->next != NULL && entry->prev != NULL) {
        Cache *p = entry->prev;
        Cache *n = entry->next;

        p->next = entry->next;
        n->prev = entry->prev;

        cache->prev = entry;
        entry->next = cache;
        entry->prev = NULL;
        cache = entry;
    }

    //entry at the back
    if (entry->next == NULL && entry->prev != NULL) {
        entry->prev->next = NULL;

        cache->prev = entry;
        entry->next = cache;
        entry->prev = NULL;
        cache = entry;
    }

    return;
}

/*
 * Remove entries from cache until enough spaces are freed
 * 
 * space: the amount of space that need to be freed
 */
static void evict_entries(ssize_t space) {
    
    Cache *e = cache;
    ssize_t freed = 0;

    // Go to the end of list where LRU data locate
    while (e->next != NULL) {
        e = e->next;
    }

    // Free entries from the back until enough space
    // shows up
    do {
        Cache *tmp;

        freed += e->block_size;
        e->prev->next = NULL;
        
        tmp = e;
        e = e->prev;
        
        free(tmp->web_object);
        free(tmp->url);
        free(tmp);

    } while (freed < space);

    // update cache size
    cache_size -= freed;

    return;
}

/*
 * See if an url has been stored in the cache. Return the cache entry
 * if match is found. Else return NULL.
 * 
 * request_url: test if this url exists in cache
 */
Cache *get_web_object(char *request_url) {
    Cache *cur = cache;
    while (cur != NULL) {
        if (strcmp(request_url, cur->url) == 0) {
            // Update LRU count 
            move_to_head(cur);
            return cur;
        }

        cur = cur->next;
    }
    return NULL;
}

/*
 * Add a new entry into cache. Remove LRU cached data before inserting
 * new data if there are not enough free space in cache. 
 * 
 * url: key of the entry
 * web_object: data to be stored 
 * block_size: size of the data
 */
void write_cache(char *url, char *web_object, ssize_t block_size) {
    
    // check the same url has not been added by other thread
    if (get_web_object(url) != NULL) {
        return;
    }

    // Remove LRU entries if new data cannot fit in
    if (cache_size + block_size > MAX_CACHE_SIZE) {
        evict_entries(block_size);
    }

    cache_size += block_size;

    // Allocate memory
    Cache *entry = (Cache *) malloc(sizeof(Cache));
    entry->url = (char *) malloc(sizeof(char) * MAXLINE);
    entry->web_object = (char *) malloc(sizeof(char) * block_size);

    // Fill in key and value
    strcpy(entry->url, url);
    entry->web_object = (char *) memcpy(entry->web_object, web_object,block_size);
    entry->block_size = block_size;
    entry->next = NULL;
    entry->prev = NULL;

    //insert empty list
    if (cache == NULL) {
        cache = entry;
        return;
    }

    //insert at head
    cache->prev = entry;
    entry->next = cache;
    entry->prev = NULL;
    cache = entry;
    return;
}

/*
 * Initialize cache and cache size
 */
void init_cache() {
    cache = NULL;
    cache_size = 0;
    return;
}

/*
 * Free any allocated blocks in the cache
 */
void release_cache() {
    Cache *cur = cache;
    Cache *tmp;
    while (cur) {
        tmp = cur->next;

        cur->next = NULL;
        cur->prev = NULL;

        free(cur->web_object);
        free(cur->url);
        free(cur);

        cur = tmp;
    }
    return;
}

 
