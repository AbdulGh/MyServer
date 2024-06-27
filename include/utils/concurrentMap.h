#ifndef CONCURRENTMAP_H
#define CONCURRENTMAP_H

// *Very* basic, just uses a R/W mutex to protect the map
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <concepts>

namespace MyServer::Utils {

template <typename T, typename K, typename V>
concept MapLike = requires(T candidate, K key, V value, typename T::iterator it) {
  typename T::const_iterator;
  //users can iterate over the map, and we want to use a read-lock for this, so the iteration should be const
  //probably this isn't enough and the user can cause race conditions if they try
  // requires requires (T::iterator it) {
    // std::same_as<typename T::const_iterator, decltype(std::make_const_iterator(it))>;
  // };

  { candidate.find(key) } -> std::same_as<typename T::iterator>;
  { candidate.end() } -> std::same_as<typename T::iterator>;

  { candidate.cbegin() } -> std::same_as<typename T::const_iterator>;
  { candidate.cend() } -> std::same_as<typename T::const_iterator>;

  { candidate.insert_or_assign(key, value) } -> std::same_as<std::pair<typename T::iterator, bool>>;
  { candidate.erase(key) } -> std::same_as<std::size_t>;
  { candidate.erase(it) } -> std::same_as<typename T::iterator>;
  { candidate.clear() } -> std::same_as<void>;

  { candidate.empty() } -> std::same_as<bool>;
};

template <typename K, typename V, MapLike<K, V> MapType = std::unordered_map<K,V>>
class ConcurrentMap 
{
protected:
  MapType map {};
  mutable std::shared_mutex rwLock {};

public:
  std::optional<V> get(const K& key) const {
    std::shared_lock<std::shared_mutex> lock(rwLock);
    auto it = map.find(key);
    if (it == map.end()) return {};
    return it->second;
  }

  bool insert_or_assign(const K& key, const V& value) {
    std::unique_lock<std::shared_mutex> lock(rwLock);
    return map.insert_or_assign(key, value).second;
  }

  size_t erase(const K& deletion) {
    std::unique_lock<std::shared_mutex> lock(rwLock);
    return map.erase(deletion);
  }

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

  void clear() {
    std::unique_lock<std::shared_mutex> lock(this->rwLock);
    map.clear();
  }

  bool empty() const {
    return map.empty();
  }
};

//currently trusts the user that the chosen map type makes sense
template <typename K, typename V, MapLike<K, V> MapType = std::map<K,V>>
class OrderedConcurrentMap: public ConcurrentMap<K, V, MapType>
{
public:
  MapType::const_iterator cbegin() const {
    std::shared_lock<std::shared_mutex> lock(this->rwLock);
    return this->map.cbegin();
  }

  void popFront() {
    std::unique_lock<std::shared_mutex> lock(this->rwLock);
    this->map.erase(this->map.begin());
  }
};

}
#endif

