#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <map>
#include <vector>
#include <optional>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT 4221

struct CaseInsensitive
{
  bool operator()(const std::string &lhs, const std::string &rhs) const
  {
    return (strcasecmp(lhs.c_str(), rhs.c_str()) < 0);
  }
};

typedef std::map<std::string, std::string, CaseInsensitive> HeaderMap;

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

enum class Status
{
  OK = 200,
  NOT_FOUND = 404,
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
  HeaderMap headers;

  std::string get_user_agent() const
  {
    auto iterator = headers.find("User-Agent");

    if (iterator == headers.end())
      return ("");

    return (iterator->second);
  }
};

struct Response
{
  Status status;
  HeaderMap headers;
  std::optional<std::vector<unsigned char>> body;

  Response(Status status) : status(status) {}
};

std::string status_get_phrase(Status status)
{
  switch (status)
  {
  case Status::OK:
    return ("OK");

  case Status::NOT_FOUND:
    return ("Not Found");

  default:
    return ("");
  }
}

Request request_parse(int client_fd)
{
  Request request;
  std::string line;

  {
    recv_line(client_fd, line);

    const std::string delimiter = " ";

    size_t offset = line.find(delimiter);
    request.method = method_parse(line.substr(0, offset));
    line.erase(0, offset + delimiter.length());

    offset = line.find(delimiter);
    request.path = line.substr(0, offset);
  }

  const std::string delimiter = ": ";
  while (recv_line(client_fd, line) > 0)
  {
    size_t offset = line.find(delimiter);
    std::string key = line.substr(0, offset);
    std::string value = line.substr(offset + delimiter.length());

    request.headers.insert(std::make_pair(key, value));
  }

  return (request);
}

Response response_route(const Request &request)
{
  if (request.path == "/")
  {
    return (Response(Status::OK));
  }

  if (request.path.rfind("/echo/", 0) == 0)
  {
    Response response = Response(Status::OK);
    response.headers["Content-Type"] = "text/plain";
    response.body = std::vector<unsigned char>(request.path.begin() + 6, request.path.end());

    return (response);
  }

  if (request.path == "/user-agent")
  {
    std::string user_agent = request.get_user_agent();

    Response response = Response(Status::OK);
    response.headers["Content-Type"] = "text/plain";
    response.body = std::vector<unsigned char>(user_agent.begin(), user_agent.end());

    return (response);
  }

  if (request.path.rfind("/files/", 0) == 0)
  {
    std::string path = request.path.substr(7);

    std::ifstream stream;
    stream.open(path);

    if (!stream.is_open())
      return Response(Status::NOT_FOUND);

    std::string content(std::istreambuf_iterator<char>{stream}, {});

    stream.close();

    Response response = Response(Status::OK);
    response.headers["Content-Type"] = "text/plain";
    response.body = std::vector<unsigned char>(content.begin(), content.end());

    return (response);
  }

  return (Response(Status::NOT_FOUND));
}

int main(int argc, char **argv)
{
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  for (int index = 0; index < argc; ++index)
  {
    if (strcmp("--directory", argv[index]) == 0)
    {
      ++index;
      if (chdir(argv[index]) == -1)
      {
        perror("chdir");
        return (EXIT_FAILURE);
      }
    }
  }

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

  while (true)
  {
    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
    std::cout << "client connected" << std::endl;

    pid_t pid = fork();
    if (pid == -1)
    {
      perror("fork");
      close(client_fd);
      break;
    }

    if (pid != 0)
    {
      close(client_fd);
      continue;
    }

    Request request = request_parse(client_fd);
    Response response = response_route(request);

    char buffer[512];

    std::string phrase = status_get_phrase(response.status);
    sprintf(buffer, "HTTP/1.1 %d %s\r\n", response.status, phrase.c_str());
    send(client_fd, buffer, strlen(buffer), 0);

    if (response.body.has_value())
      response.headers["Content-Length"] = std::to_string(response.body->size());

    for (auto iterator = response.headers.begin(); iterator != response.headers.end(); ++iterator)
    {
      sprintf(buffer, "%s: %s\r\n", iterator->first.c_str(), iterator->second.c_str());
      send(client_fd, buffer, strlen(buffer), 0);
    }

    send(client_fd, "\r\n", 2, 0);

    if (response.body.has_value())
      send(client_fd, &(*response.body->begin()), response.body->size(), 0);

    close(client_fd);
    return (EXIT_SUCCESS);
  }

  close(server_fd);
  return (EXIT_SUCCESS);
}
