#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define PORT 8080
#define BUFFER_SIZE 1024 * 1024 // 1MB

void error(const char *msg) {
  perror(msg);
  exit(1);
}

int main() {
  int server_fd, new_socket;
  struct sockaddr_in address;
  int addrlen = sizeof(address);
  char buffer[BUFFER_SIZE] = {0};
  const char *ack = "ACK";

  // Creating socket file descriptor
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
      error("socket failed");
    }

  // Prepare the sockaddr_in structure
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  // Bind the socket to the network address and port
  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
      error("bind failed");
    }

  // Listen for incoming connections
  if (listen(server_fd, 3) < 0) {
      error("listen failed");
    }

  // Accept an incoming connection
  if ((new_socket = accept(server_fd, (struct sockaddr* )&address, (socklen_t*)&addrlen)) < 0) {
      error("accept failed");
    }

  // Continuously read data from the connected client and send acknowledgment
  while (1) {
      long bytes_read = read(new_socket, buffer, BUFFER_SIZE);
      if (bytes_read <= 0) {
          break;
        }
      // Send acknowledgment to the client
      if (send(new_socket, ack, strlen(ack), 0) == -1) {
          error("send ACK failed");
        }
    }

  // Close the sockets
  close(new_socket);
  close(server_fd);
  return 0;
}