/*
	Author: Nathan Herrington
	Date: 2024-05-05
	Description: Simple DFS server for CSCI 4273 PA4
				 gcc dfs.c -o dfs -lpthread
                 ./dfs ./dfs1 10001 & ./dfs ./dfs2 10002 & ./dfs ./dfs3 10003 & ./dfs ./dfs4 10004 &

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

//the thread function
void *connection_handler(void *);

char fileDirectory[10];
pthread_mutex_t fileListMutex;

// Main function from https://www.binarytides.com/server-client-example-c-sockets-linux/ with minor modifications
int main(int argc , char *argv[])
{
    int portNum = 0;
    if (argc != 3) {
        fprintf(stderr, "usage: %s <storage directory> <port>\n", argv[0]);
        exit(1);
    }
    portNum = atoi(argv[2]);
    if (strlen(argv[1]) > 10) {
        fprintf(stderr, "Directory name too long\n");
        exit(1);
    }
    strncpy(fileDirectory, argv[2], 10);

	int socket_desc , client_sock , c , *new_sock;
	struct sockaddr_in server , client;
	
	//Create socket
	socket_desc = socket(AF_INET , SOCK_STREAM , 0);
	if (socket_desc == -1)
	{
		printf("Could not create socket");
	}
	puts("Socket created");
	
	//Prepare the sockaddr_in structure
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons( portNum );
	
	//Bind
	if( bind(socket_desc,(struct sockaddr *)&server , sizeof(server)) < 0)
	{
		//print the error message
		perror("bind failed. Error");
		return 1;
	}
	puts("bind done");
	
	//Listen
	listen(socket_desc , 10);
	
	//Accept and incoming connection
	puts("Waiting for incoming connections...");
	c = sizeof(struct sockaddr_in);
	
	while( (client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c)) )
	{
		puts("Connection accepted");
		
		pthread_t sniffer_thread;
		new_sock = (int *) malloc(sizeof(int));
		*new_sock = client_sock;
		
		if( pthread_create( &sniffer_thread , NULL ,  connection_handler , (void*) new_sock) < 0)
		{
			perror("could not create thread");
			return 1;
		}
		
		//Now join the thread , so that we dont terminate before the thread
		// pthread_join( sniffer_thread , NULL);
		puts("Handler assigned");
	}
	
	if (client_sock < 0)
	{
		perror("accept failed");
		return 1;
	}
	
	return 0;
}

/*
 * This will handle connection for each client
 * */
void *connection_handler(void *socket_desc)
{
	//Get the socket descriptor
	int sock = *(int*)socket_desc;
	int read_size;
	char *message , client_message[2000], *fileContents;
	char cmd[10];
	char requestDetails[200];
    char fileName[100];
    char fileListName[100];
	FILE *file1;
    FILE *file2;
    FILE *fileList;
    char part1[4];
    char part2[4];
    char part1Size[100];
    char part2Size[100];
    int readResult;
    char* finalMessage;

	pthread_detach(pthread_self());

	// Read HTTP request 
	read_size = recv(sock , client_message , sizeof(client_message) , 0);
    int requestError = sscanf(client_message, "%s %s", cmd, requestDetails);
    printf("Request: %s\n", client_message);

    chdir(fileDirectory);

	if (!strncmp(client_message, "GET", 3)) {
        // GET request structure GET filename <filename>
        sscanf(requestDetails, "filename %s", fileName);
        
        // Check if file with  filename fileList exist in fileDirectory
        pthread_mutex_lock(&fileListMutex);
        fileList = fopen("fileList", "r");
        if (!fileList) {
            // No files exist on server, skip to sending NULL to signify no parts of file present
            printf("No files Exist on Server here\n");
            message = (char*) malloc(100);
            strcpy(message, "ACK NULL");
            write(sock, message, strlen(message));
            close(sock);
            free(message);
            free(socket_desc);
            return NULL;
        }

        // Check if file exists in fileList
        char fileLine[110];
        int fileFound = 0;
        while (fgets(fileLine, 110, fileList)) {
            sscanf(fileLine, "%s,%s,%s,%s,%s", fileListName, part1, part1Size, part2, part2Size);
            if (!strncmp(fileListName, fileName, strlen(fileName))) {
                fileFound = 1;
                break;
            }
        }
        fclose(fileList);
        pthread_mutex_unlock(&fileListMutex);
        if (!fileFound) {
            // File not found in fileList
            printf("File not found in fileList\n");
            message = (char*) malloc(100);
            strcpy(message, "ACK NULL");
            write(sock, message, strlen(message));
            close(sock);
            free(message);
            free(socket_desc);
            return NULL;
        } else {
            // File found in fileList, send parts of file ACK <part1> <part1Size> <part2> <part2Size>
            printf("File found in fileList\n");
            message = (char*) malloc(100);
            strcpy(message, "ACK ");
            strcat(message, part1);
            strcat(message, " ");
            strcat(message, part1Size);
            strcat(message, " ");
            strcat(message, part2);
            strcat(message, " ");
            strcat(message, part2Size);
            write(sock, message, strlen(message));
        }

        // Wait for response from client
        // Response should be of form ACK <bool is part 1 needed> <bool is part 2 needed>
        read_size = recv(sock , client_message , sizeof(client_message) , 0);
        if (strncmp(cmd, "ACK", 3)) {
            // Bad request received
            printf("Bad request received\n");
            message = (char*) malloc(100);
            strcpy(message, "400 Bad Request");
            write(sock, message, strlen(message));
            close(sock);
            free(message);
            free(socket_desc);
            return NULL;
        }

        // Check if client needs part 1
        if (client_message[4] == '1') {
            // Send part 1
            char part1FileName[101];
            strcpy(part1FileName, fileName);
            strcat(part1FileName, part1);
            file1 = fopen(part1FileName, "rb");
            if (!file1) {
                // Part 1 not found
                printf("Part 1 not found\n");
                message = (char*) malloc(100);
                strcpy(message, "ACK ERROR");
                write(sock, message, strlen(message));
                close(sock);
                free(message);
                free(socket_desc);
                return NULL;
            }
            // Read file
            fileContents = (char*) malloc(atoi(part1Size) * sizeof(char));
            readResult = 0;
            if (fileContents) {
                readResult = fread(fileContents, 1, atoi(part1Size), file1);
            }

            write(sock , fileContents , readResult);
            printf("Part 1 Sent\n");
            fclose(file1);
            free(fileContents);

            // Wait for ACK from client to continue
            read_size = recv(sock , client_message , sizeof(client_message) , 0);
            if (strncmp(client_message, "ACK", 3)) {
                // Bad request received
                printf("Bad request received\n");
                message = (char*) malloc(100);
                strcpy(message, "400 Bad Request");
                write(sock, message, strlen(message));
                close(sock);
                free(message);
                free(socket_desc);
                return NULL;
            }
        }

        // Check if client needs part 2
        if (client_message[6] == '1') {
            // Send part 2
            char part2FileName[101];
            strcpy(part2FileName, fileName);
            strcat(part2FileName, part2);
            file2 = fopen(part2FileName, "rb");
            if (!file2) {
                // Part 2 not found
                printf("Part 2 not found\n");
                message = (char*) malloc(100);
                strcpy(message, "ACK ERROR");
                write(sock, message, strlen(message));
                close(sock);
                free(message);
                free(socket_desc);
                return NULL;
            }
            // Read file
            fileContents = (char*) malloc(atoi(part2Size) * sizeof(char));
            readResult = 0;
            if (fileContents) {
                readResult = fread(fileContents, 1, atoi(part2Size), file2);
            }
            write(sock , fileContents , readResult);
            printf("Part 2 Sent\n");
            fclose(file2);
            free(fileContents);
        }
        close(sock);
        free(socket_desc);
        return NULL;
	} else if (!strncmp(client_message, "LIST", 4)) {
        // LIST request structure LIST
        // Send list of files in fileList
        pthread_mutex_lock(&fileListMutex);
        fileList = fopen("fileList", "r");
        if (!fileList) {
            // No files exist on server, skip to sending NULL to signify no parts of file present
            printf("No files Exist on Server there\n");
            message = (char*) malloc(100);
            strcpy(message, "ACK NULL");
            write(sock, message, strlen(message));
            close(sock);
            free(message);
            free(socket_desc);
            return NULL;
        }

        // Create list of all files in fileList
        // <filename1>,<part1>,<part2>|<filename2>,<part1>,<part2>|...
        char fileLine[110];
        char fileListMessage[1000];
        while (fgets(fileLine, 110, fileList)) {
            char fileListName[100];
            sscanf(fileLine, "%s,%s,%s,%s,%s", fileListName, part1, part1Size, part2, part2Size);
            strcat(fileListMessage, fileListName);
            strcat(fileListMessage, ",");
            strcat(fileListMessage, part1);
            strcat(fileListMessage, ",");
            strcat(fileListMessage, part2);
            strcat(fileListMessage, "|");
        }
        write(sock, fileListMessage, strlen(fileListMessage));
        fclose(fileList);
        pthread_mutex_unlock(&fileListMutex);
        close(sock);
        free(socket_desc);
        return NULL;
    } else if (!strncmp(client_message, "PUT", 3)) {
        // PUT request structure PUT <filename> <part1> <part1Size> <part2> <part2Size>
        sscanf(requestDetails, "%s %s %s %s %s", fileName, part1, part1Size, part2, part2Size);

        // Send ACK to client
        printf("Sending ACK to client\n");
        message = (char*) malloc(100);
        strcpy(message, "ACK");
        write(sock, message, strlen(message));
        free(message);

        // Receive part 1
        printf("Receiving part 1\n");
        message = (char*) malloc(atoi(part1Size) * sizeof(char));
        read_size = recv(sock , message , atoi(part1Size) * sizeof(char), 0);

        // Write part 1 to file
        printf("Writing part 1 to file\n");
        char part1FileName[101];
        strcpy(part1FileName, fileName);
        strcat(part1FileName, part1);
        printf("Part 1 file name: %s\n", part1FileName);
        file1 = fopen(part1FileName, "wb");
        fwrite(message, 1, atoi(part1Size), file1);
        fclose(file1);
        free(message);

        // Send ACK to client
        printf("Sending ACK to client\n");
        message = (char*) malloc(100);
        strcpy(message, "ACK");
        write(sock, message, strlen(message));
        free(message);

        // Receive part 2
        printf("Receiving part 2\n");
        message = (char*) malloc(atoi(part2Size) * sizeof(char));
        read_size = recv(sock , message , atoi(part2Size) * sizeof(char), 0);

        // Write part 2 to file
        printf("Writing part 2 to file\n");
        char part2FileName[101];
        strcpy(part2FileName, fileName);
        strcat(part2FileName, part2);
        printf("Part 2 file name: %s\n", part2FileName);
        file2 = fopen(part2FileName, "wb");
        fwrite(message, 1, atoi(part2Size), file2);
        fclose(file2);
        free(message);

        // Update fileList
        pthread_mutex_lock(&fileListMutex);
        fileList = fopen("fileList", "a");
        fprintf(fileList, "%s,%s,%s,%s,%s\n", fileName, part1, part1Size, part2, part2Size);
        fclose(fileList);
        pthread_mutex_unlock(&fileListMutex);
        close(sock);
        free(socket_desc);
        return NULL;
    } else {
		printf("Bad request received\n");
        message = (char*) malloc(100);
        strcpy(message, "400 Bad Request");
		write(sock, message, strlen(message));
		close(sock);
		free(message);
		free(socket_desc);
		return NULL;
	}
}