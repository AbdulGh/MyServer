#ifndef READERBIASEDSET_H
#define READERBIASEDSET_H

#include <mutex>
#include <unordered_set>
#include <utility>

namespace MyServer::Utils {

// writers have two mutexes to go through, so at most one will contend with the reader 
template <typename T, typename SetType = std::unordered_set<T>>
class ReaderBiasedSet
{
private:
  SetType set {};

  std::mutex writerMutex {};
  std::mutex readerMutex {};

public:
  void add(const T& insertion) {
    std::lock_guard<std::mutex> writeLock {writerMutex};
    std::lock_guard<std::mutex> readLock {readerMutex};
    set.insert(insertion);
  }

  SetType take () {
    std::lock_guard<std::mutex> readLock {readerMutex};
    return std::exchange(set, {});
  }
};

}

#endif
