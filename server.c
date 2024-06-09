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

    int window_size = 1024 * 1024; // 1 MB
    setsockopt(client_socket, SOL_SOCKET, SO_RCVBUF, &window_size, sizeof(window_size));
    setsockopt(client_socket, SOL_SOCKET, SO_SNDBUF, &window_size, sizeof(window_size));

    char message[1024*1024];
    memset(message, 0, 1024*1024);

    // Send message to the client
    int i = 0;
    char str[20]; // Buffer to hold the converted string

//    recv(client_socket, message, 255, 0);
//    printf("%s\n", message);
//    fflush(stdout);
    
    while (1){
        int bytes_sent = recv(client_socket, message, 1024*1024, 0);
//        printf("%s - %d - %d\n", message, i, bytes_sent);
        fflush(stdout);
        i++;
//        sprintf(str,"%d",i);
        if (strcmp("FINISHED", message) == 0) {
          printf("%s\n", message);
          fflush(stdout);
          send(client_socket, "FINISHED", strlen("FINISHED"), 0);
          continue;
        }
        if (strcmp(message,"COMPLETE") == 0){
          printf("%s\n", message);
          fflush(stdout);
          break;
        }
        send(client_socket, "RECEIVED", strlen("RECEIVED"), 0);
    }

//    printf("%s - %d\n", message, i);
    // Close the client socket
    close(client_socket);

    return 0;
}
