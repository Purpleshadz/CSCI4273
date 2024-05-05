#include "dnsCache.h"

struct node* head;

long int addHash(char* url) {
    MD5_CTX context;
    MD5_Init(&context);
    MD5_Update(&context, url, strlen(url));
    unsigned char hash[16];
    MD5_Final(hash, &context);
    struct node* newNode = (struct node*)malloc(sizeof(struct node));
    memcpy(newNode->url, url, 50);
    memcpy(newNode->hash, hash, 16);
    newNode->creationTime = (struct timeval*)malloc(sizeof(struct timeval));
    gettimeofday(newNode->creationTime, NULL);
    newNode->next = NULL;
    if (head == NULL) {
        head = newNode;
        newNode->previous = NULL;
        // print seconds + microseconds
        return newNode->creationTime->tv_sec + newNode->creationTime->tv_usec;
    }
    struct node* temp = head;
    while (temp->next != NULL) {
        temp = temp->next;
    }
    temp->next = newNode;
    newNode->previous = temp;
    return newNode->creationTime->tv_sec + newNode->creationTime->tv_usec;
}

long int checkHash(char* url, int timeout) {
    MD5_CTX context;
    MD5_Init(&context);
    MD5_Update(&context, url, strlen(url));
    unsigned char hash[16];
    MD5_Final(hash, &context);
    struct node* temp = head;
    struct node* delete;
    struct timeval* currentTime = (struct timeval*)malloc(sizeof(struct timeval));
    gettimeofday(currentTime, NULL);
    while (temp != NULL) {
        if (temp->creationTime->tv_sec + timeout < currentTime->tv_sec) {
            // delete node
            delete = temp;
            temp = temp->next;
            if (delete->previous != NULL) {
                delete->previous->next = delete->next;
            }
            if (delete->next != NULL) {
                delete->next->previous = delete->previous;
            }
            char fileName[256];
            sprintf(fileName, "cache/%li", delete->creationTime->tv_sec + delete->creationTime->tv_usec);
            printf("Deleting file %s\n", fileName);
            remove(fileName);
            free(delete->creationTime);
            free(delete);
            // delete associated file from cache
            continue;
        } else {
            if (memcmp(temp->hash, hash, 16) == 0) {
                free(currentTime);
                return temp->creationTime->tv_sec + temp->creationTime->tv_usec;
            }
            temp = temp->next;
        }

    }
    free(currentTime);
    return -1;
}