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
    "/", Request::Method::GET,
    [](Request&) -> Response  {
      return {
        .statusCode = Response::StatusCode::OK,
        .contentType = Response::ContentType::PLAINTEXT,
        .body = "hello"
      };
  });

  server.registerHandler(
    "/echo", Request::Method::POST,
    [](Request& request) -> Response  {
      return {
        .statusCode = Response::StatusCode::OK,
        .contentType = Response::ContentType::PLAINTEXT,
        .body = std::string("Server replies: ") + request.body
      };
  });

  using Todo = JSON<Pair<"description", std::string>, Pair<"done", bool>, Pair<"due", Nullable<std::string>>>;
  Utils::ConcurrentMap<std::string, Todo, JSON<MapOf<Todo>>> todoDatabase {};

  server.registerHandler(
    "/todo", Request::Method::GET,
    [&todoDatabase](Request& req) -> Response {
      auto it = req.query.find("id");
      if (it == req.query.end()) return {
        .statusCode = Response::StatusCode::OK,
        .contentType = Response::ContentType::JSON,
        .body = todoDatabase.getUnderlyingMap().toString()
      };
      else {
        std::optional<Todo> retrieved = todoDatabase.get(it->second);
        if (retrieved) return {
          .statusCode = Response::StatusCode::OK,
          .contentType = Response::ContentType::JSON,
          .body = retrieved->toString()
        };
        else return {
          .statusCode = Response::StatusCode::NOT_FOUND,
          .body = "check the provided id"
        };
      }
    }
  );

  server.registerHandler(
    "/todo", Request::Method::DELETE,
    [&todoDatabase](Request& req) -> Response {
      auto it = req.query.find("id");
      if (it == req.query.end()) return {
        .statusCode = Response::StatusCode::BAD_REQUEST,
        .body = "please provide an id"
      };
      size_t removed = todoDatabase.erase(it->second);
      if (removed) return {
        .statusCode = Response::StatusCode::OK
      };
      else return {
        .statusCode = Response::StatusCode::NOT_FOUND,
        .body = "couldn't find that id"
      };
    }
  );

  std::mt19937 mt{};
  using TodoResponse = JSON<Pair<"id", std::string>, Pair<"todo", Todo>>;
  server.registerHandler(
    "/todo", Request::Method::PUT,
    [&todoDatabase, &mt](Request& req) -> Response {
      Todo todo {req};

      if (!todo.get<"description">()) {
        throw Utils::HTTPException(Response::StatusCode::UNPROCESSABLE_ENTITY, "Need a description");
      }
      if (!todo.get<"done">()) todo.get<"done">() = false;

      std::string id;
      if (auto it = req.query.find("id"); it != req.query.end()) id = it->second;
      else id = std::format("{:x}{:x}{:x}", mt(), mt(), mt());

      todoDatabase.insert_or_assign(id, todo);

      TodoResponse resp;
      resp.get<"id">() = id;
      resp.get<"todo">() = std::move(todo);

      return {
        .statusCode = Response::StatusCode::OK,
        .contentType = Response::ContentType::JSON,
        .body = resp.toString()
      };
    }
  );

  server.go(8675);
  return 0;
}
