/*
	Author: Nathan Herrington
	Date: 2024-05-05
	Description: Simple DFS client for CSCI 4273 PA4
				 gcc dfc.c -o dfc -lpthread -lssl -lcrypto
*/

#include<stdio.h>
#include<string.h>	//strlen
#include<stdlib.h>	//strlen
#include<sys/socket.h>
#include<arpa/inet.h>	//inet_addr
#include<unistd.h>	//write
#include<pthread.h> //for threading , link with lpthread
#include <sys/stat.h>
#include <errno.h>
#include <netdb.h>
#include <sys/time.h>
#include <fcntl.h>
#include <openssl/md5.h>

// error codes
// 1: Timeout
// 2: Bad packet from server
// 3: File does not exist

struct threadGet {
    int ID;
    char serverIP[16];
    char *filename;
    char *part1;
    char *part2;
    char part1Size[100];
    char part2Size[100];
    int error;
};

struct threadPut {
    int ID;
    char serverIP[16];
    char *filename;
    char *part1;
    char *part2;
    int part1Size;
    int part2Size;
    int error;
    int part1Num;
    int part2Num;
};

struct threadList {
    int ID;
    char serverIP[16];
    int error;
};

struct node {
    char *filename;
    int part1;
    int part2;
    int part3;
    int part4;
    struct node *next;
};

struct node *head = NULL;

pthread_mutex_t listLock;

struct node* findNode(char *filename) {
    struct node *current = head;
    while (current != NULL) {
        if (strcmp(current->filename, filename) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}


// Part Map
// Stores which threads have downloaded which parts and what part number it is
int partMap[4][4] = {
        {0, 0, 0, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0}
};


void *getThread(void *);

void *putThread(void *);

void *listThread(void *);

int main(int argc , char *argv[]) {
    // Verify at least 1 argument
    if(argc < 2){
        printf("Usage: %s <cmd get|list|put> <filename1> <filename2> ...\n", argv[0]);
        return 1;
    }

    // Verify command
    if(!strcmp(argv[1], "get") && !strcmp(argv[1], "list") && !strcmp(argv[1], "put")){
        printf("Usage: %s <cmd get|list|put> <filename1> <filename2> ...\n", argv[0]);
        return 1;
    }

    char *cmd = argv[1];

    char IPs[4][25];

    // Get the IP addresses from the config file
    FILE *configFile = fopen("dfc.conf", "r");
    if (configFile == NULL) {
        printf("Error: Could not open dfc.conf\n");
        return 1;
    }
    for (int i = 0; i < 4; i++) {
        fscanf(configFile, "%s", IPs[i]);
    }

    // Create array of threads
    pthread_t threads[4];

    if (!strncmp(cmd, "get", 3)) {
        // Get Command
        struct threadGet *inputStructsArr[4];
        for (int i = 0; i < 4; i++) {
            inputStructsArr[i] = malloc(sizeof(struct threadGet));
            inputStructsArr[i]->ID = i;
            strcpy(inputStructsArr[i]->serverIP, IPs[i]);
            inputStructsArr[i]->filename = argv[2];
            inputStructsArr[i]->error = 0;
            pthread_create(&threads[i], NULL, getThread, (void *) inputStructsArr[i]);
        }
        for (int i = 0; i < 4; i++) {
            pthread_join(threads[i], NULL);
        }
        
        // Check partMap to ensure each part was downloaded at least once
        int part1Count = 0;
        int part2Count = 0;
        int part3Count = 0;
        int part4Count = 0;
        for (int i = 0; i < 4; i++) {
            if (partMap[i][0] >= 1) {
                part1Count++;
            }
            if (partMap[i][1] >= 1) {
                part2Count++;
            }
            if (partMap[i][2] >= 1) {
                part3Count++;
            }
            if (partMap[i][3] >= 1) {
                part4Count++;
            }
        }

        if (part1Count == 0|| part2Count == 0 || part3Count == 0 || part4Count == 0) {
            printf("Error: File missing parts\n");
            return 1;
        }

        // Combine parts
        FILE *outputFile = fopen(argv[2], "w");
        if (outputFile == NULL) {
            printf("Error: Could not open file for writing\n");
            return 1;
        }
        // print part map
        for (int i = 0; i < 4; i++) {
            printf("%d %d %d %d\n", partMap[i][0], partMap[i][1], partMap[i][2], partMap[i][3]);
        }
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                if (partMap[j][i] == 1) {
                    fwrite(inputStructsArr[j]->part1, 1, atoi(inputStructsArr[j]->part1Size), outputFile);
                    printf("Part 1 size: %d\n", atoi(inputStructsArr[j]->part1Size));
                    break;
                } else if (partMap[j][i] == 2) {
                    fwrite(inputStructsArr[j]->part2, 1, atoi(inputStructsArr[j]->part2Size), outputFile);
                    printf("Part 2 size: %d\n", atoi(inputStructsArr[j]->part2Size));
                    break;
                }
            }
        }

        fclose(outputFile);
        for (int i = 0; i < 4; i++) {
            free(inputStructsArr[i]->part1);
            free(inputStructsArr[i]->part2);
            free(inputStructsArr[i]);
        }


    } else if (strncmp(cmd, "put", 3) == 0) {
        FILE *inputFile;
        struct threadPut *inputStructsArr[4];
        for (int i = 2; i < argc; i++) {
            // Attempt to open file from argv
            inputFile = fopen(argv[i], "rb");
            if (inputFile == NULL) {
                printf("Error: Could not open file %s moving to next file\n", argv[i]);
                continue;
            }

            // Get file size
            fseek(inputFile, 0, SEEK_END);
            int fileSize = ftell(inputFile);
            fseek(inputFile, 0, SEEK_SET);

            printf("Reading file %s\n", argv[i]);
            // Read file into buffer
            char *fileBuffer = (char *) malloc(fileSize);
            fread(fileBuffer, 1, fileSize, inputFile);
            fclose(inputFile);

            // Split file into 4 parts
            int partSize = fileSize / 4;
            // Special part 4 size in case of uneven split
            int part4Size = fileSize - (partSize * 3);

            int partSplitTable[4][4][2] = {
                    {{1, 2}, {2, 3}, {3, 4}, {4, 1}},
                    {{4, 1}, {1, 2}, {2, 3}, {3, 4}},
                    {{3, 4}, {4, 1}, {1, 2}, {2, 3}},
                    {{2, 3}, {3, 4}, {4, 1}, {1, 2}}
            };

            MD5_CTX context;
            MD5_Init(&context);
            MD5_Update(&context, argv[i], strlen(argv[i]));
            unsigned char hash[16];
            MD5_Final(hash, &context);

            char hashStr[33];

            for (int i = 0; i < 16; i++) {
                sprintf(&hashStr[i*2], "%02x", (unsigned int)hash[i]);
            }

            int x = strtol(hashStr, NULL, 16) % 4;

            printf("Uploading file %s\n", argv[i]);
            // Create input structs
            for (int j = 0; j < 4; j++) {
                inputStructsArr[j] = malloc(sizeof(struct threadPut));
                inputStructsArr[j]->ID = j;
                strcpy(inputStructsArr[j]->serverIP, IPs[j]);
                inputStructsArr[j]->filename = argv[i];
                inputStructsArr[j]->part1 = (char*) malloc(partSize * sizeof(char));
                inputStructsArr[j]->part2 = (char*) malloc(partSize * sizeof(char));
                memcpy(inputStructsArr[j]->part1, fileBuffer + (partSize * (partSplitTable[x][j][0] - 1)), partSize);
                memcpy(inputStructsArr[j]->part2, fileBuffer + (partSize * (partSplitTable[x][j][1] - 1)), partSize);
                if (partSplitTable[x][j][0] == 4) {
                    inputStructsArr[j]->part1Size = part4Size;
                } else {
                    inputStructsArr[j]->part1Size = partSize;
                }
                if (partSplitTable[x][j][1] == 4) {
                    inputStructsArr[j]->part2Size = part4Size;
                } else {
                    inputStructsArr[j]->part2Size = partSize;
                }
                inputStructsArr[j]->error = 0;
                inputStructsArr[j]->part1Num = partSplitTable[x][j][0];
                inputStructsArr[j]->part2Num = partSplitTable[x][j][1];
                pthread_create(&threads[j], NULL, putThread, (void *) inputStructsArr[j]);
            }
            printf("Waiting for threads to finish\n");
            for (int j = 0; j < 4; j++) {
                pthread_join(threads[j], NULL);
            }
            printf("Threads finished\n");
            free (fileBuffer);
            for (int j = 0; j < 4; j++) {
                free(inputStructsArr[j]->part1);
                free(inputStructsArr[j]->part2);
                free(inputStructsArr[j]);
            }
        }

    } else if (strncmp(cmd, "list", 4) == 0) {
        // List Command
        struct threadList *inputStructsArr[4];
        for (int i = 0; i < 4; i++) {
            inputStructsArr[i] = malloc(sizeof(struct threadList));
            inputStructsArr[i]->ID = i;
            strcpy(inputStructsArr[i]->serverIP, IPs[i]);
            inputStructsArr[i]->error = 0;
            pthread_create(&threads[i], NULL, listThread, (void *) inputStructsArr[i]);
        }
        for (int i = 0; i < 4; i++) {
            pthread_join(threads[i], NULL);
        }
        while (head != NULL) {
            if (head->part1 == 1 && head->part2 == 1 && head->part3 == 1 && head->part4 == 1) {
                printf("%s\n", head->filename);
            } else {
                printf("%s[incomplete]\n", head->filename);
            }
            struct node *temp = head;
            head = head->next;
            free(temp);
        }
        for (int i = 0; i < 4; i++) {
            free(inputStructsArr[i]);
        }
    }
    return 0;

}

void *getThread(void *inputStruct) {
    struct threadGet *input = (struct threadGet *) inputStruct;
    fd_set sock;
    char* message;

    char IP[9];
    strncpy(IP, input->serverIP, 9);

    char port[6];
    strncpy(port, input->serverIP + 10, 5);

    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Specify server address
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(atoi(port));
    server_address.sin_addr.s_addr = inet_addr(IP);

    // Connect to server but only wait 1 second for response
    // Temporarily set socket to non-blocking to allow for timeout
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    connect(client_socket, (struct sockaddr *) &server_address, sizeof(server_address));
    FD_ZERO(&sock); 
    FD_SET(client_socket, &sock); 
    int selectReturn = select(client_socket + 1, NULL, &sock, NULL, &tv);
    if (selectReturn <= 0) {
        // Timeout reached, set error flag and return
        printf("Timeout %d\n", selectReturn);
        input->error = 1;
        return 0;
    }

    // Send GET request
    // Format: GET <filename>
    message = (char*) malloc(100);
    strcpy(message, "GET ");
    strcat(message, input->filename);
    send(client_socket, message, strlen(message), 0);
    free(message);

    // Receive response
    message = (char*) malloc(100);
    recv(client_socket, message, 100, 0);

    if (strncmp(message, "ACK", 3)) {
        // Error occurred, set error flag and return
        input->error = 2;
        return 0;
    }

    if (!strncmp(message, "ACK NULL", 8)) {
        // File does not exist, set error flag and return
        input->error = 3;
        return 0;
    }
    char *part1, *part2;
    strtok(message, " ");
    part1 = strtok(NULL, " ");
    strcpy(input->part1Size, strtok(NULL, " "));
    part2 = strtok(NULL, " ");
    strcpy(input->part2Size, strtok(NULL, " "));
    partMap[input->ID][atoi(part1) - 1] = 1;
    partMap[input->ID][atoi(part2) - 1] = 2;
    free(message);
    // Send ACK
    message = (char*) malloc(100);
    strcpy(message, "ACK");
    send(client_socket, message, strlen(message), 0);
    free(message);
    // Receive part 1
    input->part1 = (char*) malloc(atoi(input->part1Size));
    printf("Part 1 before size: %d\n", atoi(input->part1Size));
    sprintf(input->part1Size, "%ld", recv(client_socket, input->part1, atoi(input->part1Size), 0));
    printf("Part 1 after size: %d\n", atoi(input->part1Size));

    // Send ACK
    message = (char*) malloc(100);
    strcpy(message, "ACK");
    send(client_socket, message, strlen(message), 0);
    free(message);

    // Receive part 2
    input->part2 = (char*) malloc(atoi(input->part2Size));
    // recv(client_socket, input->part2, atoi(input->part2Size), 0);
    sprintf(input->part2Size, "%ld", recv(client_socket, input->part2, atoi(input->part2Size), 0));
    close(client_socket);
    return 0;
}

void *putThread(void *inputStruct) {
    struct threadPut *input = (struct threadPut *) inputStruct;
    fd_set sock;
    char* message;

    char IP[9];
    strncpy(IP, input->serverIP, 9);

    char port[6];
    strncpy(port, input->serverIP + 10, 5);

    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Specify server address
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(atoi(port));
    server_address.sin_addr.s_addr = inet_addr(IP);

    // Connect to server but only wait 1 second for response
    // Temporarily set socket to non-blocking to allow for timeout
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    connect(client_socket, (struct sockaddr *) &server_address, sizeof(server_address));
    FD_ZERO(&sock); 
    FD_SET(client_socket, &sock); 
    int selectReturn = select(client_socket + 1, NULL, &sock, NULL, &tv);
    if (selectReturn <= 0) {
        // Timeout reached, set error flag and return
        printf("Timeout %d\n", selectReturn);
        input->error = 1;
        return 0;
    }

    // Send PUT request
    // PUT request structure PUT <filename> <part1num> <part1Size> <part2num> <part2Size>
    message = (char*) malloc(100);
    sprintf(message, "PUT %s %d %d %d %d", input->filename, input->part1Num, input->part1Size, input->part2Num, input->part2Size);
    send(client_socket, message, strlen(message), 0);
    free(message);

    // Receive response
    message = (char*) malloc(100);
    recv(client_socket, message, 100, 0);

    if (strncmp(message, "ACK", 3) != 0) {
        // Error occurred, set error flag and return
        input->error = 2;
        return 0;
    }
    free(message);

    // Send Part 1
    send(client_socket, input->part1, input->part1Size, 0);

    // Receive ACK
    message = (char*) malloc(100);
    recv(client_socket, message, 100, 0);
    if (strncmp(message, "ACK", 3) != 0) {
        // Error occurred, set error flag and return
        input->error = 2;
        return 0;
    }
    free(message);

    // Send Part 2
    fcntl(client_socket, F_SETFL, fcntl(client_socket, F_GETFL, 0) | O_NONBLOCK);
    send(client_socket, input->part2, input->part2Size, 0);
    close(client_socket);
    return 0;
}

void *listThread(void *inputStruct) {
    struct threadPut *input = (struct threadPut *) inputStruct;
    fd_set sock;
    char* message;

    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    char IP[9];
    strncpy(IP, input->serverIP, 9);

    char port[6];
    strncpy(port, input->serverIP + 10, 5);

    // Specify server address
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(atoi(port));
    server_address.sin_addr.s_addr = inet_addr(IP);

    // Connect to server but only wait 1 second for response
    // Temporarily set socket to non-blocking to allow for timeout
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    fcntl(client_socket, F_SETFL, fcntl(client_socket, F_GETFL, 0) | O_NONBLOCK);
    connect(client_socket, (struct sockaddr *) &server_address, sizeof(server_address));
    FD_ZERO(&sock); 
    FD_SET(client_socket, &sock); 
    int selectReturn = select(client_socket + 1, NULL, &sock, NULL, &tv);
    if (selectReturn <= 0) {
        // Timeout reached, set error flag and return
        input->error = 1;
        return 0;
    }
    // Reset socket to blocking
    fcntl(client_socket, F_SETFL, fcntl(client_socket, F_GETFL, 0) & (~O_NONBLOCK));

    // Send LIST request
    // Format: LIST
    message = (char*) malloc(100);
    strcpy(message, "LIST");
    send(client_socket, message, strlen(message), 0);
    free(message);

    // Receive response
    // <filename1>,<part1>,<part2>|<filename2>,<part1>,<part2>|...
    message = (char*) malloc(1000);
    recv(client_socket, message, 1000, 0);
    
    // Parse response into linked list
    pthread_mutex_lock(&listLock);
    char *token = strtok(message, "|");
    int part1, part2;
    char *filename;
    while (token != NULL) {
        sscanf(token, "%s,%d,%d", filename, &part1, &part2);
        struct node *current = findNode(filename);
        if (current == NULL) {
            current = (struct node*) malloc(sizeof(struct node));
            current->filename = filename;
            if (part1 == 1 || part2 == 1) {
                current->part1 = 1;
            } else {
                current->part1 = 0;
            }
            if (part1 == 2 || part2 == 2) {
                current->part2 = 1;
            } else {
                current->part2 = 0;
            }
            if (part1 == 3 || part2 == 3) {
                current->part3 = 1;
            } else {
                current->part3 = 0;
            }
            if (part1 == 4 || part2 == 4) {
                current->part4 = 1;
            } else {
                current->part4 = 0;
            }
            current->next = head;
            head = current;
        } else {
            if (part1 == 1 || part2 == 1) {
                current->part1 = 1;
            }
            if (part1 == 2 || part2 == 2) {
                current->part2 = 1;
            }
            if (part1 == 3 || part2 == 3) {
                current->part3 = 1;
            }
            if (part1 == 4 || part2 == 4) {
                current->part4 = 1;
            }
        }
        token = strtok(NULL, "|");
    }
    pthread_mutex_unlock(&listLock);
    free(message);
    close(client_socket);
    return 0;
}
