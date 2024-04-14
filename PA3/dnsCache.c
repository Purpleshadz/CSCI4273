#include "dnsCache.h"

struct node* head;

void addHash(char* url) {
    MD5_CTX context;
    MD5_Init(&context);
    MD5_Update(&context, url, strlen(url));
    unsigned char hash[16];
    MD5_Final(hash, &context);
    struct node* newNode = (struct node*)malloc(sizeof(struct node));
    memcpy(newNode->url, url, 50);
    memcpy(newNode->hash, hash, 16);
    newNode->next = NULL;
    if (head == NULL) {
        head = newNode;
        newNode->previous = NULL;
        return;
    }
    struct node* temp = head;
    while (temp->next != NULL) {
        temp = temp->next;
    }
    temp->next = newNode;
    newNode->previous = temp;
    gettimeofday(newNode->creationTime, NULL);
}

int checkHash(char* url, int timeout) {
    MD5_CTX context;
    MD5_Init(&context);
    MD5_Update(&context, url, strlen(url));
    unsigned char hash[16];
    MD5_Final(hash, &context);
    struct node* temp = head;
    struct timeval* currentTime = (struct timeval*)malloc(sizeof(struct timeval));
    gettimeofday(currentTime, NULL);
    while (temp != NULL) {
        if (temp->creationTime->tv_sec + timeout < currentTime->tv_sec) {
            if (temp->previous != NULL) {
                temp->previous->next = temp->next;
            }
            if (temp->next != NULL) {
                temp->next->previous = temp->previous;
            }
            free(temp);
            temp = temp->next;
            // delete associated file from cache
            char* fileName = (char*)malloc(60);
            strcpy(fileName, "cache/");
            strcat(fileName, url);
            remove(fileName);
            continue;
        }
        if (memcmp(temp->hash, hash, 16) == 0) {
            free(currentTime);
            return 1;
        }
        temp = temp->next;

    }
    free(currentTime);
    return 0;
}