#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

void send_large_message (int sock, char *packet, int message_len) {
  int bytes_sent_total = 0;
  int bytes_sent_now = 0;
  char message[256];
  int i = 0;
  memset(message, 0, 256);
  while (bytes_sent_total < message_len) {
    bytes_sent_now = send(sock, &packet[bytes_sent_total], message_len - bytes_sent_total, 0);
    printf("%s - %d\n","sent",bytes_sent_now);
    i++;
    if (bytes_sent_now == -1) {
      printf("Error sending chunk of size %d\n", message_len);
      fflush(stdout);
      break;
    }
    bytes_sent_total += bytes_sent_now;
//    recv(sock, message, 255, 0);
  }
}

void send_packet (int sock, int packet_size) {
  char *packet;
  packet = (char *) malloc(packet_size);
//  if(packet_size > 1<<8){
//    send_large_message(sock,packet,packet_size);
//  }else{
    int x = send(sock, packet, packet_size, 0);
    printf("%s - %d\n","sent",x);

//  }
  free(packet);
}

void send_mul_packets (int sock, int packet_size, int amount) {
  char message[256];
  memset(message, 0, 256);
  int i = 0;
  while (1) {

    i++;
    if (i > amount) { break; }
    send_packet(sock, packet_size);
  }
  send(sock, "FINISHED", strlen("FINISHED"), 0);
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

  send(client_socket, "STARTING", strlen("STARTING"), 0);

  for (int i = 0; i < 21; i++) {
    int packet_size = 1 << i;
    send_mul_packets(client_socket, packet_size, 20);
  }
  send(client_socket, "COMPLETE", strlen("COMPLETE"), 0);



  // Close the connection
//  close(client_socket);

  return 0;
}
