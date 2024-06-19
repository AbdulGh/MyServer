#ifndef LOGGER_H
#define LOGGER_H

#include <csignal>
#include <cstring>
#include <string>
#include <array>
#include <thread>
#include <utility>
#include <iostream>

namespace MyServer {
namespace Logger {

enum class LogLevel {
  OFF, FATAL, ERROR, WARN, INFO, DEBUG
};

constexpr LogLevel reportingLevel = LogLevel::DEBUG;

consteval std::string_view logLevelToString(LogLevel level){
  constexpr std::array<std::string_view, 6> LogLevelStr {
    "MissingNo.", "FATAL", "ERROR", "WARN", "INFO", "DEBUG"
  };
  return LogLevelStr[std::to_underlying(level)];
}

template <LogLevel level>
std::ostream& greet(std::ostream& output) {
  output << logLevelToString(level) << " (thread " << std::this_thread::get_id() << "): "; 
  return output;
}

template <LogLevel level>
inline void log(const std::string& message) {
  if constexpr (std::to_underlying(reportingLevel) < std::to_underlying(level)) return;
  greet<level>(std::cout);
  std::cout << message << "\n";
}

template <>
inline void log<LogLevel::ERROR>(const std::string& message) {
  if constexpr (std::to_underlying(reportingLevel) < std::to_underlying(LogLevel::ERROR)) return;
  greet<LogLevel::ERROR>(std::cerr);
  std::cerr << message;
  std::cerr << " (last error: " << strerror(errno) << " (" << errno << "))\n";
}

template <>
inline void log<LogLevel::FATAL>(const std::string& message) {
  if constexpr (std::to_underlying(reportingLevel) < std::to_underlying(LogLevel::FATAL)) return;
  std::cout.flush();
  greet<LogLevel::FATAL>(std::cerr);
  std::cerr << message;
  std::cerr << " (last error: " << strerror(errno) << " (" << errno << "))\n";
  std::cerr << "Fatal message recieved, shutting down...";
  raise(SIGINT);
}

}

[[maybe_unused]]
inline int insist(int res, const std::string& message) {
  if (res < -1) Logger::log<Logger::LogLevel::FATAL>(message);
  return res;
}

}

#endif
