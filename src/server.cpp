#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT 4221

size_t recv_line(int fd, std::string &result)
{
  result.clear();

  while (true)
  {
    char value = 0;

    if (recv(fd, &value, 1, 0) != 1)
    {
      return (std::string::npos);
    }

    if (value == '\n')
    {
      break;
    }

    result += value;
  }

  if (!result.empty() && result.back() == '\r')
  {
    result.erase(result.end() - 1);
  }

  return (result.size());
}

enum class Method
{
  UNKNOWN,
  GET,
  POST,
};

Method
method_parse(const std::string &input)
{
  if ("GET" == input)
    return (Method::GET);

  if ("POST" == input)
    return (Method::POST);

  return (Method::UNKNOWN);
}

struct Request
{
  Method method;
  std::string path;
};

Request request_parse(int client_fd)
{
  Request request;

  std::string line;
  recv_line(client_fd, line);

  {
    const std::string delimiter = " ";

    size_t offset = line.find(delimiter);
    request.method = method_parse(line.substr(0, offset));
    line.erase(0, offset + delimiter.length());

    offset = line.find(delimiter);
    request.path = line.substr(0, offset);
  }

  return (request);
}

int main(int argc, char **argv)
{
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0)
  {
    perror("socket");
    return (EXIT_FAILURE);
  }

  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
  {
    perror("setsockopt");
    return (EXIT_FAILURE);
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(PORT);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
  {
    perror("bind");
    return (EXIT_FAILURE);
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0)
  {
    perror("listen");
    return (EXIT_FAILURE);
  }

  std::cout << "listen " << PORT << std::endl;

  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);

  std::cout << "Waiting for a client to connect..." << std::endl;

  int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
  std::cout << "Client connected" << std::endl;

  Request request = request_parse(client_fd);

  if (request.path == "/")
  {
    const char *response = "HTTP/1.1 200 OK\r\n\r\n";
    send(client_fd, response, strlen(response), 0);
  }
  else
  {
    const char *response = "HTTP/1.1 404 OK\r\n\r\n";
    send(client_fd, response, strlen(response), 0);
  }

  close(server_fd);
  close(client_fd);

  return (EXIT_SUCCESS);
}
