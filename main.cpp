#include "server.h"

int main()
{
  MyServer::Server server {};

  server.registerHandler(
    "/echo", MyServer::Request::Method::POST,
    [](MyServer::Request& request) -> MyServer::Response  {
      // std::this_thread::sleep_for(std::chrono::milliseconds(500));
      return {
        .statusCode = MyServer::Response::StatusCode::OK,
        .body = std::string("Server replies: ") + request.body
      };
  });

  server.go();
  return 0;
}
