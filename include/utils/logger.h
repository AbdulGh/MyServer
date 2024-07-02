#ifndef LOGGER_H
#define LOGGER_H

#include <csignal>
#include <cstring>
#include <string>
#include <array>
#include <syncstream>
#include <thread>
#include <utility>
#include <iostream>
#include <chrono>
#include <ctime>

namespace MyServer {
namespace Logger {

enum class LogLevel {
  OFF, FATAL, ERROR, WARN, INFO, DEBUG
};

// constexpr LogLevel reportingLevel = LogLevel::DEBUG;
constexpr LogLevel reportingLevel = LogLevel::INFO;

consteval std::string_view logLevelToString(LogLevel level){
  constexpr std::array<std::string_view, 6> LogLevelStr {
    "MissingNo.", "FATAL", "ERROR", "WARN", "INFO", "DEBUG"
  };
  return LogLevelStr[std::to_underlying(level)];
}

template <LogLevel level, typename... Args>
void withGreeting(std::ostream& output, Args... args) {
  using namespace std::chrono;
  std::osyncstream synced {output};
  synced << "[" << system_clock::now() << "] " << logLevelToString(level) << " (thread " << std::this_thread::get_id() << "): "; 
  (synced << ... << args);
  synced << std::endl;
}

template <LogLevel level>
inline void log(const std::string& message) {
  if constexpr (std::to_underlying(reportingLevel) < std::to_underlying(level)) return;
  withGreeting<level>(std::cout, message);
}

template <>
inline void log<LogLevel::ERROR>(const std::string& message) {
  if constexpr (std::to_underlying(reportingLevel) < std::to_underlying(LogLevel::ERROR)) return;
  withGreeting<LogLevel::ERROR>(std::cerr, message, " (last error: ", strerror(errno), " (", errno, "))");
}

template <>
inline void log<LogLevel::FATAL>(const std::string& message) {
  if constexpr (std::to_underlying(reportingLevel) < std::to_underlying(LogLevel::FATAL)) return;
  std::cout.flush();
  withGreeting<LogLevel::FATAL>(
    std::cerr, 
    message, " (last error: ", strerror(errno), " (", errno, ")). Fatal message received, shutting down..."
  );
  raise(SIGINT);
}

} //namespace Logger

[[maybe_unused]]
inline int insist(int res, const std::string& message) {
  if (res < -1) Logger::log<Logger::LogLevel::FATAL>(message);
  return res;
}

}

#endif
