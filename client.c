#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

int main(void)
{
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);

    // Set address to server address
    inet_aton("132.65.164.101", (struct in_addr *) &(address.sin_addr.s_addr));

    // Establish a connection to address on client_socket
    connect(client_socket, (struct sockaddr *) &address, sizeof(address));

    char message[256];
    memset(message, 0, 256);

    // Receive max of 255 characters from server (null terminated)
    while(1){
        recv(client_socket, message, 255, 0);
        printf("%s", message);
        fflush(stdout);
    }
    

    // Close the connection
    close(client_socket);

    
    return 0;
}
