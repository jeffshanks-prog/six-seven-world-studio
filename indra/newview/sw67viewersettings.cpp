/**
 * @file sw67viewersettings.cpp
 * @brief Typed text access to viewer settings for 6|7's tools.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "llviewerprecompiledheaders.h"

#include "sw67viewersettings.h"

#include "llagent.h"
#include "llcontrol.h"
#include "llformat.h"
#include "llsdutil.h"
#include "llstartup.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>

using sw67::workspace::PrefValue;

namespace sw67viewer
{
namespace
{
std::optional<PrefValue::Type> typeOf(LLControlVariable& c)
{
    switch (c.type())
    {
    case TYPE_BOOLEAN: return PrefValue::Type::Bool;
    case TYPE_S32:
    case TYPE_U32: return PrefValue::Type::Int;
    case TYPE_F32: return PrefValue::Type::Float;
    case TYPE_STRING: return PrefValue::Type::String;
    default: return std::nullopt;
    }
}

struct Collector : public LLControlGroup::ApplyFunctor
{
    std::vector<std::pair<std::string, PrefValue>> out;
    void apply(const std::string& name, LLControlVariable* control) override
    {
        if (!control || !control->isPersisted() || !control->shouldSave(true) ||
            llsd_equals(control->getValue(), control->getDefault()))
        {
            return;
        }
        if (auto v = valueOf(*control))
        {
            out.emplace_back(name, std::move(*v));
        }
    }
};
} // namespace

std::optional<PrefValue> valueOf(LLControlVariable& c)
{
    const auto type = typeOf(c);
    if (!type)
    {
        return std::nullopt;
    }
    const LLSD v = c.getValue();
    switch (*type)
    {
    case PrefValue::Type::Bool: return PrefValue{*type, v.asBoolean() ? "1" : "0"};
    case PrefValue::Type::Int: return PrefValue{*type, std::to_string(v.asInteger())};
    case PrefValue::Type::Float: return PrefValue{*type, llformat("%.9g", v.asReal())};
    case PrefValue::Type::String: return PrefValue{*type, v.asString()};
    }
    return std::nullopt;
}

std::optional<PrefValue> readSetting(LLControlGroup& group, const std::string& name)
{
    LLControlVariablePtr c = group.getControl(name);
    return c ? valueOf(*c) : std::nullopt;
}

bool writeSetting(LLControlGroup& group, const std::string& name, const PrefValue& value)
{
    LLControlVariablePtr c = group.getControl(name);
    if (!c || typeOf(*c) != value.type)
    {
        return false;
    }
    switch (value.type)
    {
    case PrefValue::Type::Bool:
        if (value.value != "0" && value.value != "1") return false;
        c->setValue(LLSD(value.value == "1"));
        return true;
    case PrefValue::Type::Int:
    {
        char* end = nullptr;
        errno = 0;
        const long long n = std::strtoll(value.value.c_str(), &end, 10);
        if (value.value.empty() || !end || *end != '\0' || errno == ERANGE) return false;
        const long long lo = c->type() == TYPE_U32 ? 0LL : -2147483648LL;
        c->setValue(LLSD(static_cast<LLSD::Integer>(llclamp(n, lo, 2147483647LL))));
        return true;
    }
    case PrefValue::Type::Float:
    {
        char* end = nullptr;
        const double d = std::strtod(value.value.c_str(), &end);
        if (value.value.empty() || !end || *end != '\0' || !std::isfinite(d)) return false;
        c->setValue(LLSD(d));
        return true;
    }
    case PrefValue::Type::String: c->setValue(LLSD(value.value)); return true;
    }
    return false;
}

std::vector<std::pair<std::string, PrefValue>> changedSettings(LLControlGroup& group)
{
    Collector collect;
    group.applyToAll(&collect);
    return std::move(collect.out);
}

bool accountSettingsReady()
{
    return LLStartUp::getStartupState() >= STATE_STARTED && gAgent.getID().notNull();
}
} // namespace sw67viewer
