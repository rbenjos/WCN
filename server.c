#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

int main(void)
{
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(8080); // Port 8080
    address.sin_addr.s_addr = INADDR_ANY;

    // Bind server_socket to address
    bind(server_socket, (struct sockaddr *) &address, sizeof(address));

    // Listen for clients and allow the accept function to be used
    // Allow 4 clients to be queued while the server processes
    listen(server_socket, 4);

    // Wait for client to connect, then open a socket
    int client_socket = accept(server_socket, NULL, NULL);

    char message[256];
    memset(message, 0, 256);

    // Send message to the client
    int i = 0;
    while(1){
        while (recv(client_socket, message, 255, 0) > 0){
            printf("%s\n", message);
            fflush(stdout);
            send(client_socket, i, strlen(str(i)), 0);
            i++;
        }
    }
    
    // Close the client socket
    close(client_socket);

    return 0;
}
