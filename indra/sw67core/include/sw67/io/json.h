/**
 * @file json.h
 * @brief A small JSON value, reader and writer for 6|7 World Studio's own
 *        files (workspace profiles, creator projects, photo batches, plan
 *        catalogues). Files are read as hostile: nesting, size and string
 *        lengths are bounded, and a parse error never throws.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::io
{

class Json
{
public:
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>; ///< Sorted: written files diff cleanly.

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : mType(Type::Bool), mBool(b) {}
    Json(int n) : mType(Type::Number), mNumber(n) {}
    Json(long long n) : mType(Type::Number), mNumber(static_cast<double>(n)) {}
    Json(double n) : mType(Type::Number), mNumber(n) {}
    Json(const char* s) : mType(Type::String), mString(s) {}
    Json(std::string s) : mType(Type::String), mString(std::move(s)) {}
    Json(std::string_view s) : mType(Type::String), mString(s) {}
    Json(Array a) : mType(Type::Array), mArray(std::make_shared<Array>(std::move(a))) {}
    Json(Object o) : mType(Type::Object), mObject(std::make_shared<Object>(std::move(o))) {}

    static Json array() { return Json(Array{}); }
    static Json object() { return Json(Object{}); }

    Type type() const { return mType; }
    bool isNull() const { return mType == Type::Null; }
    bool isBool() const { return mType == Type::Bool; }
    bool isNumber() const { return mType == Type::Number; }
    bool isString() const { return mType == Type::String; }
    bool isArray() const { return mType == Type::Array; }
    bool isObject() const { return mType == Type::Object; }

    bool asBool(bool fallback = false) const { return isBool() ? mBool : fallback; }
    double asNumber(double fallback = 0.0) const { return isNumber() ? mNumber : fallback; }
    /// The number as an int when it is one and in range, else the fallback.
    int asInt(int fallback = 0) const;
    const std::string& asString() const;
    std::string asString(std::string_view fallback) const { return isString() ? mString : std::string(fallback); }

    const Array& items() const;
    const Object& members() const;

    /// The member, or a null value when this is not an object or lacks it.
    const Json& operator[](std::string_view key) const;
    bool has(std::string_view key) const;

    /// Writes: turns this into an object or array first if it is not one.
    Json& set(const std::string& key, Json value);
    Json& push(Json value);

private:
    Type mType = Type::Null;
    bool mBool = false;
    double mNumber = 0.0;
    std::string mString;
    // Shared, copy-on-write: values copy cheaply, writes detach.
    std::shared_ptr<Array> mArray;
    std::shared_ptr<Object> mObject;
};

struct JsonLimits
{
    std::size_t maxBytes = 4u * 1024u * 1024u; ///< 4 MB of text.
    int maxDepth = 64;
    std::size_t maxStringBytes = 1024u * 1024u;
    std::size_t maxItems = 100000; ///< Elements of any one array or object.
};

struct JsonError
{
    std::size_t offset = 0;
    std::string what;
};

/// Parses strict JSON (RFC 8259). Returns nullopt and fills `error` on any
/// error or when a limit is passed.
std::optional<Json> parseJson(std::string_view text, JsonError* error = nullptr, const JsonLimits& limits = {});

/// Writes JSON, indented by two spaces, object keys sorted, UTF-8 kept as is.
std::string writeJson(const Json& value);

} // namespace sw67::io
