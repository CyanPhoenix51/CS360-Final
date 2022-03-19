#include "mftp.h"

void server(){
	int socketfd, listenfd;
	int numRead;
	struct sockaddr_in servAddr, clientAddr;
	char buf[100] = {0};
	int length = sizeof(struct sockaddr_in);

	memset(&servAddr, 0, sizeof(servAddr));	//set up the server info, port, and address
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(PORT_NUMBER);
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	socketfd = socket(AF_INET, SOCK_STREAM, 0);	//setup the socket
	if(socketfd < 0){
		fprintf(stderr, "Socket Error: %s\n", strerror(errno));
		exit(1);
	}

	if(setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1){
		fprintf(stderr, "Set Sock: %s\n", strerror(errno));
		exit(1);
	}

	if(bind(socketfd, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0){
		fprintf(stderr, "Bind: %s\n", strerror(errno));
		exit(1);
	}
	//listen for a socket to come in to connect
	if(listen(socketfd, BACKLOG) < 0){
		fprintf(stderr, "Listen: %s\n", strerror(errno));
		exit(1);
	}

	//while loop is essentially the parent process
	while(1){
		//wait for a client to connect
		listenfd = accept(socketfd, (struct sockaddr *) &clientAddr, &length);
		if(listenfd < 0){
			fprintf(stderr, "Accept: %s\n", strerror(errno));
			break;
		}
		//get info on the client that is connecting
		char hostName[NI_MAXHOST];
		int hostEntry = getnameinfo((struct sockaddr *) &clientAddr, sizeof(clientAddr), hostName, sizeof(hostName), NULL, 0, NI_NUMERICSERV);
		if(hostEntry != 0){
			fprintf(stderr, "Error: %s\n", gai_strerror(hostEntry));
			break;
		}

		int cpid = fork();
		//child process(es)
		if(cpid == 0){
			printf("Child %d: Connected!\n", getpid());
			while(1){
				char buf[1] = {0};
				char clientCommand[BUFFER] = {0};
				int numRead;
				while((numRead = read(listenfd, buf, 1)) > 0){	//read in a D, C, or Q
					strncat(clientCommand, buf, 1);
					if(buf[0] == '\n')
						break;
				}
				if(numRead < 0){
					fprintf(stderr, "Num Read Error: %s\n", strerror(errno));
					exit(1);
				}
				if(strcmp(clientCommand, "D\n") == 0){			//when a D\n is read in
					int dataSocketfd, datafd;					//setup datafd
					struct sockaddr_in dataServAddr, dataClientAddr;
					int dataLength = sizeof(struct sockaddr_in);
					char acceptPort[BUFFER] = {0};

					memset(&dataServAddr, 0, sizeof(dataServAddr));	//set up the server info, port, and address
					dataServAddr.sin_family = AF_INET;
					dataServAddr.sin_port = 0;
					dataServAddr.sin_addr.s_addr = htonl(INADDR_ANY);

					dataSocketfd = socket(AF_INET, SOCK_STREAM, 0);

					//anything errors we need to send an 'E\n'
					if(dataSocketfd < 0){
						sprintf(acceptPort, "E%s\n", strerror(errno));
					}
					if(setsockopt(dataSocketfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1){
						sprintf(acceptPort, "E%s\n", strerror(errno));
					}

					if(bind(dataSocketfd, (struct sockaddr *) &dataServAddr, sizeof(dataServAddr)) < 0){
						sprintf(acceptPort, "E%s\n", strerror(errno));
					}
					if(getsockname(dataSocketfd, (struct sockaddr *) &dataServAddr, &dataLength) == -1){
						sprintf(acceptPort, "E%s\n", strerror(errno));
					}
					if(listen(dataSocketfd, 1) == -1){
						sprintf(acceptPort, "E%s\n", strerror(errno));
					}
					if(acceptPort[0] != 'E'){
						sprintf(acceptPort, "A%d\n", ntohs(dataServAddr.sin_port));
					}
					write(listenfd, acceptPort, strlen(acceptPort));	//send Eerror or Aport
					if(acceptPort[0] == 'E'){
						printf("Child %d: %s\n", getpid(), acceptPort);
						continue;
					}

					memset(clientCommand, 0, sizeof(clientCommand));

					//on Aport we can accept
					datafd = accept(dataSocketfd, (struct sockaddr *) &dataClientAddr, &dataLength);
					while((numRead) = read(listenfd, buf, 1) > 0){
						strncat(clientCommand, buf, 1);
						if(buf[0] == '\n'){
							break;
						}
					}
					if(numRead < 0){
						fprintf(stderr, "Couldn't get command: %s\n", strerror(errno));
						exit(1);
					}
					char *copyCommand = strdup(clientCommand);
					if(strcmp(copyCommand, "L\n") == 0){	//L\n command was sent
						int status;
						int cpid = fork();
						if(cpid){
							close(datafd);
							close(dataSocketfd);
							wait(&status);
						}
						else{
							close(1);dup(datafd);close(datafd);
							execlp("ls", "ls", "-l", "-a", (char *) NULL);	//list the files in the server on the client
						}
						if(status != 0){
							char *E = "Eexec failed!\n";
							write(listenfd, E, strlen(E));
						}else if(status == 0){
							char *A = "A\n";
							write(listenfd, A, strlen(A));
						}
					}
					else if(copyCommand[0] == 'G'){		//G<pathname> was sent
						struct stat path_stat;
						char *path = strtok(copyCommand, "G");
						path[strlen(path)-1] = '\0';		//grab <pathname>
						lstat(path, &path_stat);
						if(!S_ISREG(path_stat.st_mode) && !access(path, R_OK)){	//check access
							char gError[BUFFER];
							sprintf(gError, "E%s\n", strerror(errno));
							write(listenfd, gError, strlen(gError));
							printf("Child %d: %s\n", getpid(), gError);
							continue;
						}
						int fd;
						if((fd = open(path, O_RDONLY)) < 0){
							char oError[BUFFER];
							sprintf(oError, "E%s\n", strerror(errno));
							write(listenfd, oError, strlen(oError));
							continue;
						}
						char *A = "A\n";		//we send A\n we can send th contents of the file
						write(listenfd, A, strlen(A));
						char getBUF[BUFFER];
						printf("Child %d: Reading file %s\n", getpid(), path);
						printf("Child %d: transmitting file %s to client\n", getpid(), path);
						while((numRead = read(fd, getBUF, BUFFER)) > 0){
							write(datafd, getBUF, numRead);
						}
						if(numRead < 0){
							fprintf(stderr, "Get Send Error: %s\n", strerror(errno));
							exit(1);
						}
						close(datafd);
						close(dataSocketfd);
						close(fd);
					}
					else if(copyCommand[0] == 'P'){	//P<pathname> read in
						char *path = strtok(copyCommand, "P");
						path[strlen(path) - 1] = '\0';
						char *file;
						while(path != NULL){		//grab <pathname>
							file = strdup(path);
							path = strtok(NULL, "/");
						}
						int fd = open(file, O_RDWR | O_CREAT | O_EXCL, 0700);
						if(fd == -1)
							fprintf(stderr, " File Error: %s\n", strerror(errno));
						else{			//we were able to create the file
							char *A = "A\n";
							write(listenfd, A, strlen(A));
							char putBUF[BUFFER];
							printf("Child %d: Writing file %s\n", getpid(), file);
							printf("Child %d: receiving file %s from client\n", getpid(), file);
							while((numRead = read(datafd, putBUF, BUFFER)) > 0){
								write(fd, putBUF, numRead);
							}
							if(numRead > 0){
								char pError[BUFFER];
								printf("E%s\n", strerror(errno));
								sprintf(pError, "E%s\n", strerror(errno));
								unlink(file);
								write(listenfd, pError, strlen(pError));
							}
						close(fd);
						close(datafd);
						close(dataSocketfd);
						}
					}
				}
				else if(clientCommand[0] == 'C'){	//C<pathname> was read in
					struct stat path_stat;
					char *path = strtok(clientCommand, "C");	//grab <pathname>
					path[strlen(path) - 1] = '\0';
					lstat(path, &path_stat);
					//check access of <pathname>
					if(S_ISDIR(path_stat.st_mode) && access(path, R_OK) == 0){
						chdir(path);
						printf("Child %d: Changed current directory to %s\n", getpid(), path);
						char *A2 = "A\n";
						write(listenfd, A2, strlen(A2));
					}
					else{
						char cdError[BUFFER];
						sprintf(cdError, "E%s\n", strerror(errno));
						write(listenfd, cdError, strlen(cdError));
					}
				}
				else if(strcmp(clientCommand, "Q\n") == 0){	//Q\n was read in
					char *accept = "A\n";					//send A that so we can close shop on the client
					write(listenfd, accept, strlen(accept));
					printf("Child %d: Quitting\n", getpid());
					break;
				}
			
			}
		}
		while(waitpid(0, NULL, WNOHANG) > 0);	//clean up zombies
	}
}

int main(int argc, char *argv[]){
	server();
}