#include "cJSON.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace {

char *copyString(const char *value) {
  if (value == nullptr) {
    return nullptr;
  }
  const size_t length = std::strlen(value);
  auto *copy = static_cast<char *>(std::malloc(length + 1));
  if (copy != nullptr) {
    std::memcpy(copy, value, length + 1);
  }
  return copy;
}

cJSON *makeNode(int type) {
  auto *node = static_cast<cJSON *>(std::calloc(1, sizeof(cJSON)));
  if (node != nullptr) {
    node->type = type;
  }
  return node;
}

void appendChild(cJSON *parent, cJSON *item) {
  if ((parent == nullptr) || (item == nullptr)) {
    return;
  }
  if (parent->child == nullptr) {
    parent->child = item;
    return;
  }
  cJSON *last = parent->child;
  while (last->next != nullptr) {
    last = last->next;
  }
  last->next = item;
  item->prev = last;
}

void appendEscaped(std::string &output, const char *value) {
  output.push_back('"');
  if (value != nullptr) {
    for (const unsigned char *cursor = reinterpret_cast<const unsigned char *>(value);
         *cursor != '\0'; cursor++) {
      switch (*cursor) {
        case '"':
          output += "\\\"";
          break;
        case '\\':
          output += "\\\\";
          break;
        case '\b':
          output += "\\b";
          break;
        case '\f':
          output += "\\f";
          break;
        case '\n':
          output += "\\n";
          break;
        case '\r':
          output += "\\r";
          break;
        case '\t':
          output += "\\t";
          break;
        default:
          if (*cursor < 0x20) {
            char escaped[7] = {};
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", *cursor);
            output += escaped;
          } else {
            output.push_back(static_cast<char>(*cursor));
          }
          break;
      }
    }
  }
  output.push_back('"');
}

void printNode(const cJSON *item, std::string &output) {
  if (item == nullptr) {
    output += "null";
    return;
  }

  switch (item->type) {
    case cJSON_Object:
    {
      output.push_back('{');
      bool first = true;
      for (const cJSON *child = item->child; child != nullptr; child = child->next) {
        if (!first) {
          output.push_back(',');
        }
        first = false;
        appendEscaped(output, child->string);
        output.push_back(':');
        printNode(child, output);
      }
      output.push_back('}');
      break;
    }
    case cJSON_Array:
    {
      output.push_back('[');
      bool first = true;
      for (const cJSON *child = item->child; child != nullptr; child = child->next) {
        if (!first) {
          output.push_back(',');
        }
        first = false;
        printNode(child, output);
      }
      output.push_back(']');
      break;
    }
    case cJSON_String:
      appendEscaped(output, item->valuestring);
      break;
    case cJSON_Number:
    {
      std::ostringstream number;
      number << std::setprecision(std::numeric_limits<double>::digits10 + 1) << item->valuedouble;
      output += number.str();
      break;
    }
    case cJSON_True:
      output += "true";
      break;
    case cJSON_False:
      output += "false";
      break;
    case cJSON_NULL:
    default:
      output += "null";
      break;
  }
}

class Parser {
 public:
  Parser(const char *begin, const char *end) : m_Cursor(begin), m_End(end) {}

  cJSON *parse(void) {
    skipWhitespace();
    cJSON *value = parseValue();
    skipWhitespace();
    if ((value == nullptr) || (m_Cursor != m_End)) {
      cJSON_Delete(value);
      return nullptr;
    }
    return value;
  }

 private:
  void skipWhitespace(void) {
    while ((m_Cursor < m_End)
           && ((*m_Cursor == ' ') || (*m_Cursor == '\t') || (*m_Cursor == '\r')
               || (*m_Cursor == '\n'))) {
      m_Cursor++;
    }
  }

  bool consume(char expected) {
    skipWhitespace();
    if ((m_Cursor >= m_End) || (*m_Cursor != expected)) {
      return false;
    }
    m_Cursor++;
    return true;
  }

  std::string parseString(void) {
    if ((m_Cursor >= m_End) || (*m_Cursor != '"')) {
      return {};
    }
    m_Cursor++;
    std::string result;
    while (m_Cursor < m_End) {
      const char character = *m_Cursor++;
      if (character == '"') {
        return result;
      }
      if (character != '\\') {
        result.push_back(character);
        continue;
      }
      if (m_Cursor >= m_End) {
        return {};
      }
      switch (*m_Cursor++) {
        case '"':
          result.push_back('"');
          break;
        case '\\':
          result.push_back('\\');
          break;
        case '/':
          result.push_back('/');
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        case 'u':
          if (m_End - m_Cursor < 4) {
            return {};
          }
          m_Cursor += 4;
          result.push_back('?');
          break;
        default:
          return {};
      }
    }
    return {};
  }

  cJSON *parseObject(void) {
    if (!consume('{')) {
      return nullptr;
    }
    cJSON *object = cJSON_CreateObject();
    if (object == nullptr) {
      return nullptr;
    }
    skipWhitespace();
    if (consume('}')) {
      return object;
    }
    while (m_Cursor < m_End) {
      skipWhitespace();
      std::string key = parseString();
      if (key.empty() && ((m_Cursor >= m_End) || (m_Cursor[-1] != '"'))) {
        cJSON_Delete(object);
        return nullptr;
      }
      if (!consume(':')) {
        cJSON_Delete(object);
        return nullptr;
      }
      cJSON *value = parseValue();
      if (value == nullptr) {
        cJSON_Delete(object);
        return nullptr;
      }
      cJSON_AddItemToObject(object, key.c_str(), value);
      skipWhitespace();
      if (consume('}')) {
        return object;
      }
      if (!consume(',')) {
        cJSON_Delete(object);
        return nullptr;
      }
    }
    cJSON_Delete(object);
    return nullptr;
  }

  cJSON *parseArray(void) {
    if (!consume('[')) {
      return nullptr;
    }
    cJSON *array = cJSON_CreateArray();
    if (array == nullptr) {
      return nullptr;
    }
    skipWhitespace();
    if (consume(']')) {
      return array;
    }
    while (m_Cursor < m_End) {
      cJSON *value = parseValue();
      if (value == nullptr) {
        cJSON_Delete(array);
        return nullptr;
      }
      cJSON_AddItemToArray(array, value);
      skipWhitespace();
      if (consume(']')) {
        return array;
      }
      if (!consume(',')) {
        cJSON_Delete(array);
        return nullptr;
      }
    }
    cJSON_Delete(array);
    return nullptr;
  }

  cJSON *parseNumber(void) {
    const char *begin = m_Cursor;
    while ((m_Cursor < m_End)
           && ((*m_Cursor == '-') || (*m_Cursor == '+') || (*m_Cursor == '.')
               || (*m_Cursor >= '0' && *m_Cursor <= '9') || (*m_Cursor == 'e')
               || (*m_Cursor == 'E'))) {
      m_Cursor++;
    }
    std::string text(begin, m_Cursor);
    if (text.empty()) {
      return nullptr;
    }
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if ((end == text.c_str()) || (*end != '\0') || !std::isfinite(value)) {
      return nullptr;
    }
    return cJSON_CreateNumber(value);
  }

  cJSON *parseValue(void) {
    skipWhitespace();
    if (m_Cursor >= m_End) {
      return nullptr;
    }
    if (*m_Cursor == '{') {
      return parseObject();
    }
    if (*m_Cursor == '[') {
      return parseArray();
    }
    if (*m_Cursor == '"') {
      const std::string value = parseString();
      if ((value.empty()) && ((m_Cursor >= m_End) || (m_Cursor[-1] != '"'))) {
        return nullptr;
      }
      return cJSON_CreateString(value.c_str());
    }
    if (m_End - m_Cursor >= 4 && std::strncmp(m_Cursor, "true", 4) == 0) {
      m_Cursor += 4;
      return makeNode(cJSON_True);
    }
    if (m_End - m_Cursor >= 5 && std::strncmp(m_Cursor, "false", 5) == 0) {
      m_Cursor += 5;
      return makeNode(cJSON_False);
    }
    if (m_End - m_Cursor >= 4 && std::strncmp(m_Cursor, "null", 4) == 0) {
      m_Cursor += 4;
      return makeNode(cJSON_NULL);
    }
    return parseNumber();
  }

  const char *m_Cursor;
  const char *m_End;
};

}  // namespace

extern "C" cJSON *cJSON_CreateObject(void) {
  return makeNode(cJSON_Object);
}

extern "C" cJSON *cJSON_CreateArray(void) {
  return makeNode(cJSON_Array);
}

extern "C" cJSON *cJSON_CreateString(const char *value) {
  cJSON *node = makeNode(cJSON_String);
  if (node != nullptr) {
    node->valuestring = copyString(value == nullptr ? "" : value);
  }
  return node;
}

extern "C" cJSON *cJSON_CreateNumber(double value) {
  cJSON *node = makeNode(cJSON_Number);
  if (node != nullptr) {
    node->valuedouble = value;
    node->valueint = static_cast<int>(value);
  }
  return node;
}

extern "C" cJSON *cJSON_AddObjectToObject(cJSON *object, const char *name) {
  cJSON *item = cJSON_CreateObject();
  cJSON_AddItemToObject(object, name, item);
  return item;
}

extern "C" cJSON *cJSON_AddArrayToObject(cJSON *object, const char *name) {
  cJSON *item = cJSON_CreateArray();
  cJSON_AddItemToObject(object, name, item);
  return item;
}

extern "C" cJSON *cJSON_AddStringToObject(cJSON *object, const char *name, const char *value) {
  cJSON *item = cJSON_CreateString(value);
  cJSON_AddItemToObject(object, name, item);
  return item;
}

extern "C" cJSON *cJSON_AddBoolToObject(cJSON *object, const char *name, int value) {
  cJSON *item = makeNode(value ? cJSON_True : cJSON_False);
  cJSON_AddItemToObject(object, name, item);
  return item;
}

extern "C" cJSON *cJSON_AddNumberToObject(cJSON *object, const char *name, double value) {
  cJSON *item = cJSON_CreateNumber(value);
  cJSON_AddItemToObject(object, name, item);
  return item;
}

extern "C" void cJSON_AddItemToObject(cJSON *object, const char *name, cJSON *item) {
  if (item == nullptr) {
    return;
  }
  std::free(item->string);
  item->string = copyString(name == nullptr ? "" : name);
  appendChild(object, item);
}

extern "C" void cJSON_AddItemToArray(cJSON *array, cJSON *item) {
  appendChild(array, item);
}

extern "C" const cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *name) {
  if ((object == nullptr) || (object->type != cJSON_Object) || (name == nullptr)) {
    return nullptr;
  }
  for (const cJSON *item = object->child; item != nullptr; item = item->next) {
    if ((item->string != nullptr) && (std::strcmp(item->string, name) == 0)) {
      return item;
    }
  }
  return nullptr;
}

extern "C" int cJSON_IsNumber(const cJSON *item) {
  return item != nullptr && item->type == cJSON_Number;
}

extern "C" cJSON *cJSON_ParseWithLength(const char *value, size_t length) {
  if (value == nullptr) {
    return nullptr;
  }
  return Parser(value, value + length).parse();
}

extern "C" char *cJSON_PrintUnformatted(const cJSON *item) {
  std::string text;
  printNode(item, text);
  auto *result = static_cast<char *>(std::malloc(text.size() + 1));
  if (result != nullptr) {
    std::memcpy(result, text.c_str(), text.size() + 1);
  }
  return result;
}

extern "C" void cJSON_Delete(cJSON *item) {
  if (item == nullptr) {
    return;
  }
  cJSON *child = item->child;
  while (child != nullptr) {
    cJSON *next = child->next;
    cJSON_Delete(child);
    child = next;
  }
  std::free(item->valuestring);
  std::free(item->string);
  std::free(item);
}

extern "C" void cJSON_free(void *item) {
  std::free(item);
}
