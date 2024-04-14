/*
	Author: Nathan Herrington
	Date: 2023-04-14
	Description: Simple proxy server for CSCI 4273 PA3
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
#include "dnsCache.h"

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
	int sock1= *(int*)socket_desc;
	int read_size;
	char *message , client_message[2000], *fileContents;
	char cmd[10];
	char url[200];
	char urlCopy[200];
	char version[10];
	char fileDirectory[100];
	message = (char*) malloc(200);
	FILE *file;
	char fileType[70];

	pthread_detach(pthread_self());

	// Read HTTP request 
	read_size = recv(sock1, client_message , sizeof(client_message) , 0);
	int requestError = sscanf(client_message, "%s %s %s\r\n", cmd, url, version);
	strcpy(urlCopy, cmd);

	if (!strcmp(cmd, "") || !strcmp(url, "") || !strcmp(cmd, "")) {
		// Error parsing the request, send 400 Bad Request response
		printf("Bad request received\n");
		strcpy(message, version);
		strcat(message, " 400 Bad Request\r\nContent-Type: \r\nContent-Length: \r\n\r\n");
		write(sock1, message, strlen(message));
		close(sock1);
		free(message);
		free(socket_desc);
		return NULL;
	}

	if (strcmp(version, "HTTP/1.1") && strcmp(version, "HTTP/1.0")) {
		// Version not supported, send 505 HTTP Version Not Supported response
		strcpy(message, version);
		strcat(message, " 505 HTTP Version Not Supported\r\nContent-Type: \r\nContent-Length: \r\n\r\n");
		write(sock1, message, strlen(message));
		close(sock1);
		free(message);
		free(socket_desc);
		return NULL;
	}

	if (!strcmp("GET ", cmd)) {
		// cmd not allowed
		printf("Method Not Allowed, exiting");
		strcpy(message, version);
		strcat(message, " 400 bad request\r\n Content-Type: \r\n Content-Length: \r\n\r\n");
		write(sock1, message , strlen(message));
	}

	// Strip website from url
	strtok(url, "/");
	char* website = strtok(NULL, "/");
	
	// Check if website exists
	struct hostent *host = gethostbyname(website);

	if (host == NULL) {
		// Website not found, send 404 Not Found response
		strcpy(message, version);
		strcat(message, " 404 Not Found\r\nContent-Type: \r\nContent-Length: \r\n\r\n");
		write(sock1, message, strlen(message));
		close(sock1);
		free(message);
		free(socket_desc);
		return NULL;
	}

	// Open and read blocklist file
	file = fopen("blocklist", "r");
	if (file == NULL) {
		// Error opening blocklist file
		printf("Error opening blocklist file\n");
		strcpy(message, version);
		strcat(message, " 500 Internal Server Error\r\nContent-Type: \r\nContent-Length: \r\n\r\n");
		write(sock1, message, strlen(message));
		close(sock1);
		free(message);
		free(socket_desc);
		return NULL;
	}

	// Check if website is in blocklist
	char blocklist[100];
	while (fgets(blocklist, sizeof(blocklist), file)) {
		if (strstr(website, blocklist) != NULL) {
			// Website is in blocklist, send 403 Forbidden response
			strcpy(message, version);
			strcat(message, " 403 Forbidden\r\nContent-Type: \r\nContent-Length: \r\n\r\n");
			write(sock1, message, strlen(message));
			close(sock1);
			free(message);
			free(socket_desc);
			return NULL;
		}
	}

	// Check if url has dynamic options
	if (strchr(url, '?') == NULL) {
		// No dynamic options found, look in cache
		if (checkHash(url, 60)) {
			// Found in cache, send message from cache
			char* fileName = (char*)malloc(60);
			strcpy(fileName, "cache/");
			strcat(fileName, url);
			file = fopen(fileName, "r");
			fseek(file, 0, SEEK_END);
			int fileSize = ftell(file);
			fseek(file, 0, SEEK_SET);
			fileContents = (char*) malloc(fileSize * sizeof(char));
			int readResult = 0;
			if (fileContents) {
				readResult = fread(fileContents, 1, fileSize, file);
			}
			write(sock1, fileContents, readResult);
			close(sock1);
			free(fileContents);
			free(message);
			free(socket_desc);
			return NULL;
		}
	}

	// Make second socket
	int sock2 = socket(AF_INET, SOCK_STREAM, 0);

	// Connect to website
	struct sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_port = htons(80);
	server.sin_addr = *((struct in_addr*)host->h_addr);
	if (connect(sock2, (struct sockaddr*)&server, sizeof(server)) < 0) {
		// Error connecting to website
		printf("Error connecting to website\n");
		strcpy(message, version);
		strcat(message, " 500 Internal Server Error\r\nContent-Type: \r\nContent-Length: \r\n\r\n");
		write(sock1, message, strlen(message));
		close(sock1);
		free(message);
		free(socket_desc);
		return NULL;
	}

	// Send request to website
	write(sock2, client_message, strlen(client_message));

	// Read response from website
	read_size = recv(sock2, client_message, sizeof(client_message), 0);
	if (read_size < 0) {
		// Error reading response from website
		printf("Error reading response from website\n");
		strcpy(message, version);
		strcat(message, " 500 Internal Server Error\r\nContent-Type: \r\nContent-Length: \r\n\r\n");
		write(sock1, message, strlen(message));
		close(sock1);
		free(message);
		free(socket_desc);
		return NULL;
	}

	// Send response to client
	write(sock1, client_message, strlen(client_message));

	close(sock1);
	close(sock2);
	free(message);
	free(socket_desc);
	return NULL;
}