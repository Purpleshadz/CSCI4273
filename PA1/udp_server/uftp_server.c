/*
	Author: Nathan Herrington
	Date: 2023-02-20
	Description: Simple UDP server for CSCI 4273 PA1
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/select.h>
#include <ctype.h>
#include <dirent.h> 

#define BUFSIZE 1024

/*
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(1);
}

int main(int argc, char **argv) {
    int sockfd; /* socket */
    int portno; /* port to listen on */
    int clientlen; /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    struct hostent *hostp; /* client host info */
    char buf[BUFSIZE]; /* message buf */
    char *hostaddrp; /* dotted decimal host addr string */
    int optval; /* flag value for setsockopt */
    int n; /* message byte size */
    char data[BUFSIZE - 1];
    char cmd[BUFSIZE];
    char fileName[BUFSIZE];
    // Add timeout to socket of 100ms
    struct timeval timeout;
    int timeoutSec = 1;
    int timeoutUSec = 0;
    fd_set sock;

    /* 
    * check command line arguments 
    */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    /* 
    * socket: create the parent socket 
    */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets 
    * us rerun the server immediately after we kill it; 
    * otherwise we have to wait about 20 secs. 
    * Eliminates "ERROR on binding: Address already in use" error. 
    */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
            (const void *)&optval , sizeof(int));

    /*
    * build the server's Internet address
    */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /* 
    * bind: associate the parent socket with a port 
    */
    if (bind(sockfd, (struct sockaddr *) &serveraddr, 
        sizeof(serveraddr)) < 0) 
        error("ERROR on binding");

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);


    clientlen = sizeof(clientaddr);
    while (1) {
        char ack[4];
        sprintf(ack, "0ACK");
        /*
        * recvfrom: receive a UDP datagram from a client
        */
        bzero(buf, BUFSIZE);
        n = recvfrom(sockfd, buf, BUFSIZE, 0,
            (struct sockaddr *) &clientaddr, &clientlen);
        if (n < 0) {
            error("ERROR in recvfrom");
        }
        sscanf(buf, "%s %s", cmd, fileName);

        /* 
        * gethostbyaddr: determine who sent the datagram
        */
        hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr, 
                sizeof(clientaddr.sin_addr.s_addr), AF_INET);
        if (hostp == NULL)
        error("ERROR on gethostbyaddr");
        hostaddrp = inet_ntoa(clientaddr.sin_addr);
        if (hostaddrp == NULL)
        error("ERROR on inet_ntoa\n");
        printf("server received datagram from %s (%s)\n", 
        hostp->h_name, hostaddrp);
        printf("server received %d/%d bytes: %s\n", strlen(buf), n, buf);
        
        /* 
        * sendto: echo the input back to the client 
        */
        if (strncmp(cmd, "get", 3) == 0) {
            FILE *file = fopen(fileName, "r");
            if (file == NULL) {
                // File does not exist
                buf[0] = 'n';
                buf[1] = 'a';
                sendto(sockfd, buf, strlen(buf), 0, &clientaddr, clientlen);
            } else {
                // File exists
                // Send file size
                fseek(file, 0, SEEK_END);
                int fileSize = ftell(file);
                fseek(file, 0, SEEK_SET);
                sprintf(buf, "%d", fileSize);
                sendto(sockfd, buf, strlen(buf), 0, &clientaddr, clientlen);
                if (n < 0) {
                    error("ERROR in sendto");
                }
                do {
                    FD_ZERO(&sock); 
                    FD_SET(sockfd, &sock); 
                    timeout.tv_sec = timeoutSec;
                    timeout.tv_usec = timeoutUSec;
                    int selectReturn = select(sockfd + 1, &sock, NULL, NULL, &timeout);
                    if (selectReturn <= 0) {
                        // Timeout reached, resend file size
                        sprintf(buf, "%d", fileSize);
                        n = sendto(sockfd, buf, strlen(buf), 0, &clientaddr, clientlen);
                        if (n < 0) {
                            error("ERROR in sendto");
                        }
                    } else {
                        memset(buf, 0, sizeof(buf));
                        recvfrom(sockfd, buf, BUFSIZE, 0, &clientaddr, &clientlen);
                        if (strncmp(buf, ack, 4) != 0) {
                            // Incorrect ACK received, resend file size
                            sprintf(buf, "%d", fileSize);
                            n = sendto(sockfd, buf, BUFSIZE, 0, &clientaddr, clientlen);
                            if (n < 0) {
                                error("ERROR in sendto");
                            }
                        }
                    }
                } while (strncmp(buf, ack, 4) != 0);

                int packetNumber = 0;
                int bytesRead;
                while (fileSize > 0) {
                    memset(buf, 0, BUFSIZE);
                    memset(data, 0, BUFSIZE - 1);
                    // Generate ACK that will be used to compare against the received ACK
                    sprintf(ack, "%dACK", packetNumber);
                    // Read the file into the buffer
                    if (fileSize < BUFSIZE - 1) {
                        bytesRead = fread(data, 1, fileSize, file);
                    } else {
                        bytesRead = fread(data, 1, BUFSIZE - 1, file);
                    }
                    buf[0] = packetNumber + '0';
                    memcpy(buf + 1, data, bytesRead);
                    // Send the packet to the client
                    n = sendto(sockfd, buf, BUFSIZE, 0, (struct sockaddr *) &clientaddr, clientlen);
                    if (n < 0) {
                        error("ERROR in sendto");
                    }
                    // Resend the packet until the correct ACK is received
                    do {
                        FD_ZERO(&sock); 
                        FD_SET(sockfd, &sock); 
                        timeout.tv_sec = timeoutSec;
                        timeout.tv_usec = timeoutUSec;
                        int selectReturn = select(sockfd + 1, &sock, NULL, NULL, &timeout);
                        if (selectReturn <= 0) {
                            // Timeout reached, resend packet
                            memset(buf, 0, BUFSIZE);
                            buf[0] = packetNumber + '0';
                            memcpy(buf + 1, data, bytesRead);
                            n = sendto(sockfd, buf, BUFSIZE, 0, &clientaddr, clientlen);
                            if (n < 0) {
                                error("ERROR in sendto");
                            }
                        } else {
                            memset(buf, 0, BUFSIZE);
                            recvfrom(sockfd, buf, BUFSIZE, 0, &clientaddr, &clientlen);
                            if ((buf[0] - '0') != packetNumber) {
                                // Incorrect ACK received, resend packet
                                memset(buf, 0, BUFSIZE);
                                buf[0] = packetNumber + '0';
                                memcpy(buf + 1, data, bytesRead);
                                n = sendto(sockfd, buf, BUFSIZE, 0, (struct sockaddr *) &clientaddr, clientlen);
                                if (n < 0) {
                                    error("ERROR in sendto");
                                }
                            }
                        }
                    } while ((buf[0] - '0') != packetNumber);
                    fileSize -= bytesRead;
                    packetNumber++;
                    if (packetNumber == 10) {
                        packetNumber = 0;
                    }
                }
                fclose(file);
            }
        } else if (strncmp(cmd, "put", 3) == 0) {
            printf("Receiving file\n");
            char fileSize[100];
            int fileSizeInt;
            int bytesTransferred;

            // Parse the file name and file size from the client
            sscanf(buf, "%s %s %s", cmd, fileName, fileSize);
            fileSizeInt = atoi(fileSize);

            FILE *file = fopen(fileName, "w");

            bytesTransferred = 0;
            char packetNumber = "0";
            int ackNumber = 0;
            while (bytesTransferred < fileSizeInt) {
                memset(data, 0, BUFSIZE - 1);
                memset(buf, 0, BUFSIZE);
                // Send ACK to the client
                sprintf(ack, "%dACK", ackNumber);
                strncpy(buf, ack, 4);
                n = sendto(sockfd, ack, strlen(ack), 0, &clientaddr, clientlen);
                if (n < 0) {
                    error("ERROR in sendto");
                }
                // Send ACKS until the correct packet is received
                do {
                    FD_ZERO(&sock); 
                    FD_SET(sockfd, &sock); 
                    timeout.tv_sec = timeoutSec;
                    timeout.tv_usec = timeoutUSec;
                    int selectReturn = select(sockfd + 1, &sock, NULL, NULL, &timeout);
                    if (selectReturn <= 0) {
                        // Timeout reached, resending ACK
                        sprintf(ack, "%dACK", ackNumber);
                        strncpy(buf, ack, 4);
                        n = sendto(sockfd, ack, strlen(ack), 0, &clientaddr, clientlen);
                        if (n < 0) {
                            error("ERROR in sendto");
                        }
                    } else {
                        memset(buf, 0, BUFSIZE);
                        n = recvfrom(sockfd, buf, BUFSIZE, 0, &clientaddr, &clientlen);
                        if ((buf[0] - '0') != ackNumber) {\
                            // Incorrect packet received, resending ACK
                            memset(buf, 0, BUFSIZE);
                            sprintf(ack, "%dACK", ackNumber);
                            n = sendto(sockfd, ack, strlen(ack), 0, &clientaddr, clientlen);
                            if (n < 0) {
                                error("ERROR in sendto");
                            }
                        }
                    }
                } while ((buf[0] - '0') != ackNumber);

                // the packetNumber is in the first byte of the packet, write everything but that byte to the file
                memset(buf + BUFSIZE, 0, 1);
                memset(data, 0, BUFSIZE - 1);
                if (bytesTransferred + BUFSIZE - 1 > fileSizeInt) {
                    memcpy(data, buf + 1, fileSizeInt - bytesTransferred);
                    fwrite(data, 1, fileSizeInt - bytesTransferred, file);
                    bytesTransferred += fileSizeInt - bytesTransferred;
                } else {
                    memcpy(data, buf + 1, BUFSIZE - 1);
                    fwrite(data, 1, BUFSIZE - 1, file);
                    bytesTransferred += BUFSIZE - 1;
                }

                // send the packetNumber + ACK back to the server
                ackNumber++;
                if (ackNumber == 10) {
                    ackNumber = 0;
                }
            }
            fclose(file);
            printf("File received\n");
        } else if (strncmp(cmd, "delete", 6) == 0) {
            FILE *file = fopen(fileName, "r");
            if (file == NULL) {
                // File does not exist
                sendto(sockfd, "na", 2, 0, (struct sockaddr *) &clientaddr, clientlen);
            } else {
                fclose(file);
            }
            if (remove(fileName) == 0) {
                // File deleted
                sendto(sockfd, "File deleted", 13, 0, (struct sockaddr *) &clientaddr, clientlen);
            } else {
                // Error deleting file
                sendto(sockfd, "er", 2, 0, (struct sockaddr *) &clientaddr, clientlen);
            }
        } else if (strncmp(buf, "ls", 2) == 0) {
            memset(buf, 0, BUFSIZE);
            // Get the current directory
            DIR *d;
            struct dirent *dir;
            d = opendir(".");
            if (d) {
                while ((dir = readdir(d)) != NULL) {
                    sprintf(buf, "%s %s\n", buf, dir->d_name);
                }
                closedir(d);
            }
            // Send the directory to the client
            sendto(sockfd, buf, strlen(buf), 0, (struct sockaddr *) &clientaddr, clientlen);
        } else if (strncmp(buf, "exit", 4) == 0) {
            printf("Exiting\n");
            close(sockfd);
            return 0;
        }
    }
    return 0;
}
