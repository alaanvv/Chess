#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PRINT(...) { printf(__VA_ARGS__); printf("\n"); }
#define BUFFER_SIZE 1024
#define PORT 8081 

typedef uint8_t u8;
typedef int32_t i32;
typedef char    c8;

u8 amount_connected = 0;
c8 buffer[BUFFER_SIZE];
c8 move_reg[5] = "n0000";

// ---

void _send(i32 req, char* text) {
  PRINT("SENDING (%li) %s", strlen(text), text);
  send(req, text, strlen(text), 0);
  close(req);
}

void main() {
  struct sockaddr_in addr;
  i32 server, req, addrlen = sizeof(addr);

  server = socket(AF_INET, SOCK_STREAM, 0);
  i32 opt = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(PORT);
  bind(server, (struct sockaddr*) &addr, sizeof(addr));
  listen(server, 3);

  while (1) {
    req = accept(server, (struct sockaddr*) &addr, (socklen_t*) &addrlen);

    i32 valread = read(req, buffer, BUFFER_SIZE - 1);
    if (!valread) _send(req, "ERROR");

    if      (buffer[0] == 'c') {
      amount_connected++;
      if      (amount_connected == 1)   _send(req, "W");
      else if (amount_connected == 2) { _send(req, "B"); if (move_reg[0] == 'n') move_reg[0] = 'b'; }
      else                              _send(req, "FULL");
    }
    else if (buffer[0] == 'm') {
      PRINT("SOMEONE MOVED");
      move_reg[0] = buffer[1];
      move_reg[1] = buffer[2];
      move_reg[2] = buffer[3];
      move_reg[3] = buffer[4];
      move_reg[4] = buffer[5];
      _send(req, "OK");
    }
    else if (buffer[0] == 'r') {
      _send(req, move_reg);
    }
    else _send(req, "WHAT");
  }

  close(server);
}
