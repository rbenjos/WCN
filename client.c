#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

#define PORT 8080
#define MAX_MESSAGE_SIZE 1024 * 1024 // 1MB
#define NUM_MESSAGES 1000
#define WARM_UP_CYCLES 10

void error(const char *msg) {
  perror(msg);
  exit(1);
}

void send_messages(int sock, size_t message_size) {
  char *buffer = (char *)malloc(message_size);
  memset(buffer, 'A', message_size);

  for (int i = 0; i < NUM_MESSAGES; i++) {
      if (send(sock, buffer, message_size, 0) == -1) {
          error("send failed");
        }
    }

  free(buffer);
}

int main(int argc, char const *argv[]) {
  if (argc != 2) {
      fprintf(stderr, "Usage: %s <server-ip>\n", argv[0]);
      exit(EXIT_FAILURE);
    }

  const char *server_ip = argv[1];
  int sock = 0;
  struct sockaddr_in serv_addr;

  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      error("Socket creation error");
    }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);

  if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
      error("Invalid address/ Address not supported");
    }

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
      error("Connection failed");
    }

  for (int i = 0; i < WARM_UP_CYCLES; i++) {
      send_messages(sock, 1); // Warm-up with 1 byte messages
    }

  for (size_t message_size = 1; message_size <= MAX_MESSAGE_SIZE; message_size *= 2) {
      struct timespec start, end;
      clock_gettime(CLOCK_MONOTONIC, &start);

      send_messages(sock, message_size);

      clock_gettime(CLOCK_MONOTONIC, &end);
      double elapsed_time = end.tv_sec - start.tv_sec + (end.tv_nsec - start.tv_nsec) / 1e9;

      double throughput = (message_size * NUM_MESSAGES) / (elapsed_time * 1024 * 1024); // MB/s

      printf("%zu\t%.2f\tMB/s\n", message_size, throughput);
    }

  close(sock);
  return 0;
}