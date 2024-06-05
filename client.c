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
	fflush(stdout);
        break;
    }
    bytes_sent_total += bytes_sent_now;
}
}

void send_packet_multiple_times(int sock, char *packet, int packet_size, int send_count) {
    for (int j = 0; j < send_count; j++) {
        if (packet_size > 2048){
            send_large_message(sock,packet, packet_size);
        }
        else{
		if(send(sock, packet, packet_size, 0) != packet_size) {
            printf("Error sending packet of size %d\n", packet_size);
	    fflush(stdout);
            return;
        }}
    }

    send(sock,"PHASE COMPLETE",strlen("PHASE COMPLETE"),0);	
    printf("Sent %d packets of size %d bytes\n", send_count, packet_size);
    fflush(stdout);
}

void send_packets(int sock, int send_count) {
    char *packet;
    int packet_size;
    char buffer[1024] = {0};

    for (int i = 0; i < 20; i++) {
        packet_size = 1 << i;  // Calculate packet size as 2^i
        packet = (char *)malloc(packet_size);  // Allocate memory for the packet
	
        if (packet == NULL) {
            printf("Memory allocation failed\n");
            return;
        }

	for (int k=0; k<packet_size; k++){
		packet[k]='A';
	}
	    
        send_packet_multiple_times(sock, packet, packet_size, send_count);

	while(1){
	int valread = read(sock, buffer, 1024);
	printf("Received: %s\n", buffer);
	fflush(stdout);
	if (strcmp(buffer,"PHASE COMPLETE") == 0){
		break;
	}
        
	}
        free(packet);
        printf("Sent all packets\n");
    }
}

int main() {
    int sock = 0, valread;
    struct sockaddr_in serv_addr;

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
	
    // Closing the connected socket
    close(sock);
    return 0;
}
