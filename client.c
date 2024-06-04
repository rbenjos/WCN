#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 8888  // The port number to connect to on the server

void send_large_message(int sock, char *packet, int message_len){

int bytes_sent_total = 0;
int bytes_sent_now = 0;
while (bytes_sent_total < message_len)
{
    bytes_sent_now = send(sock, &packet[bytes_sent_total], message_len - bytes_sent_total, 0);
    if (bytes_sent_now == -1)
    {
        printf("Error sending chunk of size %d\n", message_len);
        break;
    }
    bytes_sent_total += bytes_sent_now;
}
}

void send_packet_multiple_times(int sock, char *packet, int packet_size, int send_count) {
    int j;

    for (j = 0; j < send_count; j++) {
        if (packet_size > 2048){
            send_large_message(sock,packet, packet_size);
        }
        else{
		if(send(sock, packet, packet_size, 0) != packet_size) {
            printf("Error sending packet of size %d\n", packet_size);
            return;
        }}
    }

    send(sock,"MESSAGE COMPLETE",strlen("MESSAGE COMPLETE"),0);	
    printf("Sent %d packets of size %d bytes\n", send_count, packet_size);
}

void send_packets(int sock, int send_count) {
    int i;
    char *packet;
    int packet_size;
    char buffer[1024] = {0};

    for (i = 0; i < 20; i++) {
        packet_size = 1 << i;  // Calculate packet size as 2^i
        packet = (char *)malloc(packet_size);  // Allocate memory for the packet

        if (packet == NULL) {
            printf("Memory allocation failed\n");
            return;
        }

        send_packet_multiple_times(sock, packet, packet_size, send_count);
        int valread = read(sock, buffer, 1024);
        printf("Received: %s\n", buffer);
    
        free(packet);
        printf("Sent all packets\n");
    }
}

int main() {
    int sock = 0, valread;
    struct sockaddr_in serv_addr;
    char *hello = "Hello from client";  // Message to send to the server
    char buffer[1024] = {0};  // Buffer to store the response from the server

    // Creating socket file descriptor
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;  // Address family: IPv4
    serv_addr.sin_port = htons(PORT);  // Convert the port number to network byte order

    // Convert IPv4 address from text to binary form
    if (inet_pton(AF_INET, "132.65.164.101", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    // Connecting to the server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    // Sending data to the server
    int send_count = 10;
    send_packets(sock, send_count);

    // Reading the response from the server
    valread = read(sock, buffer, 1024);
    printf("Received: %s\n", buffer);

    // Closing the connected socket
    close(sock);
    return 0;
}
