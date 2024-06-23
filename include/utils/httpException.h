#ifndef HTTPEXCEPTION_H
#define HTTPEXCEPTION_H

#include "server/common.h"

namespace MyServer::Utils {

class HTTPException: public std::runtime_error {
private:
  using StatusCode = MyServer::Response::StatusCode; 

  const char* message {};
  StatusCode code;

public:
  HTTPException(StatusCode code, const char* message):
    std::runtime_error{message}, code{code} {}

  StatusCode statusCode() { return code; }
};

}
#endif
