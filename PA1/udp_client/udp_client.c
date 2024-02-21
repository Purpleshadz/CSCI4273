#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <sys/time.h>
#include <ctype.h>

#define BUFSIZE 1024

/* 
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(0);
}

int main(int argc, char **argv) {
    int sockfd, portno, n;
    int serverlen;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    char *hostname;
    char buf[BUFSIZE];
    char data[BUFSIZE - 1];
    char cmd[BUFSIZE];
    char fileName[BUFSIZE];
    int fileSize;
    char ack[4];
    // Add timeout to socket of 100ms
    struct timeval timeout;
    int timeoutSec = 1;
    int timeoutUSec = 0;
    fd_set sock;

    /* check command line arguments */
    if (argc != 3) {
       fprintf(stderr,"usage: %s <hostname> <port>\n", argv[0]);
       exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");


    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
	  (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(portno);

    serverlen = sizeof(serveraddr);

    while (1) {
        /* get a message from the user */
        bzero(buf, BUFSIZE);
        printf("Choose one of these commands: get [file_name], put [file_name], delete [file_name], ls, exit\n");
        fgets(buf, BUFSIZE, stdin);
        sscanf(buf, "%s %s", cmd, fileName);

        if (strncmp(cmd, "get", 3) == 0) {
            int bytesTransferred;

            /* send the cmd to the server */
            n = sendto(sockfd, buf, strlen(buf), 0, &serveraddr, serverlen);
            if (n < 0) {
                error("ERROR in sendto");
            }
            
            /* read server reply */
            do {
                FD_ZERO(&sock); 
                FD_SET(sockfd, &sock); 
                timeout.tv_sec = timeoutSec;
                timeout.tv_usec = timeoutUSec;
                int selectReturn = select(sockfd + 1, &sock, NULL, NULL, &timeout);
                if (selectReturn <= 0) {
                    // Timeout reached, resend command
                    sendto(sockfd, buf, strlen(buf), 0, &serveraddr, serverlen);
                } else {
                    // Reading server reply
                    recvfrom(sockfd, buf, BUFSIZE, 0, &serveraddr, &serverlen);
                }
            } while (strncmp(buf, cmd, 3) == 0);
            if (strncmp(buf, "na", 2) == 0) {
                printf("File not found\n");
                continue;
            }
            fileSize = atoi(buf);

            FILE *file = fopen(fileName, "w");

            bytesTransferred = 0;
            char packetNumber = "0";
            int ackNumber = 0;
            while (bytesTransferred < fileSize) {
                memset(data, 0, BUFSIZE - 1);
                memset(buf, 0, BUFSIZE);
                // Generate ACK and send ACK
                sprintf(ack, "%dACK", ackNumber);
                strncpy(buf, ack, 4);
                n = sendto(sockfd, ack, strlen(ack), 0, &serveraddr, serverlen);
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
                        // Timeout reached, resend ACK
                        sprintf(ack, "%dACK", ackNumber);
                        strncpy(buf, ack, 4);
                        n = sendto(sockfd, ack, strlen(ack), 0, &serveraddr, serverlen);
                        if (n < 0) {
                            error("ERROR in sendto");
                        }
                    } else {
                        memset(buf, 0, BUFSIZE);
                        n = recvfrom(sockfd, buf, BUFSIZE, 0, &serveraddr, &serverlen);
                        if ((buf[0] - '0') != ackNumber) {
                            // Incorrect  packet received, resend ACK
                            memset(buf, 0, BUFSIZE);
                            sprintf(ack, "%dACK", ackNumber);
                            n = sendto(sockfd, ack, strlen(ack), 0, &serveraddr, serverlen);
                            if (n < 0) {
                                error("ERROR in sendto");
                            }
                        }
                    }
                } while ((buf[0] - '0') != ackNumber);

                // the packetNumber is in the first byte of the packet, write everything but that byte to the file
                memset(buf + BUFSIZE, 0, 1);
                memset(data, 0, BUFSIZE - 1);
                if (bytesTransferred + BUFSIZE - 1 > fileSize) {
                    memcpy(data, buf + 1, fileSize - bytesTransferred);
                    fwrite(data, 1, fileSize - bytesTransferred, file);
                    bytesTransferred += fileSize - bytesTransferred;
                } else {
                    memcpy(data, buf + 1, BUFSIZE - 1);
                    fwrite(data, 1, BUFSIZE - 1, file);
                    bytesTransferred += BUFSIZE - 1;
                }
                ackNumber++;
                if (ackNumber == 10) {
                    ackNumber = 0;
                }
            }
            fclose(file);
            printf("File received\n");
        } else if (strncmp(cmd, "put", 3) == 0) {
            // Generate ACK that will be used to compare against the received ACK
            strncpy(ack, "0ACK", 4);
            FILE *file = fopen(fileName, "r");
            if (file == NULL) {
                printf("File not found\n");
                continue;
            }
            printf("Sending file\n");

            // Send file size with command in the form of "put [file_name] [file_size]"
            fseek(file, 0, SEEK_END);
            int fileSize = ftell(file);
            fseek(file, 0, SEEK_SET);
            sprintf(buf, "%s %s %d", cmd, fileName, fileSize);
            sendto(sockfd, buf, strlen(buf), 0, &serveraddr, serverlen);
            if (n < 0) {
                error("ERROR in sendto");
            }
            // Resend file size until ready ACK is received
            do {
                FD_ZERO(&sock); 
                FD_SET(sockfd, &sock); 
                timeout.tv_sec = timeoutSec;
                timeout.tv_usec = timeoutUSec;
                int selectReturn = select(sockfd + 1, &sock, NULL, NULL, &timeout);
                if (selectReturn <= 0) {
                    // Timeout reached, resend file size
                    sprintf(buf, "%d", fileSize);
                    n = sendto(sockfd, buf, BUFSIZE, 0, &serveraddr, serverlen);
                    if (n < 0) {
                        error("ERROR in sendto");
                    }
                } else {
                    memset(buf, 0, sizeof(buf));
                    recvfrom(sockfd, buf, BUFSIZE, 0, &serveraddr, &serverlen);
                    if (strncmp(buf, ack, 4) != 0) {
                        // Incorrect ACK received, resend file size
                        sprintf(buf, "%d", fileSize);
                        n = sendto(sockfd, buf, BUFSIZE, 0, &serveraddr, serverlen);
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
                // Read file into data
                if (fileSize < BUFSIZE - 1) {
                    bytesRead = fread(data, 1, fileSize, file);
                } else {
                    bytesRead = fread(data, 1, BUFSIZE - 1, file);
                }
                // Send packet
                buf[0] = packetNumber + '0';
                memcpy(buf + 1, data, bytesRead);
                n = sendto(sockfd, buf, BUFSIZE, 0, (struct sockaddr *) &serveraddr, serverlen);
                if (n < 0) {
                    error("ERROR in sendto");
                }
                // Resend packet until correct ACK is received
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
                        n = sendto(sockfd, buf, BUFSIZE, 0, &serveraddr, serverlen);
                        if (n < 0) {
                            error("ERROR in sendto");
                        }
                    } else {
                        memset(buf, 0, BUFSIZE);
                        recvfrom(sockfd, buf, BUFSIZE, 0, &serveraddr, &serverlen);
                        if ((buf[0] - '0') != packetNumber) {
                            // Incorrect ACK received, resend packet
                            memset(buf, 0, BUFSIZE);
                            buf[0] = packetNumber + '0';
                            memcpy(buf + 1, data, bytesRead);
                            n = sendto(sockfd, buf, BUFSIZE, 0, (struct sockaddr *) &serveraddr, serverlen);
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
        } else if (!strncmp(cmd, "delete", 6)) {
            // Send delete command to server
            n = sendto(sockfd, buf, strlen(buf), 0, &serveraddr, serverlen);
            if (n < 0) {
                error("ERROR in sendto");
            }
            recvfrom(sockfd, buf, BUFSIZE, 0, &serveraddr, &serverlen);
            if (strncmp(buf, "na", 2) == 0) {
                printf("File not found\n");
            } else if (strncmp(buf, "er", 2) == 0){
                printf("Error deleting file\n");
            } else {
                printf("File deleted\n");
            }
        } else if (!strncmp(buf, "ls", 2)) {
            // Send ls command to server
            n = sendto(sockfd, buf, strlen(buf), 0, &serveraddr, serverlen);
            if (n < 0) {
                error("ERROR in sendto");
            }
            recvfrom(sockfd, buf, BUFSIZE, 0, &serveraddr, &serverlen);
            printf("Files in directory: %s\n", buf);
        } else if (!strncmp(buf, "exit", 4)) {
            // Send exit command to server
            n = sendto(sockfd, buf, strlen(buf), 0, &serveraddr, serverlen);
            if (n < 0) {
                error("ERROR in sendto");
            }
            close(sockfd);
            printf("Exiting\n");
            break;
        } else {
            printf("Invalid command\n");
        }
        
    }
    return 0;
}
