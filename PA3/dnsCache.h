#include <openssl/md5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

struct node {
    unsigned char hash[16];
    char url[256];
    struct node* next;
    struct node* previous;
    struct timeval *creationTime;
};

long int addHash(char* url);

long int checkHash(char* url, int timeout);