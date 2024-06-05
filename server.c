#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 8888  // The port number on which the server will listen

int main() {
    printf("%s\n","starting");
    fflush(stdout);
    int server_fd, new_socket, valread;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[1<<20] = {0};  // Buffer to store incoming data from the client

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed\n");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;  // Address family: IPv4
    address.sin_addr.s_addr = INADDR_ANY;  // Bind to any available network interface
    address.sin_port = htons(PORT);  // Convert the port number to network byte order

    // Forcefully attaching socket to the port 8888
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed\n");
        exit(EXIT_FAILURE);
    }

    // Listening for incoming connections
    if (listen(server_fd, 3) < 0) {
        perror("listen\n");
        exit(EXIT_FAILURE);
    }

    // Accepting incoming connection
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        perror("accept\n");
        exit(EXIT_FAILURE);
    }
    
    while (strcmp(buffer,"EXPERIMENT COMPLETE") != 0){
        valread = read(new_socket, buffer, 1<<20); // Reading data from the client
	printf("Received: %s\n", buffer);
	fflush(stdout);
        if (strcmp(buffer,"PHASE COMPLETE") == 0){
            send(new_socket, "RECEIVED", strlen("RECEIVED"), 0);
        }    
    }
    
    // Sending a response back to the client
    send(new_socket, "EXPERIMENT COMPLETE", strlen("EXPERIMENT COMPLETE"), 0);
    printf("EXPERIMENT COMPLETE, SHUTTING DOWN\n");

    // Closing the connected socket
    close(new_socket);
    shutdown(server_fd, SHUT_RDWR);
    return 0;
}
