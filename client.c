#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>


void send_packet (int sock, int packet_size) {
  char *packet;
  packet = (char *) malloc(packet_size);
  int bytes_sent = send(sock, packet, packet_size, 0);
//  printf("%s - %d\n","sent",bytes_sent);
  free(packet);
}

void send_mul_packets (int sock, int packet_size, int amount) {
  char message[256];
  memset(message, 0, 256);
  for(int i = 0; i < amount; i++) {
    send_packet(sock, packet_size);
    recv(sock, message, 256, 0);
  }
  send(sock, "FINISHED", strlen("FINISHED"), 0);
//  printf("%s\n","sented");
//  fflush(stdout);
  recv(sock, message, 256, 0);
//  printf("%s\n","not receved");
//  fflush(stdout);
  printf("%s - %d\n", message, packet_size);
  fflush(stdout);
}

int main (void) {
  int client_socket = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_port = htons(8080);

  // Set address to server address
  inet_aton("132.65.164.101", (struct in_addr *) &(address.sin_addr.s_addr));

  // Establish a connection to address on client_socket
  connect(client_socket, (struct sockaddr *) &address, sizeof(address));
  int window_size = 1024 * 1024; // 1 MB
  setsockopt(client_socket, SOL_SOCKET, SO_RCVBUF, &window_size, sizeof(window_size));
  setsockopt(client_socket, SOL_SOCKET, SO_SNDBUF, &window_size, sizeof(window_size));
//  send(client_socket, "STARTING", strlen("STARTING"), 0);

  for (int i = 0; i <= 20; i++) {
    int packet_size = 1 << i;
    send_mul_packets(client_socket, packet_size, 20);
  }
  send(client_socket, "COMPLETE", strlen("COMPLETE"), 0);

    printf("%s\n","I AM COMPLETE!!");

  // Close the connection
  close(client_socket);

  return 0;
}
