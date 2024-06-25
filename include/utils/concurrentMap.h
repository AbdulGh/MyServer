#ifndef CONCURRENTMAP_H
#define CONCURRENTMAP_H

// *Very* basic, just uses a R/W mutex to protect the map
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <concepts>

namespace MyServer::Utils {

template <typename T, typename K, typename V>
concept MapLike = requires(T candidate, K key, V value) {
  typename T::iterator;

  //users can iterate over the map, and we want to use a read-lock for this, so the iteration should be const
  //probably this isn't enough and the user can cause race conditions if they try
  // requires requires (T::iterator it) {
    // std::same_as<typename T::const_iterator, decltype(std::make_const_iterator(it))>;
  // };

  { candidate.find(key) } -> std::same_as<typename T::iterator>;
  { candidate.end() } -> std::same_as<typename T::iterator>;

  { candidate.cbegin() } -> std::same_as<typename T::const_iterator>;
  { candidate.cend() } -> std::same_as<typename T::const_iterator>;

  { candidate.insert(std::make_pair(key, value)) } -> std::same_as<std::pair<typename T::iterator, bool>>;
  { candidate.erase(key) } -> std::same_as<std::size_t>;
};

template <typename K, typename V, MapLike<K, V> MapType = std::unordered_map<K,V>>
class ConcurrentMap 
{
private:
  MapType map {};
  mutable std::shared_mutex rwLock {};

public:
  std::optional<V> get(const K& key) const {
    std::shared_lock<std::shared_mutex> lock(rwLock);
    auto it = map.find(key);
    if (it == map.end()) return {};
    return *it;
  }

  bool insert(const std::pair<K, V>& insertion) {
    std::unique_lock<std::shared_mutex> lock(rwLock);
    return map.insert(insertion).second;
  }

  size_t erase(const std::pair<K, V>& insertion) {
    std::unique_lock<std::shared_mutex> lock(rwLock);
    return map.erase(insertion);
  }

  //todo check pair constness
  template <typename FuncType>
  requires std::invocable<FuncType, const std::pair<K,V>&>
  void forEach(FuncType&& fun) {
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
      fun(*it);
    }
  }

  const MapType& getUnderlyingMap() const {
    return map;
  }
};

}
#endif

