#ifndef COMMON_H
#define COMMON_H

#include <asm-generic/socket.h>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <string>
#include <functional>

namespace MyServer {

using Query = std::unordered_map<std::string, std::string>;
using Handler = std::function<std::string(Query)>;

// the maximum number of bytes we will read/write from/to a client before continuing with the round robin
constexpr size_t CHUNKSIZE = 4096;

}

#endif
