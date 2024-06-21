#include "server/server.h"
#include "utils/concurrentMap.h"

int main()
{
  using namespace MyServer;

  Server server {};

  server.registerHandler(
    "/echo", Request::Method::POST,
    [](Request& request) -> Response  {
      return {
        .statusCode = Response::StatusCode::OK,
        .body = std::string("Server replies: ") + request.body
      };
  });

  Utils::ConcurrentMap<std::string, std::string> todoDatabase {};
  todoDatabase.insert({"key", "value1"});
  todoDatabase.insert({"key2", "value2"});

  server.registerHandler(
    "/list", Request::Method::GET,
    [&todoDatabase](Request&) -> Response {
      std::string response {""};
      todoDatabase.forEach([&response](const std::pair<std::string, std::string>& todo) {
        response += todo.first + " " + todo.second + "\n";
      });
      return {
        .statusCode = Response::StatusCode::OK,
        .body = std::move(response)
      };
    }
  );

  server.go();
  return 0;
}
