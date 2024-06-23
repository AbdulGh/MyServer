#ifndef JSON_H
#define JSON_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include <array>
#include <iostream>
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

template <typename... Ts>
struct Member;
template <typename T, typename... Ts>
struct Member<T, std::variant<Ts...>> : std::disjunction<std::is_same<T, Ts>...> {};
template <typename T>
concept JSONAtom = Member<T, std::variant<std::string, double, bool>>::value;

//todo this shouldn't be needed
template <typename... Ts>
struct ContentTypeWrapper {};

template <JSONAtom T>
struct ContentTypeWrapper<T> {
  using type = T;
};

template <typename... Ts>
using ContentType = ContentTypeWrapper<Ts...>::type;

// should only be used in one of its specialised forms
template <typename... Ts>
class JSON {
public:
  ContentType<Ts...> contents;

  JSON() = delete;
  JSON(std::string_view& str): contents{consumeFromJSON(str)} {}
  JSON(const Request& req): JSON{std::string_view{req.body}} {}
  JSON(JSON&) = default;
  JSON(JSON&&) = default;

  JSON(const ContentType<Ts...>& other) {
    contents = other; 
  }   
  JSON(ContentType<Ts...>&& other) {
    contents = std::move(other);
  }   

  static ContentType<Ts...> consumeFromJSON(std::string_view&) {
    static_assert(false, "consumeFromJSON should be specialised");
  }

  JSON& operator=(const JSON& other) {
    contents = other.contents;
    return *this;
  }

  JSON& operator=(JSON&& other) {
    contents = std::move(other.contents);
    return *this;
  }

  friend bool operator==(const JSON<Ts...>& lhs, const JSON<Ts...>& rhs) {
    return lhs.contents == rhs.contents;
  };

  //useful for JSON<std::string> etc
  JSON& operator=(const ContentType<Ts...>& newContents) {
    contents = newContents;
    return *this;
  }
  JSON& operator=(ContentType<Ts...>&& newContents) {
    contents = newContents;
    return *this;
  }

  friend bool operator==(const JSON<Ts...>& lhs, ContentType<Ts...> nested) {
    return lhs.contents == nested;
  }

  std::string toString() = delete;
};

//useful so the user doesn't need to write JSON<Pair<"key", *JSON<*double*>*>
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

// A json is:
// - a string
// - a double
// - a boolean
// - an array of json
// - a lookup from strings to json
// - or null

/*** copy/move/comparison for atoms ***/

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
  return result; //nrvo
};

template <>
inline std::string JSON<std::string>::toString() {
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
inline std::string JSON<double>::toString() {
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
inline std::string JSON<bool>::toString() {
  if (contents) return "true";
  return "false";
}

// homogeneous array of json

template <typename T>
struct ListOf {
  using type = IdempotentJSONTag_t<T>;
};

template <typename T>
struct JSON<ListOf<T>> {
  using IdempotentJSONType = IdempotentJSONTag_t<T>;
  using VectorType = std::vector<IdempotentJSONType>;

  VectorType contents {};

  JSON(std::string_view& json): contents{consumeFromJSON(json)} {}

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
    return contents[index];
  }

  std::string toString() {
    if (contents.size() == 0) return "[]";

    using IdempotentJSONType = IdempotentJSONTag_t<T>;
    std::stringstream out {};
    out << "[" << contents[0].toString();

    for (int i = 1; i < contents.size(); ++i) {
      out << "," << contents[i].toString();
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

template <StringLiteral... Ks, typename... Vs>
class JSON<Pair<Ks, Vs>...> {
public:
  static constexpr std::array<std::string_view, sizeof...(Ks)> keys = { Ks.contents... };
  using ValueTupleType = std::tuple<std::optional<IdempotentJSONTag_t<Vs>>...>;
  ValueTupleType contents;

  JSON() {}
  JSON(std::string_view& json): contents{consumeFromJSON(json)} {}

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
        std::cout << str << std::endl;
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
      if (!set(newContents, key, str)) {
        //todo allow option to enforce strict interpretation
      }

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

  std::string toString() {
    return "{" + toStringHelper<0>(false);
  }

  template <StringLiteral key>
  auto& get() {
    constexpr size_t index = findIndex<key>();
    return std::get<index>(contents);
  }

private:

  template <StringLiteral key, size_t index = 0>
  consteval const size_t findIndex() {
    static_assert(index < sizeof...(Ks), "Key not defined in type");
    if constexpr (key == keys[index]) return index;
    else return findIndex<key, index + 1>();
  }

  //maybe not intuitive that this method, like all the others, consumes from the json
  //(but it is private)
  template <size_t index = 0>
  static bool set(ValueTupleType& tuple, std::string_view key, std::string_view& value) {
    if constexpr (index == sizeof...(Ks)) return false;
    else if (keys[index] == key) {
      std::get<index>(tuple) = std::tuple_element_t<index, ValueTupleType>{value};
      return true;
    }
    else return set<index + 1>(tuple, key, value);
  }

  template <size_t index>
  std::string toStringHelper(bool withComma) {
    if constexpr (index >= sizeof...(Ks)) {
      return "}";
    }
    else if (!std::get<index>(contents)) {
      return toStringHelper<index + 1>(withComma);
    }
    else {
      std::stringstream out;
      if (withComma) out << ','; 
      out << "\"" << keys[index] << "\":" << std::get<index>(contents)->toString();
      out << toStringHelper<index+1>(withComma);
      return out.str();
    }
  }
};

template <StringLiteral... Ks, typename... Vs>
struct ContentTypeWrapper<Pair<Ks, Vs>...> {
  using type = JSON<Pair<Ks, Vs>...>::ValueTupleType;
};

//just a wrapper
template <typename T>
struct Nullable {
  using type = T;
};
struct Null {};

template <>
struct JSON<Null> {
  friend constexpr bool operator==(const JSON<Null>&, const JSON<Null>&) {
    return true;
  }

  friend constexpr bool operator!=(const JSON<Null>&, const JSON<Null>&) {
    return false;
  }

  std::string toString() {
    return "null";
  }
};

template <typename T> 
struct JSON<Nullable<T>> {
  using NestedType = IdempotentJSONTag_t<T>;
  std::variant<NestedType, JSON<Null>> contents;

  JSON(std::string_view& str): contents{consumeFromJSON(str)} {}

  std::variant<NestedType, JSON<Null>> consumeFromJSON(std::string_view& str) {
    stripWhitespace(str);
    if (str.size() == 0 || str[0] != 'n') return NestedType{str};
    else {
      if (str.substr(0, 4) != "null") {
        throw HTTPException(UNPROCESSABLE_ENTITY, "expected 'null'");
      }
      stripWhitespace(str, 5);
      return JSON<Null>{};
    }
  }

  operator bool() {
    return !std::holds_alternative<JSON<Null>>(contents);
  }

  NestedType& operator*() {
    return std::get<NestedType>(contents);
  }

  NestedType* operator->() {
    return &(this->operator*());
  }

  template <typename AccessType>
  auto& get() {
    return std::get<IdempotentJSONTag_t<AccessType>>(contents);
  }

  std::string toString() {
    if (*this) return this->operator*().toString();
    return "null";
  }
};

template <typename T> 
struct ContentTypeWrapper<JSON<Nullable<T>>> {
  using type = std::variant<typename JSON<Nullable<T>>::NestedType, JSON<Null>>;
};

/*** 'general' (but still homogeneous) maps ***/
// currently no hope of this being consteval

template <typename T>
struct MapOf {
  using type = T;
};

template <typename T> 
struct JSON<MapOf<T>> {
  using JSONType = IdempotentJSONTag_t<T>;
  std::unordered_map<std::string, JSONType> contents;

  JSON(std::string_view& str): contents{consumeFromJSON(str)} {}

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

      contents.emplace(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(json));

      //look for comma 
      stripWhitespace(json);
      if (json.size() > 0 && json[0] == ',') stripWhitespace(json, 1);
    }

    if (json.empty()) {
      throw HTTPException(UNPROCESSABLE_ENTITY, "expected closing '}'");
    }

    return newContents;
  }

  std::string toString() {
    if (contents.size() == 0) return "{}";

    std::stringstream out {};
    out << '{';
    auto it = contents.begin();
    out << '"' << it->first << "\":" << it->second.toString(); 

    while (++it != contents.end()) {
      out << ",\"" << it->first << "\":" << it->second.toString(); 
    }

    out << '}';
    return out.str();
  }
};


template <typename T> 
struct ContentTypeWrapper<JSON<MapOf<T>>> {
  using type = std::unordered_map<std::string, T>;
};


}

#endif
