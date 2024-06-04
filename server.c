#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 8888  // The port number on which the server will listen

int main() {
    printf("%s","starting");
    int server_fd, new_socket, valread;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[1<<20] = {0};  // Buffer to store incoming data from the client
    char *hello = "Hello from server";  // Message to send back to the client

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;  // Address family: IPv4
    address.sin_addr.s_addr = INADDR_ANY;  // Bind to any available network interface
    address.sin_port = htons(PORT);  // Convert the port number to network byte order

    // Forcefully attaching socket to the port 8888
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listening for incoming connections
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // Accepting incoming connection
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }
    
    while (strcmp(buffer,"END") == 0){
        valread = read(new_socket, buffer, 1 << 20);
        if (strcmp(buffer,"MESSAGE COMPLETE") == 1){
            printf("Received: %s\n", buffer);
            send(new_socket, "RECEIVED", strlen("RECEIVED"), 0);
        }    
    }
    
    // Reading data from the client
    printf("Received: %s\n", buffer);

    // Sending a response back to the client
    send(new_socket, hello, strlen(hello), 0);
    printf("Hello message sent\n");

    // Closing the connected socket
    close(new_socket);
    // Closing the listening socket
    shutdown(server_fd, SHUT_RDWR);
    return 0;
}