#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h> 
#include <ctype.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>

#define SERVER_PORT 11111
#define MAXLINE 4096
#define SA struct sockaddr

void* handleConnection(void *arg);				// Function for handling HTTP request on opened port

void sigHandle(int sigNum);						// Function for handling error signals (NOT FUNCTIONAL)

int main(int argc, char* argv[]) {

	signal(SIGPIPE, sigHandle);					// signal() for SIGPIPE errors if client closes socket during function execution (NOT FUNCTIONAL)

	int BACKLOG = 100;
 
	if (argc != 2) {
		printf("ERROR: incorrect number of arguments provided.\n");
		return -1;
	}
	int port = atoi(argv[1]);
	if (port< 1024 || port > 65535) {
		printf("ERROR: TCP port invalid.\n");
		return -1;
	}

	int server_socket = socket(AF_INET, SOCK_STREAM, 0);									// create server socket

	struct sockaddr_in server_sa;
    memset(&server_sa, 0, sizeof(server_sa));
    server_sa.sin_family = AF_INET;
    server_sa.sin_port = htons(port);
    server_sa.sin_addr.s_addr = htonl(INADDR_ANY);  										// listen on any network device.


    if (bind(server_socket, (struct sockaddr *) &server_sa, sizeof(server_sa)) == -1) {
        printf("Error: bind()\n");
        return EXIT_FAILURE;
    }

    // configure socket's listen queue
    if (listen(server_socket, BACKLOG) == -1) {
        printf("Error: listen()\n");
        return EXIT_FAILURE;
    }

    // printf("listening on port: %d\n\n", port);
	

	for (;;) {																				// infinitely listen for requests.
		struct sockaddr_in client_sa;
        socklen_t client_sa_len;
        char client_addr[INET_ADDRSTRLEN];

        int *client_socket = (int*) malloc(sizeof(int));									// malloc int for client socket to enable rapid requests and not overlap current processes.
        *client_socket = accept(server_socket, (struct sockaddr *) &client_sa, &client_sa_len);		// wait for and accept any requests on the opened port. 
		
        inet_ntop(AF_INET, &(client_sa.sin_addr), client_addr, INET_ADDRSTRLEN);  			// convert client IP address into printable string
        // printf("client_socket: %d (%s:%d)\n", client_socket, client_addr, ntohs(client_sa.sin_port));

        pthread_t client_thread;															// create client socket variable to use for recieving HTTP request
        if (pthread_create(&client_thread, NULL, handleConnection, client_socket) != 0) {			// attempt to create thread and run handleConnection, 
			char* ERR = "HTTP/1.1 500 Internal Error\r\n";											// if pThread can not be created send error and exit
			printf("ERROR PTHREAD CREATE: %s\n", ERR);
			send(*client_socket, ERR, strlen(ERR), 0);
			close(*client_socket);
		}
        pthread_detach(client_thread); // when a detached thread terminates, its resources are automatically released without a join

	}
	return 0;
}


void* handleConnection(void *param) {

	int MAXLEN = 1000;

    int client_socket = *((int *) param);													// cast provided void pointer to an int pointer
    char buf[MAXLEN+1];																		// initalize buffer for receiving data from client socket
    memset(buf, 0, MAXLEN+1);
    ssize_t bytes_read = recv(client_socket, buf, sizeof(buf), 0);							// receive HTTP request
    if (bytes_read <= 0) {																	// if non-valid amount of data is read, report error and exit
		char* ERR = "HTTP/1.1 500 Internal Error\r\n\r\n";
        printf("Error reading client request: %s\n", ERR);
		send(client_socket, ERR, strlen(ERR), 0);
        close(client_socket);
        return NULL;
    }
    buf[bytes_read-2] = '\0';  // remove CR/NL
    printf("\n\nreceived: %s(%lu)\n\n", buf, bytes_read);


	char *header = strtok(buf, "\r");														// tokenize buffer to get the important header information

	char* httpFun = strtok(header, " ");													// tokenize header to get the HTTP function (HEAD / GET)
	char* reqFile = strtok(NULL, " ");														// tokenize header again to get the requested file

	if (reqFile == NULL) {																	// if token is NULL, no file was provided, send error and exit
		char* ERR = "HTTP/1.1 400 Bad Request\r\n\r\n";
		printf("INCORRECTLY FORMATTED FILE: %s\n", ERR);
		send(client_socket, ERR, strlen(ERR), 0);
		close(client_socket);
		free(param);
		return NULL;	
	}

	if (reqFile[0] != '/') {																// if file is tokened, but does not start with '/' send error and exit
		char* ERR = "HTTP/1.1 400 Bad Request\r\n\r\n";
		printf("INCORRECTLY FORMATTED FILE: %s\n", ERR);
		send(client_socket, ERR, strlen(ERR), 0);
		close(client_socket);
		free(param);
		return NULL;
	}

	if (strstr(reqFile, "..") != NULL) {													// if requested file contains '..' do not allow upwards traversal, send error and exit
		char* ERR = "HTTP/1.1 404 Not Found\r\n\r\n";
		printf("INCORRECTLY FORMATTED FILE: %s\n", ERR);
		send(client_socket, ERR, strlen(ERR), 0);
		close(client_socket);
		free(param);
		return NULL;
	}

	reqFile++;																				// iterate character pointer by 1 to remove the '/' infront of the file name
	char* httpVer = strtok(NULL, " ");														// tokenize the header again to confirm it contains 'HTTP/1.1'

	// printf("FUNCTION: %s\n\n", httpFun);
	// printf("FILE: %s\n\n", reqFile);
	// printf("VERSION: %s\n\n", httpVer);

	if (strcmp(httpVer, "HTTP/1.1") != 0) {													// if the last part does not contain 'HTTP/1.1', send error and fail
		char* ERR = "HTTP/1.1 400 Bad Request\r\n\r\n";
		printf("INCORRECTLY FORMATTED FILE: %s\n", ERR);
		send(client_socket, ERR, strlen(ERR), 0);
		close(client_socket);
		free(param);
		return NULL;
	}





// =============================== GET =============================== 
	if (strcmp(httpFun, "GET") == 0) {
		char RET[MAXLEN];

		if (strncmp(reqFile, "delay", 5) == 0) {											// DELAY 			>curl -v http://localhost:12357/delay/3

			reqFile+=6;																		// increment character pointer by 6 to get the integer value to delay by
			int delayTime = atoi(reqFile);													// convert char delay to be an int to use in sleep()
			sleep(delayTime);
			snprintf(RET, MAXLEN, "HTTP/1.1 200 OK\r\n\r\n");
			printf("%s", RET);
			send(client_socket, RET, strlen(RET), 0);
			close(client_socket);
			free(param);
			return NULL;
		}

		else {																				// REGULAR GET 		>curl -v http://localhost:12357/file.txt
			FILE *fp;
			fp = fopen(reqFile, "r");														// open requested file
			if (fp == NULL) {																// File not found
				char* ERR = "HTTP/1.1 404 Not Found\r\n\r\n";
				printf("FILE NOT FOUND: %s\n", ERR);
				send(client_socket, ERR, strlen(ERR), 0);
				close(client_socket);
				free(param);
				return NULL;
			}
			else {																			// file is found, print and send contents after HTTP/1.1 200 OK
				struct stat fileStat;
				stat(reqFile, &fileStat);
				snprintf(RET, MAXLEN, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n", fileStat.st_size);		// print and send content information
				printf("%s", RET);
				send(client_socket, RET, strlen(RET), 0);
				char line[MAXLINE];
				while (fgets(line, MAXLINE, fp) != NULL) {									// for all lines in the file, send the line to the client socket
					printf("%s", line);
					send(client_socket, line, strlen(line), 0);
				}
				fclose(fp);
				close(client_socket);
				free(param);
				return NULL;
			}
		}
	}




	// =============================== HEAD =============================== 
	else if (strcmp(httpFun, "HEAD") == 0) {												// HEAD: 			>curl -I http://localhost:12357/file.txt
		FILE *fp;
		fp = fopen(reqFile, "r");															// open requested file
		if (fp == NULL) {																	// File not found
			char* ERR = "HTTP/1.1 404 Not Found\r\n\r\n";
			printf("FILE NOT FOUND: %s\n", ERR);
			send(client_socket, ERR, strlen(ERR), 0);
			close(client_socket);
			free(param);
			return NULL;
		}
		else {																				// file is found, print and send HTTP/1.1 200 OK
			struct stat fileStat;
			stat(reqFile, &fileStat);
			// printf("HEAD FUNCTION\n");
			char RET[MAXLEN];
			snprintf(RET, MAXLEN, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n", fileStat.st_size);		// print and send file information
			printf("%s\n", RET);
			send(client_socket, RET, strlen(RET), 0);
			close(client_socket);
			fclose(fp);
			free(param);
			return NULL;
		}
	}




	// =============================== OTHER FUNCTION =============================== 
	else {																					// function not implemented													
		char* ERRNOTIMPL = "HTTP/1.1 501 Not Implemented\r\n\r\n";
		printf("NOT IMPLEMENTED: %s\n", ERRNOTIMPL);
		send(client_socket, ERRNOTIMPL, strlen(ERRNOTIMPL), 0);
		close(client_socket);
		free(param);
		return NULL;
	}

	free(param);
	return NULL;
}


void sigHandle(int sigNum) {
	printf("SIGNAL NUMBER %d DETECTED, ABORTING...\n", sigNum);
	exit(1);

}