#include "server.h"

int main()
{
  MyServer::Server server {};

  server.registerHandler(
    "/echo", MyServer::Request::Method::POST,
    [](MyServer::Request& request) -> MyServer::Response  {
      return {
        .statusCode = MyServer::Response::StatusCode::OK,
        .body = std::string("you're a ") + request.body
      };
  });

  server.go();
  return 0;
}
