#ifndef PARSEHTTP_H
#define PARSEHTTP_H

#include <optional>

#include "common.h"

namespace MyServer{

struct Request {
  enum class HTTPMethod { GET, POST, NUM_HTTP_METHODS };
  HTTPMethod method;
  std::string endpoint;
  Query query;
  std::string body;
};

std::optional<Request> parseRequest(std::string_view request);

}

#endif
