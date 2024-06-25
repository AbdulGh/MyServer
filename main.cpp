#include <climits>
#include <format>
#include <random>
#include <utility>

#include "server/server.h"
#include "utils/concurrentMap.h"
#include "utils/httpException.h"
#include "utils/json.h"

int main()
{
  using namespace MyServer;
  using namespace MyServer::Utils::JSON;

  Server server {};

  server.registerHandler(
    "/echo", Request::Method::POST,
    [](Request& request) -> Response  {
      return {
        .statusCode = Response::StatusCode::OK,
        .body = std::string("Server replies: ") + request.body
      };
  });

  using Todo = JSON<Pair<"description", std::string>, Pair<"done", bool>, Pair<"due", Nullable<std::string>>>;
  Utils::ConcurrentMap<std::string, Todo, JSON<MapOf<Todo>>> todoDatabase {};

  server.registerHandler(
    "/list", Request::Method::GET,
    [&todoDatabase](Request&) -> Response {
      return {
        .statusCode = Response::StatusCode::OK,
        .body = todoDatabase.getUnderlyingMap().toString()
      };
    }
  );

  std::mt19937 mt{};

  using TodoResponse = JSON<Pair<"id", std::string>, Pair<"todo", Todo>>;
  server.registerHandler(
    "/todo", Request::Method::PUT,
    [&todoDatabase, &mt](Request& req) -> Response {
      std::string_view todo_fixme {req.body};
      Todo todo {todo_fixme};

      if (!todo.get<"description">()) throw Utils::HTTPException(Response::StatusCode::UNPROCESSABLE_ENTITY, "Need a description");
      if (!todo.get<"done">()) todo.get<"done">() = false;

      std::string id;
      if (auto it = req.query.find("id"); it != req.query.end()) id = it->second;
      else id = std::format("{:x}", mt());

      todoDatabase.insert({id, todo});

      TodoResponse resp;
      resp.get<"id">() = id;
      resp.get<"todo">() = std::move(todo);

      return {
        .statusCode = Response::StatusCode::OK,
        .body = resp.toString()
      };
    }
  );


  server.go();
  return 0;
}
