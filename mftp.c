#include "mftp.h"

void client(char *ipAddr){
	int socketfd;
	struct addrinfo hints, *actualdata;
	int numRead, err;

	memset(&hints, 0, sizeof(hints));	//setup the type of socket we will be using
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_INET;

	err = getaddrinfo(ipAddr, strPort, &hints, &actualdata);	//get address of the port
	if(err != 0){
		fprintf(stderr, "Error: %s\n", gai_strerror(err));
		exit(1);
	}

	socketfd = socket(actualdata->ai_family, actualdata->ai_socktype, 0);	//setup the socket

	if(connect(socketfd, actualdata->ai_addr, actualdata->ai_addrlen) < 0){	//connect the socket
		fprintf(stderr, "%s\n", strerror(errno));
		exit(1);
	}
	printf("Connected to %s\n", ipAddr);
	while(1){							//main loop
		char servRespond[BUFFER] = {0};	//setup chars
		char inputCommand[BUFFER] = {0};
		char buf[1] = {0};
		write(STDOUT_FILENO, "MFTP>", sizeof("MFTP>"));
		while((numRead = read(STDOUT_FILENO, buf, 1)) > 0){	//get command
			strncat(inputCommand, buf, 1);
			if(buf[0] == '\n')
				break;
		}
		if(numRead < 0){
			printf("Command Error: %s\n", strerror(errno));
			exit(1);
		}

		char *copyCommand = strdup(inputCommand);		//copy command
		char *splitInput = strtok(copyCommand, " ");	//grab the actuall command

		if(strcmp(splitInput, "exit\n") == 0){			//when exit is read
			char *Q = "Q\n";
			write(socketfd, Q, strlen(Q));				//Tell server we are quitting
			while((numRead = read(socketfd, buf, 1)) > 0){
				strncat(servRespond, buf, 1);
				if(buf[0] == '\n')
					break;
			}
			if(numRead < 0){
				printf("Exit Error: %s", strerror(errno));
				exit(1);
			}
			if(servRespond[0] == 'A'){					//if server responds with an A
				close(socketfd);
				free(actualdata);
				exit(0);
			}
		}
		else if(strcmp(splitInput, "cd") == 0){			//local cd command
			splitInput = strtok(NULL, " \n");
			struct stat path_stat;
			lstat(splitInput, &path_stat);
			//check if we can access
			if(S_ISDIR(path_stat.st_mode) && access(splitInput, R_OK) == 0){
				chdir(splitInput);
			}
			else{
				fprintf(stderr, "Access: %s\n", strerror(errno));
			}

		}
		else if(strcmp(splitInput, "rcd") == 0){		//remote cd command
			splitInput = strtok(NULL, " ");
			splitInput[strlen(splitInput)] = '\0';
			write(socketfd, "C", 1);					//send C<pathname>
			write(socketfd, splitInput, strlen(splitInput));
			while((numRead = read(socketfd, buf, 1)) > 0){	//read in the response
				strncat(servRespond, buf, 1);
				if(buf[0] == '\n'){
					break;
				}
			}
			if(numRead < 0){
				printf("RCD Recieve Error: %s\n", strerror(errno));
				exit(1);
			}
			if(servRespond[0] == 'E')
				printf("%s", servRespond);
		}
		else if(strcmp(splitInput, "ls\n") == 0){	//local ls command
			int status;
			if(fork()){
				wait(&status);
			}
			else{
				int fd[2];
				int rdr, wtr;
				int wStatus;

				if(pipe(fd) < 0){					//pipe the lines
					printf("%s\n", strerror(errno));
					fflush(stdout);
				}

				rdr = fd[0]; wtr = fd[1];
				if(fork()){
					close(wtr);
					close(0); dup(rdr); close(rdr);
					wait(&wStatus);
					execlp("more", "more", "-d", "-20", (char *) NULL);	//be able to control how many lines
				}
				else{
					close(rdr);
					close(1); dup(wtr); close(wtr);
					execlp("ls", "ls", "-l", "-a", (char *) NULL);		//ls the directory
				}
			}
		}
		else if(strcmp(splitInput, "rls\n") == 0){		//remote ls command
			char *D = "D\n";
			write(socketfd, D, strlen(D));
			while((numRead = read(socketfd, buf, 1)) > 0){	//read response from server
				strncat(servRespond, buf, 1);
				if(buf[0] == '\n'){
					break;
				}
			}
			if(numRead < 0){
				printf("RLS Receive Error: %s\n", strerror(errno));
				exit(1);
			}
			if(servRespond[0] == 'A'){				//if Aport is recieved
				struct addrinfo dHints, *dActualData;

				memset(&dHints, 0, sizeof(dHints));	//setup the data connection
				dHints.ai_socktype = SOCK_STREAM;
				dHints.ai_family = AF_INET;

				char *port = strtok(servRespond, "A\n");
				err = getaddrinfo(ipAddr, port, &dHints, &dActualData);
				if(err != 0){
					fprintf(stderr, "Err: %s\n", gai_strerror(err));
					fflush(stderr);
				}
				//the datafd that will send the data thru
				int datafd = socket(dActualData->ai_family, dActualData->ai_socktype, 0);
				if(connect(datafd, dActualData->ai_addr, dActualData->ai_addrlen) < 0){
					fprintf(stderr, "Connect Error: %s\n", strerror(errno));
					fflush(stderr);
				}
				
				char *L = "L\n";				//send the command to ls the server
				write(socketfd, L, strlen(L));
				memset(servRespond, 0, sizeof(servRespond));
				numRead = read(socketfd, servRespond, BUFFER);
				if(numRead < 0){
					fprintf(stderr, "%s\n", strerror(errno));
					exit(1);
				}
				int status;
				if(servRespond[0] == 'A'){		//we get a A response back that mean we have a list of lines to output
					if(fork()){
						wait(&status);
						close(datafd);
						free(dActualData);
					}
					else{
						close(0); dup(datafd); close(datafd);
						execlp("more", "more", "-d", "-20", (char *) NULL);
					}
				}
				else if(servRespond[0] = 'E'){	//we got an error response in return
					fprintf(stderr, "RLS error: %s\n", servRespond);
				}
			}
			else if(servRespond[0] == 'E'){
				fprintf(stderr, "RLS D response Error: %s\n", servRespond);
			}

		}
		else if(strcmp(splitInput, "get") == 0){ //get function to put in local directory
			char *D = "D\n";
			write(socketfd, D, strlen(D));		//send a D to setup data connection
			while((numRead = read(socketfd, buf, 1)) > 0){
				strncat(servRespond, buf, 1);
				if(buf[0] == '\n'){
					break;
				}
			}
			if(numRead < 0){
				fprintf(stderr, "Couldn't send D: %s\n", strerror(errno));
				exit(1);
			}
			splitInput = strtok(NULL, " ");
			splitInput[strlen(splitInput)] = '\0';
			if(servRespond[0] == 'A'){			//we can successfully setup the data connection
				struct addrinfo dHints, *dActualData;

				memset(&dHints, 0, sizeof(dHints));	//setup data connection
				dHints.ai_socktype = SOCK_STREAM;
				dHints.ai_family = AF_INET;

				char *port = strtok(servRespond, "A\n");
				err = getaddrinfo(ipAddr, port, &dHints, &dActualData);
				if(err != 0){
					fprintf(stderr, "Get Addr Error: %s\n", gai_strerror(err));
				}
				int datafd = socket(dActualData->ai_family, dActualData->ai_socktype, 0);
				if(connect(datafd, dActualData->ai_addr, dActualData->ai_addrlen) < 0){
					fprintf(stderr, "Connect Error: %s\n", strerror(errno));
				}
				write(socketfd, "G", 1);						//send G<pathname>
				write(socketfd, splitInput, strlen(splitInput));

				numRead = read(socketfd, servRespond, BUFFER);	//read response
				if(numRead < 0){
					fprintf(stderr, "Couldn't grab Get Response Error: %s\n", strerror(errno));
					exit(1);
				}
				if(servRespond[0] == 'A'){
					char *ptr = strtok(splitInput, "/");	//grab the file name
					char *file;
					while(ptr != NULL){
						file = strdup(ptr);
						ptr = strtok(NULL, "/");
					}
					file[strlen(file) - 1] = '\0';
					int fd = open(file, O_RDWR | O_CREAT | O_EXCL, 0700);	//create file
					if(fd == -1)
						fprintf(stderr, " File Error: %s\n", strerror(errno));
					else{
						char getBUF[BUFFER];			//read in the data
						while((numRead = read(datafd, getBUF, BUFFER)) > 0){
							write(fd, getBUF, numRead);
						}
						if(numRead > 0){
							fprintf(stderr, "%s\n", strerror(errno));
							unlink(file);
						}
						close(fd);
					}
				}
				else if(servRespond[0] == 'E'){
					fprintf(stderr, "Get Command Error: %s\n", servRespond);
				}
				close(datafd);
				free(dActualData);
			}
			else if(servRespond[0] == 'E'){
				fprintf(stderr, "D Command Error: %s\n", servRespond);
			}
		}
		else if(strcmp(splitInput, "show") == 0){		//show remote file on client command
			char *D = "D\n";
			write(socketfd, D, strlen(D));				//send D
			while((numRead = read(socketfd, buf, 1)) > 0){
				strncat(servRespond, buf, 1);
				if(buf[0] == '\n'){
					break;
				}
			}
			if(numRead < 0){
				fprintf(stderr, "Couldn't send D: %s\n", strerror(errno));
				exit(1);
			}
			splitInput = strtok(NULL, " ");
			splitInput[strlen(splitInput)] = '\0';
			if(servRespond[0] == 'A'){
				struct addrinfo dHints, *dActualData;

				memset(&dHints, 0, sizeof(dHints));	//setup the type of socket we will be using
				dHints.ai_socktype = SOCK_STREAM;
				dHints.ai_family = AF_INET;

				char *port = strtok(servRespond, "A\n");
				err = getaddrinfo(ipAddr, port, &dHints, &dActualData);
				if(err != 0){
					fprintf(stderr, "Err: %s\n", gai_strerror(err));
					fflush(stderr);
				}
				int datafd = socket(dActualData->ai_family, dActualData->ai_socktype, 0);
				if(connect(datafd, dActualData->ai_addr, dActualData->ai_addrlen) < 0){
					fprintf(stderr, "Connect Error: %s\n", strerror(errno));
					fflush(stderr);
				}

				write(socketfd, "G", 1);			//Send G<pathname>
				write(socketfd, splitInput, strlen(splitInput));

				numRead = read(socketfd, servRespond, BUFFER);	//get response
				if(numRead < 0){
					fprintf(stderr, "Show response error: %s\n", strerror(errno));
					exit(1);
				}
				int status;
				if(servRespond[0] == 'A'){
					if(fork()){
						wait(&status);
						close(datafd);
						free(dActualData);
					}
					else{
						close(0); dup(datafd); close(datafd);
						execlp("more", "more", "-20", (char *) NULL);	//more the lines of the file
					}
				}
			}
			else if(servRespond[0] == 'E'){
				fprintf(stderr, "Show D Command Error: %s\n", servRespond);
			}
		}
		else if(strcmp(splitInput, "put") == 0){	//Put command
			splitInput = strtok(NULL, " ");
			splitInput[strlen(splitInput)] = '\0';
			
			struct stat path_stat;
			lstat(splitInput, &path_stat);
			//check access of directory
			if(!S_ISREG(path_stat.st_mode) && access(splitInput, R_OK) == 0){
				fprintf(stderr, "Access Error: %s\n", strerror(errno));
				continue;
			}
			char *D = "D\n";	//send the D command
			write(socketfd, D, strlen(D));

			while((numRead = read(socketfd, buf, 1)) > 0){	//server response
				strncat(servRespond, buf, 1);
				if(buf[0] == '\n'){
					break;
				}
			}
			if(numRead < 0){
				fprintf(stderr, "Couldn't send D: %s\n", strerror(errno));
				exit(1);
			}
			if(servRespond[0] == 'A'){				//server responses with A
				struct addrinfo dHints, *dActualData;

				memset(&dHints, 0, sizeof(dHints));	//setup the type of socket we will be using
				dHints.ai_socktype = SOCK_STREAM;
				dHints.ai_family = AF_INET;

				char *port = strtok(servRespond, "A\n");
				err = getaddrinfo(ipAddr, port, &dHints, &dActualData);
				if(err != 0){
					fprintf(stderr, "Error: %s\n", gai_strerror(err));
					fflush(stderr);
				}
				int datafd = socket(dActualData->ai_family, dActualData->ai_socktype, 0);
				if(connect(datafd, dActualData->ai_addr, dActualData->ai_addrlen) < 0){
					perror("Error: ");
				}
				
				write(socketfd, "P", 1);		//send P<pathname>
				write(socketfd, splitInput, strlen(splitInput));

				numRead = read(socketfd, servRespond, BUFFER);
				if(numRead == -1){
					fprintf(stderr, "%s\n", strerror(errno));
				}

				if(servRespond[0] == 'A'){		//on Aport response
					char *ptr = strtok(splitInput, "/");
					char *file;
					while(ptr != NULL){
						file = strdup(ptr);
						ptr = strtok(NULL, "/");
					}
					file[strlen(file) - 1] = '\0';
					int fd = open(file, O_RDONLY);
					char putBUF[BUFFER];		//send the contents of the file thru the datafd
					while((numRead = read(fd, putBUF, BUFFER)) > 0){
						write(datafd, putBUF, numRead);
					}
					if(numRead < 0){
						fprintf(stderr, "Put socket Closed: %s\n", strerror(errno));
						exit(1);
					}
					close(fd);
				}
				else if(servRespond[0] == 'E'){
					write(STDOUT_FILENO, servRespond, BUFFER);
				}
				close(datafd);
				free(dActualData);
			}
			else if(servRespond[0] == 'E'){
				fprintf(stderr, "Put Command Error: %s\n", servRespond);
			}
		}
		else{		//unknown command typed into the input
			printf("Command '%s' is unknown - ignored\n", splitInput);
		}
		free(copyCommand);
	}
}

int main(int argc, char *argv[]){
	client("localhost");
}