#ifndef JSON_H
#define JSON_H

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <array>
#include <tuple>
#include <utility>
#include <variant>

#include "server/common.h"
#include "utils/httpException.h"

namespace {
MyServer::Response::StatusCode UNPROCESSABLE_ENTITY = MyServer::Response::StatusCode::UNPROCESSABLE_ENTITY;

constexpr void stripWhitespace(std::string_view& str, int from = 0) {
  while (from < str.size() && std::isspace(str[from])) ++from;
  str = str.substr(from);
}

}

namespace MyServer::Utils::JSON {

template <typename ContentType>
concept DefaultConstructible = std::is_default_constructible_v<ContentType>;
template <typename Me, DefaultConstructible ContentType>
struct JSONBase
{
  ContentType contents;

  JSONBase() {};
  JSONBase(std::string_view& str): contents{ Me::consumeFromJSON(str) } {}
  JSONBase(const Request& req): contents{[&req, this]() {
    std::string_view bodysv{ req.body };
    return Me::consumeFromJSON(bodysv);
  }()} {}

  JSONBase(const JSONBase&) = default;
  JSONBase(JSONBase&&) = default;

  JSONBase(const ContentType& other) {
    contents = other; 
  }   
  JSONBase(ContentType&& other) {
    contents = std::move(other);
  }   

  JSONBase& operator=(const JSONBase& other) = default;
  JSONBase& operator=(JSONBase&& other) = default;
  JSONBase& operator=(const ContentType& newContents) {
    contents = newContents;
    return *this;
  }
  JSONBase& operator=(ContentType&& newContents) {
    contents = std::move(newContents);
    return *this;
  }

  friend bool operator==(const JSONBase& lhs, const JSONBase& rhs) {
    return lhs.contents == rhs.contents;
  };
  friend bool operator==(const JSONBase& lhs, ContentType nested) {
    return lhs.contents == nested;
  }
};

template <typename... Ts>
struct JSON{};

//useful so the user doesn't need to write JSON<Pair<"key", JSON<double>>
template <typename... Ts>
struct IdempotentJSONTag {
  using type = JSON<Ts...>;
};
template <typename... Ts>
struct IdempotentJSONTag<JSON<Ts...>> {
  using type = JSON<Ts...>;
};
template <typename... Ts>
using IdempotentJSONTag_t = typename IdempotentJSONTag<Ts...>::type;

static_assert(
std::is_same_v<IdempotentJSONTag_t<JSON<std::string>>, IdempotentJSONTag_t<std::string>>,
"IdempotentJSONTag_t is broken"
);

// 'simple' atomic types (string, double, bool)
struct Null {}; //JSON<Null> to follow

template <typename... Ts>
struct Member;
template <typename T, typename... Ts>
struct Member<T, std::variant<Ts...>> : std::disjunction<std::is_same<T, Ts>...> {};
template <typename T>
concept JSONAtom = Member<T, std::variant<std::string, double, bool, Null>>::value;

template <JSONAtom ContentType>
class JSON<ContentType>: public JSONBase<JSON<ContentType>, ContentType> {
public:
  using JSONBase<JSON<ContentType>, ContentType>::JSONBase;
  using JSONBase<JSON<ContentType>, ContentType>::operator=;
  static ContentType consumeFromJSON(std::string_view&) {
    static_assert(false, "consumeFromJSON should be specialised");
  }
  std::string toString() const = delete;
};

// A json is:
// - a string
// - a double
// - a boolean
// - an array of json
// - a lookup from strings to json
// - or null

/*** string type ***/
template <>
inline std::string JSON<std::string>::consumeFromJSON(std::string_view& json) {
  stripWhitespace(json);
  //todo extract
  size_t end;
  if (json.size() < 2 || json[0] != '"' || (end = json.find_first_of('"', 1)) == std::string_view::npos) {
    throw HTTPException(UNPROCESSABLE_ENTITY, "JSON string not \"delimited\"");
  }
  std::string result {json.substr(1, end - 1)};
  json.remove_prefix(end + 1);
  return result;
};

template <>
inline std::string JSON<std::string>::toString() const {
  return "\"" + contents + "\"" ;
}

template <>
inline double JSON<double>::consumeFromJSON(std::string_view& json) {
  stripWhitespace(json);
  if (json.size() == 0 ) {
    throw HTTPException(UNPROCESSABLE_ENTITY, "Expected double");
  }

  int sign = 1;
  if (json[0] == '-') {
    sign = -1;
    json.remove_prefix(1);
  }
  else if (!std::isdigit(json[0])) {
    throw HTTPException(UNPROCESSABLE_ENTITY, "Expected double");
  }

  double result = 0;
  int head = 0;
  while (head < json.size() && std::isdigit(json[head])) {
    result *= 10;
    result += json[head] - '0';
    ++head;
  }

  if (head < json.size() && json[head] == '.') {
    ++head;
    if (head >= json.size() || !std::isdigit(json[head])) {
      throw HTTPException(UNPROCESSABLE_ENTITY, "No numbers after the .");
    }

    //todo consider fp error
    double div = 0.1;
    do {
      result += div * (json[head] - '0');
      div *= 0.1;
      ++head;
    } while (head < json.size() && std::isdigit(json[head]));
  }

  result *= sign;
  json.remove_prefix(head);
  return result;
};

template <>
inline std::string JSON<double>::toString() const {
  return std::to_string(contents);
}

template <>
inline bool JSON<bool>::consumeFromJSON(std::string_view& json) {
  stripWhitespace(json);
  //substr does bounds checking
  if (json.substr(0, 4) == "true") {
    json.remove_prefix(4);
    return true;
  }
  else if (json.substr(0, 5) == "false") {
    json.remove_prefix(5);
    return false;
  }
  else throw HTTPException(UNPROCESSABLE_ENTITY, "expected boolean");
};

template <>
inline std::string JSON<bool>::toString() const {
  if (contents) return "true";
  return "false";
}

// homogeneous array of json
template <typename T>
struct ListOf {
  using type = IdempotentJSONTag_t<T>;
};

template <typename T>
struct JSON<ListOf<T>>: public JSONBase<JSON<ListOf<T>>, std::vector<IdempotentJSONTag_t<T>>> {
  using JSONBase<JSON<ListOf<T>>, std::vector<IdempotentJSONTag_t<T>>>::operator=;
  using JSONBase<JSON<ListOf<T>>, std::vector<IdempotentJSONTag_t<T>>>::JSONBase;
  using IdempotentJSONType = IdempotentJSONTag_t<T>;
  using VectorType = std::vector<IdempotentJSONType>;

  static VectorType consumeFromJSON(std::string_view& json) {
    VectorType parsedContents {};
    stripWhitespace(json);
    if (json.empty() || json[0] != '[' || json.size() < 2) {
      throw HTTPException(UNPROCESSABLE_ENTITY, "expected array");
    }
    json.remove_prefix(1);

    while (json.size() != 0 && json[0] != ']') {
      stripWhitespace(json);
      parsedContents.emplace_back(json);
      stripWhitespace(json);

      if (json.size() == 0) {
        throw HTTPException(UNPROCESSABLE_ENTITY, "expected , in array");
      }
      else if (json[0] == ',') json.remove_prefix(1);
    }

    if (json.size() == 0) {
      throw HTTPException(UNPROCESSABLE_ENTITY, "expected closing ]");
    }
    json.remove_prefix(1); //must be ']'

    return parsedContents;
  }

  IdempotentJSONType& operator[](size_t index) {
    return this->contents[index];
  }

  std::string toString() {
    if (this->contents.size() == 0) return "[]";

    using IdempotentJSONType = IdempotentJSONTag_t<T>;
    std::stringstream out {};
    out << "[" << this->contents[0].toString();

    for (int i = 1; i < this->contents.size(); ++i) {
      out << "," << this->contents[i].toString();
    }

    out << "]";
    return out.str();
  }
};

/*** specialised objects ***/
//template 'string literals'
template <size_t N>
struct StringLiteral {
  constexpr StringLiteral(const char(&str)[N]) {
    std::copy_n(str, N - 1, contents);
    contents[N - 1] = '\0';
  }
  constexpr operator std::string_view() const { return std::string_view(contents); }
  char contents[N];
};
template <size_t N>
constexpr bool operator==(const StringLiteral<N>& lhs, std::string_view rhs) {
  return std::string_view(lhs.contents) == rhs;
}

template <StringLiteral K, typename V>
struct Pair {
  static constexpr StringLiteral key = K;
  using value = V; //not actually used
};

//todo - sort the keys for proper comparison?
template <StringLiteral... Ks, typename... Vs>
class JSON<Pair<Ks, Vs>...>: public JSONBase<JSON<Pair<Ks, Vs>...>, std::tuple<std::optional<IdempotentJSONTag_t<Vs>>...>> {
public:
  using ValueTupleType = std::tuple<std::optional<IdempotentJSONTag_t<Vs>>...>;
  using JSONBase<JSON<Pair<Ks, Vs>...>, ValueTupleType>::operator=;

  static constexpr std::array<std::string_view, sizeof...(Ks)> keys = { Ks.contents... };

  static ValueTupleType consumeFromJSON(std::string_view& str) {
    ValueTupleType newContents {};

    stripWhitespace(str);
    if (str.size() == 0 || str[0] != '{') {
      throw HTTPException(UNPROCESSABLE_ENTITY, "expected object beginning with '{'");
    }
    str.remove_prefix(1);
    stripWhitespace(str);

    if (str.size() == 0) {
      throw HTTPException(UNPROCESSABLE_ENTITY, "expected object ending with '}'");
    }
    else if (str[0] == '}') return newContents;

    do {
      if (str[0] != '\"') {
        throw HTTPException(UNPROCESSABLE_ENTITY, "expected \"delimited\" object keys");
      }

      size_t nextQuote = str.find_first_of('\"', 1);
      if (nextQuote == std::string_view::npos) {
        throw HTTPException(UNPROCESSABLE_ENTITY, "couldn't find closing \"");
      }

      std::string_view key = str.substr(1, nextQuote - 1);
      //parse a value
      stripWhitespace(str, nextQuote + 1);
      if (str.size() == 0 || str[0] != ':') {
        throw HTTPException(UNPROCESSABLE_ENTITY, "expected ':'");
      }
      stripWhitespace(str, 1);
      //str is now at the start of a value
      //set here will recursively call the appropriate json dto parser
      set(newContents, key, str);

      stripWhitespace(str);
      if (str.size() == 0) {
        throw HTTPException(UNPROCESSABLE_ENTITY, "expected object ending with '}'");
      }
      if (str[0] == ',') {
        stripWhitespace(str, 1);
        if (str.size() == 0) {
          throw HTTPException(UNPROCESSABLE_ENTITY, "json ended with a comma");
        }
      }
    } while (str[0] != '}');

    str.remove_prefix(1);

    return newContents;
  }

  std::string toString() const {
    return "{" + toStringHelper<0>(false);
  }

  /* doesnt work :(
  template <size_t index = 0>
  constexpr auto& operator[](const std::string_view& key) {
    static_assert(index < sizeof...(Ks), "bad key");
    if constexpr (key == keys[index]) return std::get<index>(contents);
    return this->operator[]<index + 1>(key);
  }
  */

  template <StringLiteral key>
  auto& get() {
    constexpr size_t index = findIndex<key>();
    return std::get<index>(this->contents);
  }

private:
  template <StringLiteral key>
  static constexpr size_t findIndex() {
    for (size_t i = 0; i < sizeof...(Ks); ++i) {
      if (key.contents == keys[i]) return i;
    }
    throw std::out_of_range("Unknown key");
  }

  //maybe not intuitive that this method, like all the others, consumes from the json
  //(but it is private)
  template <size_t index = 0> [[maybe_unused]]
  static bool set(ValueTupleType& tuple, std::string_view key, std::string_view& value) {
    if constexpr (index == sizeof...(Ks)) return false;
    else if (keys[index] == key) {
      std::get<index>(tuple) = std::tuple_element_t<index, ValueTupleType>{value};
      return true;
    }
    else return set<index + 1>(tuple, key, value);
  }

  template <size_t index>
  std::string toStringHelper(bool withComma) const {
    if constexpr (index >= sizeof...(Ks)) {
      return "}";
    }
    else if (!std::get<index>(this->contents)) {
      return toStringHelper<index + 1>(withComma);
    }
    else {
      std::stringstream out;
      if (withComma) out << ','; 
      out << "\"" << keys[index] << "\":" << std::get<index>(this->contents)->toString();
      out << toStringHelper<index+1>(true);
      return out.str();
    }
  }
};

//just a wrapper
template <typename T>
struct Nullable {
  using type = T;
};

template <>
struct JSON<Null>: public JSONBase<JSON<Null>, Null> {
  static Null consumeFromJSON(std::string_view& str) {
    if (str.substr(0, 4) != "null") {
      throw HTTPException(UNPROCESSABLE_ENTITY, "expected 'null'");
    }
    stripWhitespace(str, 4);
    return Null{};
  }

  friend constexpr bool operator==(const JSON<Null>&, const JSON<Null>&) {
    return true;
  }

  std::string toString() const {
    return "null";
  }
};

template <typename T> 
struct JSON<Nullable<T>>: public JSONBase<JSON<Nullable<T>>, std::variant<IdempotentJSONTag_t<T>, JSON<Null>>> {
  using NestedType = IdempotentJSONTag_t<T>;
  using JSONBase<JSON<Nullable<T>>, std::variant<NestedType, JSON<Null>>>::operator=;

  static std::variant<NestedType, JSON<Null>> consumeFromJSON(std::string_view& str) {
    stripWhitespace(str);
    if (str.size() == 0 || str[0] != 'n') return NestedType{str};
    else return JSON<Null>{str};
  }

  operator bool() const {
    return !std::holds_alternative<JSON<Null>>(this->contents);
  }

  NestedType& operator*() {
    return std::get<NestedType>(this->contents);
  }

  const NestedType& operator*() const {
    return std::get<NestedType>(this->contents);
  }

  NestedType* operator->() {
    return &(this->operator*());
  }

  std::string toString() const {
    if (*this) return this->operator*().toString();
    return "null";
  }
};

/*** 'general' (but still homogeneous) maps ***/
// currently no hope of this being consteval
template <typename T>
struct MapOf {
  using type = T;
};

template <typename T> 
struct JSON<MapOf<T>>: public std::unordered_map<std::string, IdempotentJSONTag_t<T>> {
  using JSONType = IdempotentJSONTag_t<T>;
  using MapType = std::unordered_map<std::string, JSONType>;

  JSON(){}
  JSON(std::string_view& str): MapType{consumeFromJSON(str)} {}
  // JSON(const Request& req): MapType{consumeFromJSON(std::string_view{req.body})} {}

  std::unordered_map<std::string, JSONType> consumeFromJSON(std::string_view& json) {
    std::unordered_map<std::string, JSONType> newContents {};

    stripWhitespace(json);
    if (json.size() == 0 || json[0] != '{') {
      throw HTTPException(UNPROCESSABLE_ENTITY, "expected object beginning with '{");
    }

    stripWhitespace(json, 1);

    while (!json.empty()) {
      size_t end;
      if (json[0] == '}') break;
      else if (json.size() < 2 || json[0] != '"' || (end = json.find_first_of('"', 1)) == std::string_view::npos) {
        throw HTTPException(UNPROCESSABLE_ENTITY, "JSON string not \"delimited\"");
      }
      std::string key {json.substr(1, end - 1)};
      stripWhitespace(json, end + 1);

      if (json.size() == 0 || json[0] != ':') {
        throw HTTPException(UNPROCESSABLE_ENTITY, "expected ':' between key and value");
      }
      stripWhitespace(json, 1);

      MapType::emplace(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(json));

      //look for comma 
      stripWhitespace(json);
      if (json.size() > 0 && json[0] == ',') stripWhitespace(json, 1);
    }

    if (json.empty()) {
      throw HTTPException(UNPROCESSABLE_ENTITY, "expected closing '}'");
    }

    return newContents;
  }

  std::string toString() const {
    if (MapType::size() == 0) return "{}";

    std::stringstream out {};
    out << '{';
    auto it = MapType::begin();
    out << '"' << it->first << "\":" << it->second.toString(); 

    while (++it != MapType::end()) {
      out << ",\"" << it->first << "\":" << it->second.toString(); 
    }

    out << '}';
    return out.str();
  }
};

}

#endif
