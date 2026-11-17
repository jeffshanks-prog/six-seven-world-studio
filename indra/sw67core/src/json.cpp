/**
 * @file json.cpp
 * @brief Bounded JSON reader and writer.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/io/json.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>

namespace sw67::io
{
namespace
{
const Json kNull;
const std::string kEmptyString;
const Json::Array kEmptyArray;
const Json::Object kEmptyObject;

class Parser
{
public:
    Parser(std::string_view text, const JsonLimits& limits) : mText(text), mLimits(limits) {}

    std::optional<Json> run(JsonError* error)
    {
        if (mText.size() > mLimits.maxBytes)
        {
            fail("file is larger than the limit");
        }
        std::optional<Json> value;
        if (mError.empty())
        {
            skipSpace();
            value = parseValue(0);
            skipSpace();
            if (mError.empty() && mPos != mText.size())
            {
                fail("unexpected text after the value");
            }
        }
        if (!mError.empty())
        {
            if (error)
            {
                error->offset = mErrorPos;
                error->what = mError;
            }
            return std::nullopt;
        }
        return value;
    }

private:
    void fail(const char* what)
    {
        if (mError.empty())
        {
            mError = what;
            mErrorPos = mPos;
        }
    }

    void skipSpace()
    {
        while (mPos < mText.size() &&
               (mText[mPos] == ' ' || mText[mPos] == '\t' || mText[mPos] == '\n' || mText[mPos] == '\r'))
        {
            ++mPos;
        }
    }

    bool literal(std::string_view word)
    {
        if (mText.substr(mPos, word.size()) == word)
        {
            mPos += word.size();
            return true;
        }
        return false;
    }

    std::optional<Json> parseValue(int depth)
    {
        if (depth > mLimits.maxDepth)
        {
            fail("nesting is deeper than the limit");
            return std::nullopt;
        }
        if (mPos >= mText.size())
        {
            fail("unexpected end of file");
            return std::nullopt;
        }
        const char c = mText[mPos];
        if (c == '{') return parseObject(depth);
        if (c == '[') return parseArray(depth);
        if (c == '"')
        {
            auto s = parseString();
            if (!s) return std::nullopt;
            return Json(std::move(*s));
        }
        if (c == 't' && literal("true")) return Json(true);
        if (c == 'f' && literal("false")) return Json(false);
        if (c == 'n' && literal("null")) return Json();
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        fail("unexpected character");
        return std::nullopt;
    }

    std::optional<Json> parseNumber()
    {
        const std::size_t start = mPos;
        if (mText[mPos] == '-') ++mPos;
        if (mPos >= mText.size()) { fail("bad number"); return std::nullopt; }
        if (mText[mPos] == '0')
        {
            ++mPos;
        }
        else if (mText[mPos] >= '1' && mText[mPos] <= '9')
        {
            while (mPos < mText.size() && mText[mPos] >= '0' && mText[mPos] <= '9') ++mPos;
        }
        else
        {
            fail("bad number");
            return std::nullopt;
        }
        if (mPos < mText.size() && mText[mPos] == '.')
        {
            ++mPos;
            const std::size_t digits = mPos;
            while (mPos < mText.size() && mText[mPos] >= '0' && mText[mPos] <= '9') ++mPos;
            if (mPos == digits) { fail("bad number"); return std::nullopt; }
        }
        if (mPos < mText.size() && (mText[mPos] == 'e' || mText[mPos] == 'E'))
        {
            ++mPos;
            if (mPos < mText.size() && (mText[mPos] == '+' || mText[mPos] == '-')) ++mPos;
            const std::size_t digits = mPos;
            while (mPos < mText.size() && mText[mPos] >= '0' && mText[mPos] <= '9') ++mPos;
            if (mPos == digits) { fail("bad number"); return std::nullopt; }
        }
        // strtod over a bounded copy: from_chars for double is not in every
        // standard library the viewer builds with.
        const std::string copy(mText.substr(start, mPos - start));
        if (copy.size() > 64) { fail("number is too long"); return std::nullopt; }
        char* end = nullptr;
        const double v = std::strtod(copy.c_str(), &end);
        if (!std::isfinite(v)) { fail("number is out of range"); return std::nullopt; }
        return Json(v);
    }

    static void appendUtf8(std::string& out, std::uint32_t cp)
    {
        if (cp < 0x80)
        {
            out.push_back(static_cast<char>(cp));
        }
        else if (cp < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    std::optional<std::uint32_t> hex4()
    {
        if (mPos + 4 > mText.size()) return std::nullopt;
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = mText[mPos++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return std::nullopt;
        }
        return v;
    }

    std::optional<std::string> parseString()
    {
        ++mPos; // opening quote
        std::string out;
        while (true)
        {
            if (mPos >= mText.size()) { fail("unterminated string"); return std::nullopt; }
            if (out.size() > mLimits.maxStringBytes) { fail("string is longer than the limit"); return std::nullopt; }
            const char c = mText[mPos++];
            if (c == '"') break;
            if (static_cast<unsigned char>(c) < 0x20) { fail("control character in string"); return std::nullopt; }
            if (c != '\\')
            {
                out.push_back(c);
                continue;
            }
            if (mPos >= mText.size()) { fail("unterminated string"); return std::nullopt; }
            const char e = mText[mPos++];
            switch (e)
            {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u':
            {
                auto cp = hex4();
                if (!cp) { fail("bad \\u escape"); return std::nullopt; }
                std::uint32_t code = *cp;
                if (code >= 0xD800 && code <= 0xDBFF)
                {
                    if (mText.substr(mPos, 2) != "\\u") { fail("unpaired surrogate"); return std::nullopt; }
                    mPos += 2;
                    auto lo = hex4();
                    if (!lo || *lo < 0xDC00 || *lo > 0xDFFF) { fail("unpaired surrogate"); return std::nullopt; }
                    code = 0x10000 + ((code - 0xD800) << 10) + (*lo - 0xDC00);
                }
                else if (code >= 0xDC00 && code <= 0xDFFF)
                {
                    fail("unpaired surrogate");
                    return std::nullopt;
                }
                appendUtf8(out, code);
                break;
            }
            default:
                fail("bad escape");
                return std::nullopt;
            }
        }
        return out;
    }

    std::optional<Json> parseArray(int depth)
    {
        ++mPos;
        Json::Array items;
        skipSpace();
        if (mPos < mText.size() && mText[mPos] == ']')
        {
            ++mPos;
            return Json(std::move(items));
        }
        while (true)
        {
            skipSpace();
            auto v = parseValue(depth + 1);
            if (!v) return std::nullopt;
            items.push_back(std::move(*v));
            if (items.size() > mLimits.maxItems) { fail("array has more items than the limit"); return std::nullopt; }
            skipSpace();
            if (mPos < mText.size() && mText[mPos] == ',') { ++mPos; continue; }
            if (mPos < mText.size() && mText[mPos] == ']') { ++mPos; break; }
            fail("expected , or ]");
            return std::nullopt;
        }
        return Json(std::move(items));
    }

    std::optional<Json> parseObject(int depth)
    {
        ++mPos;
        Json::Object members;
        skipSpace();
        if (mPos < mText.size() && mText[mPos] == '}')
        {
            ++mPos;
            return Json(std::move(members));
        }
        while (true)
        {
            skipSpace();
            if (mPos >= mText.size() || mText[mPos] != '"') { fail("expected a key"); return std::nullopt; }
            auto key = parseString();
            if (!key) return std::nullopt;
            skipSpace();
            if (mPos >= mText.size() || mText[mPos] != ':') { fail("expected :"); return std::nullopt; }
            ++mPos;
            skipSpace();
            auto v = parseValue(depth + 1);
            if (!v) return std::nullopt;
            members[std::move(*key)] = std::move(*v); // A repeated key keeps the last value.
            if (members.size() > mLimits.maxItems) { fail("object has more members than the limit"); return std::nullopt; }
            skipSpace();
            if (mPos < mText.size() && mText[mPos] == ',') { ++mPos; continue; }
            if (mPos < mText.size() && mText[mPos] == '}') { ++mPos; break; }
            fail("expected , or }");
            return std::nullopt;
        }
        return Json(std::move(members));
    }

    std::string_view mText;
    const JsonLimits& mLimits;
    std::size_t mPos = 0;
    std::string mError;
    std::size_t mErrorPos = 0;
};

void writeString(std::string& out, const std::string& s)
{
    out.push_back('"');
    for (const char c : s)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buf;
            }
            else
            {
                out.push_back(c);
            }
        }
    }
    out.push_back('"');
}

void writeNumber(std::string& out, double v)
{
    if (!std::isfinite(v))
    {
        out += "null";
        return;
    }
    if (v == std::floor(v) && std::fabs(v) < 1e15)
    {
        out += std::to_string(static_cast<long long>(v));
        return;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    out += buf;
}

void write(std::string& out, const Json& v, int indent)
{
    const std::string pad(static_cast<std::size_t>(indent) * 2, ' ');
    const std::string inner(static_cast<std::size_t>(indent + 1) * 2, ' ');
    switch (v.type())
    {
    case Json::Type::Null: out += "null"; break;
    case Json::Type::Bool: out += v.asBool() ? "true" : "false"; break;
    case Json::Type::Number: writeNumber(out, v.asNumber()); break;
    case Json::Type::String: writeString(out, v.asString()); break;
    case Json::Type::Array:
    {
        if (v.items().empty()) { out += "[]"; break; }
        out += "[\n";
        bool first = true;
        for (const Json& item : v.items())
        {
            if (!first) out += ",\n";
            first = false;
            out += inner;
            write(out, item, indent + 1);
        }
        out += "\n" + pad + "]";
        break;
    }
    case Json::Type::Object:
    {
        if (v.members().empty()) { out += "{}"; break; }
        out += "{\n";
        bool first = true;
        for (const auto& [key, item] : v.members())
        {
            if (!first) out += ",\n";
            first = false;
            out += inner;
            writeString(out, key);
            out += ": ";
            write(out, item, indent + 1);
        }
        out += "\n" + pad + "}";
        break;
    }
    }
}
} // namespace

int Json::asInt(int fallback) const
{
    if (!isNumber() || mNumber != std::floor(mNumber) ||
        mNumber < static_cast<double>(std::numeric_limits<int>::min()) ||
        mNumber > static_cast<double>(std::numeric_limits<int>::max()))
    {
        return fallback;
    }
    return static_cast<int>(mNumber);
}

const std::string& Json::asString() const
{
    return isString() ? mString : kEmptyString;
}

const Json::Array& Json::items() const
{
    return (isArray() && mArray) ? *mArray : kEmptyArray;
}

const Json::Object& Json::members() const
{
    return (isObject() && mObject) ? *mObject : kEmptyObject;
}

const Json& Json::operator[](std::string_view key) const
{
    if (!isObject() || !mObject)
    {
        return kNull;
    }
    const auto it = mObject->find(std::string(key));
    return it == mObject->end() ? kNull : it->second;
}

bool Json::has(std::string_view key) const
{
    return isObject() && mObject && mObject->count(std::string(key)) > 0;
}

Json& Json::set(const std::string& key, Json value)
{
    if (!isObject())
    {
        *this = Json(Object{});
    }
    if (mObject.use_count() > 1)
    {
        mObject = std::make_shared<Object>(*mObject);
    }
    (*mObject)[key] = std::move(value);
    return *this;
}

Json& Json::push(Json value)
{
    if (!isArray())
    {
        *this = Json(Array{});
    }
    if (mArray.use_count() > 1)
    {
        mArray = std::make_shared<Array>(*mArray);
    }
    mArray->push_back(std::move(value));
    return *this;
}

std::optional<Json> parseJson(std::string_view text, JsonError* error, const JsonLimits& limits)
{
    return Parser(text, limits).run(error);
}

std::string writeJson(const Json& value)
{
    std::string out;
    write(out, value, 0);
    out.push_back('\n');
    return out;
}

} // namespace sw67::io
