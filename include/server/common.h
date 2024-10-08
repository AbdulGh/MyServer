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
#include <iostream>

namespace MyServer {

using Query = std::unordered_map<std::string, std::string>;

struct Request {
  enum class Method { GET, POST, PUT, DELETE, NUM_METHODS };
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
    UNPROCESSABLE_ENTITY = 422,
    INTERNAL_SERVER_ERROR = 500,
  };
  enum class ContentType: unsigned {
    PLAINTEXT, JSON, NUM_CONTENTTYPES
  };
  std::array<std::string_view, std::to_underlying(ContentType::NUM_CONTENTTYPES)> mimetypes {
    "text/plain", "application/json"
  };

  StatusCode statusCode;
  ContentType contentType;
  std::string body;

  std::string toHTTPResponse() {
    std::stringstream out;
    out << "HTTP/1.1 " << std::to_underlying(statusCode) << "\r\n";
    out << "Content-Type: " << mimetypes[std::to_underlying(contentType)] << "; charset=US-ASCII\r\n";
    out << "Content-Length: " << body.size() << "\r\n\r\n";
    out << body;

    return out.str();
  }
};

using Handler = std::function<Response(Request&)>;
using HandlerMap = std::unordered_map<std::string, Handler>;

// the maximum number of bytes we will read/write from/to a client before continuing with the round robin
constexpr size_t CHUNKSIZE = 4096;

}

#endif
