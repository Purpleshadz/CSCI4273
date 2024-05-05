/*
	Author: Nathan Herrington
	Date: 2024-03-20
	Description: Simple TCP web server for CSCI 4273 PA3
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

//the thread function
void *connection_handler(void *);

// Main function from https://www.binarytides.com/server-client-example-c-sockets-linux/ with minor modifications
int main(int argc , char *argv[])
{
    int portNum = 0;
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    portNum = atoi(argv[1]);

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
	server.sin_port = htons( 8888 );
	
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

void determineFileType(char *url, char *fileType) {
	// Get the file extension
	char *extension = strrchr(url, '.');
	if (extension == NULL) {
		// File extension not found, send 400 Bad Request response
		strcat(fileType, "400 Bad Request\r\nContent-Type: \r\nContent-Length: \r\n\r\n");
		return;
	}

	// Determine the content type based on the file extension
	if (!strcmp(extension, ".html")) {
		strcpy(fileType, "text/html");
	} else if (!strcmp(extension, ".txt")) {
		strcpy(fileType, "text/plain");
	} else if (!strcmp(extension, ".png")) {
		strcpy(fileType, "image/png");
	} else if (!strcmp(extension, ".gif")) {
		strcpy(fileType, "image/gif");
	} else if (!strcmp(extension, ".jpg") || !strcmp(extension, ".jpeg")) {
		strcpy(fileType, "image/jpeg");
	} else if (!strcmp(extension, ".ico")) {
		strcpy(fileType, "image/x-icon");
	} else if (!strcmp(extension, ".css")) {
		strcpy(fileType, "text/css");
	} else if (!strcmp(extension, ".js")) {
		strcpy(fileType, "application/javascript");
	} 
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
	char url[200];
	char version[10];
	char fileDirectory[100];
	message = (char*) malloc(200);
	FILE *file;
	char fileType[70];

	pthread_detach(pthread_self());

	// Read HTTP request 
	read_size = recv(sock , client_message , sizeof(client_message) , 0);
	int requestError = sscanf(client_message, "%s %s %s\r\n", cmd, url, version);


	if (!strcmp(cmd, "") || !strcmp(url, "") || !strcmp(cmd, "")) {
		// Error parsing the request, send 400 Bad Request response
		printf("Bad request received\n");
		strcpy(message, version);
		strcat(message, " 400 Bad Request\r\nContent-Type: \r\nContent-Length: \r\n\r\n");
		write(sock, message, strlen(message));
		close(sock);
		free(message);
		free(socket_desc);
		return NULL;
	}

	if (strcmp(version, "HTTP/1.1") && strcmp(version, "HTTP/1.0")) {
		// Version not supported, send 505 HTTP Version Not Supported response
		strcpy(message, version);
		strcat(message, " 505 HTTP Version Not Supported\r\nContent-Type: \r\nContent-Length: \r\n\r\n");
		write(sock, message, strlen(message));
		close(sock);
		free(message);
		free(socket_desc);
		return NULL;
	}
	if (strcmp("GET ", cmd)) {
		if (url[strlen(url) - 1] == '/') {
			// Searching for directory, find default html
			printf("Directory requested, looking for default html\n");
			file = fopen("www/index.html", "r");
			strcpy(fileType, "text/html");
		} else {
			strcpy(fileDirectory, "www");
			strcat(fileDirectory, url);
			printf("Looking for file %s\n", fileDirectory);
			determineFileType(url, fileType);
			if (!strncmp(fileType, "image", 5)) {
				file = fopen(fileDirectory, "rb");
			} else if (!strncmp(fileType, " 400", 4)) {
				strcat(message, "400 Bad Request\r\nContent-Type: \r\nContent-Length: \r\n\r\n");
				write(sock, message, strlen(message));	
				free(message);
				free(socket_desc);
				return 0;
			} else {
				file = fopen(fileDirectory, "r");
			}
		}
		// Check if the file exists and has read permissions
		if (!file) {
			if (errno == EACCES) {
				// Read permissions missing for file
				printf("Missing read permissions\n");
				strcpy(message, version);
				strcat(message, " 403 Forbidden\r\nContent-Type: \r\nContent-Length: \r\n\r\n");
				write(sock, message, strlen(message));	
				close(sock);
				free(message);
				free(socket_desc);
				return 0;
			}
			// File doesn't exist
			printf("File not found\n");
			strcpy(message, version);
			strcat(message, " 404 Not Found\r\nContent-Type: \r\nContent-Length: \r\n\r\n");
			write(sock, message, strlen(message));
			close(sock);	
			free(message);
			free(socket_desc);
			return NULL;
		}

		// Build header
		strcpy(message, version);
		strcat(message, " 200 OK\n");
		strcat(message, "Content-Type: ");
		strcat(message, fileType);
		strcat(message, "\r\n");
		fseek(file, 0, SEEK_END);
		int fileSize = ftell(file);
		char sizeStr[30];
		sprintf(sizeStr, "Content-Length: %d\r\n\r\n", fileSize);
		strcat(message, sizeStr);
		fseek(file, 0, SEEK_SET);

		// Read file
		fileContents = (char*) malloc(fileSize * sizeof(char));
		int readResult = 0;
		if (fileContents) {
			readResult = fread(fileContents, 1, fileSize, file);
		}
		char* finalMessage;
		finalMessage = (char*) malloc(strlen(message) + readResult);

		// Build https packet
		strcpy(finalMessage, message);
		memcpy(finalMessage + strlen(message), fileContents, fileSize);
		write(sock , finalMessage , strlen(message) + readResult);
		printf("File Sent\n");
		fclose(file);
		free(fileContents);
		free(finalMessage);
	} else {
		printf("Method Not Allowed, exiting");
		strcpy(message, version);
		strcat(message, " 405 Method Not Allowed\r\n Content-Type: \r\n Content-Length: \r\n\r\n");
		write(sock , message , strlen(message));
	}
	close(sock);
	free(message);
	free(socket_desc);
	return NULL;
}