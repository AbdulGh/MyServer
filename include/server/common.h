#ifndef COMMON_H
#define COMMON_H

#include <asm-generic/socket.h>
#include <cstring>
#include <ostream>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <string>
#include <functional>
#include <utility>

namespace MyServer {

using Query = std::unordered_map<std::string, std::string>;

struct Request {
  enum class Method { GET, POST, NUM_METHODS };
  Method method;
  std::string endpoint;
  Query query;
  Query headers;
  std::string body;
};

struct Response {
  enum class StatusCode: unsigned {
    OK = 200,
    BAD_REQUEST = 400,
    NOT_FOUND = 404,
    IM_A_TEAPOT = 418,
    INTERNAL_SERVER_ERROR = 500,
  };
  StatusCode statusCode;
  std::string body;

  std::string toHTTPResponse() {
    std::stringstream out;
    out << "HTTP/1.1 " << std::to_underlying(statusCode) << "\r\n";
    out << "Content-Type: text/plain; charset=US-ASCII\r\n";
    if (body.size() > 0) {
      out << "Content-Length: " << body.size() << "\r\n\r\n";
      out << body;
    }
    else out << "\r\n";
    return out.str();
  }
};

using Handler = std::function<Response(Request&)>;
using HandlerMap = std::unordered_map<std::string, Handler>;

// the maximum number of bytes we will read/write from/to a client before continuing with the round robin
constexpr size_t CHUNKSIZE = 4096;

}

#endif
