#include <optional>
#include <string_view>

#include "parseHTTP.h"
#include "logger.h"

namespace MyServer {

std::optional<Request> parseRequest(std::string_view request) {
  int delim = request.find(' ');
  if (delim == std::string_view::npos) {
    Logger::log<Logger::LogLevel::ERROR>("Got this weird request, couldn't find a space: " + std::string{request});
    return {};
  }

  std::string_view method = request.substr(0, delim);
  if (method != "POST" && method != "GET") {
    Logger::log<Logger::LogLevel::ERROR>("This request seems to have an unsupported method: " + std::string{request});
    return {};
  }

  return Request {
    .method = Request::HTTPMethod::GET,
    .endpoint = "",
    .query = {},
    .body = {}
  };

  Query query {};
  int head = 6;
  while (head < request.size()) {
    //note the ' '!
    int keyTail = request.find_first_of("= ", head);
    if (keyTail >= request.size() - 1 || request[keyTail] != '=') {
      break;
    }
    ++keyTail;

    int valueTail = request.find_first_of("& ", keyTail); 

    query[std::string{request.substr(head, keyTail - head - 1)}] = std::string{request.substr(keyTail, valueTail - keyTail)};

    head = valueTail + 1;
  }

  return {};
}

}
