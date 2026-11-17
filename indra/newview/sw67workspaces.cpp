/**
 * @file sw67workspaces.cpp
 * @brief Workspace profiles in the viewer.
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

#include "sw67workspaces.h"

#include "sw67viewerfiles.h"

#include "sw67/photo/product.h" // safeFileStem

#include "lldir.h"
#include "llfile.h"
#include "llfloater.h"
#include "llfloaterreg.h"
#include "llformat.h"
#include "lltoolbarview.h"
#include "llviewercontrol.h"
#include "llviewerinput.h"

using namespace sw67::workspace;

namespace sw67viewer
{
namespace
{
constexpr char kSuffix[] = ".sw67workspace.json";

std::string toolbarsPath()
{
    // Per account: only meaningful once logged in.
    return gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "toolbars.xml");
}

std::string keyBindingsPath()
{
    return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "key_bindings.xml");
}

bool haveAccountDir()
{
    return !gDirUtilp->getLindenUserDir().empty();
}

LLFloater* openFloaterNamed(const std::string& name)
{
    if (!gFloaterView)
    {
        return nullptr;
    }
    for (LLView* child : *gFloaterView->getChildList())
    {
        LLFloater* f = dynamic_cast<LLFloater*>(child);
        if (f && f->getInstanceName() == name && f->getVisible())
        {
            return f;
        }
    }
    return nullptr;
}

std::optional<Rect> rectOf(LLFloater* f)
{
    if (!f)
    {
        return std::nullopt;
    }
    const LLRect r = f->getRect();
    return Rect{r.mLeft, r.mTop, r.getWidth(), r.getHeight()};
}

std::optional<PrefValue> readPref(const std::string& name)
{
    LLControlVariablePtr c = gSavedSettings.getControl(name);
    if (!c)
    {
        return std::nullopt;
    }
    const LLSD v = c->getValue();
    switch (c->type())
    {
    case TYPE_BOOLEAN: return PrefValue{PrefValue::Type::Bool, v.asBoolean() ? "1" : "0"};
    case TYPE_S32:
    case TYPE_U32: return PrefValue{PrefValue::Type::Int, std::to_string(v.asInteger())};
    case TYPE_F32: return PrefValue{PrefValue::Type::Float, llformat("%g", v.asReal())};
    case TYPE_STRING: return PrefValue{PrefValue::Type::String, v.asString()};
    default: return std::nullopt;
    }
}

bool writePref(const std::string& name, const PrefValue& value)
{
    LLControlVariablePtr c = gSavedSettings.getControl(name);
    if (!c)
    {
        return false;
    }
    switch (value.type)
    {
    case PrefValue::Type::Bool: c->setValue(LLSD(value.value == "1")); return true;
    case PrefValue::Type::Int:
    {
        const long n = std::strtol(value.value.c_str(), nullptr, 10);
        c->setValue(LLSD(static_cast<LLSD::Integer>(llclamp(n, -2147483647L, 2147483647L))));
        return true;
    }
    case PrefValue::Type::Float: c->setValue(LLSD(std::strtod(value.value.c_str(), nullptr))); return true;
    case PrefValue::Type::String: c->setValue(LLSD(value.value)); return true;
    }
    return false;
}

std::string fileFor(const std::string& name)
{
    return joinPath(workspacesDir(), sw67::photo::safeFileStem(name) + kSuffix);
}
} // namespace

CurrentState captureWorkspace(const WorkspaceProfile& profile)
{
    CurrentState s;
    auto note = [&s](const std::string& name) {
        if (s.windows.count(name) || !LLFloaterReg::isRegistered(name))
        {
            return;
        }
        LLFloater* f = openFloaterNamed(name);
        if (!f)
        {
            f = LLFloaterReg::findInstance(name);
        }
        WindowLayout w;
        w.name = name;
        w.open = f && f->getVisible();
        w.rect = rectOf(f);
        s.windows[name] = std::move(w);
    };
    for (const WindowLayout& w : profile.windows)
    {
        note(w.name);
    }
    if (gFloaterView)
    {
        for (LLView* child : *gFloaterView->getChildList())
        {
            LLFloater* f = dynamic_cast<LLFloater*>(child);
            if (f && f->getVisible() && !f->getInstanceName().empty())
            {
                note(f->getInstanceName());
            }
        }
    }
    for (const auto& [name, v] : profile.preferences)
    {
        (void)v;
        if (auto now = readPref(name))
        {
            s.preferences[name] = *now;
        }
    }
    if (haveAccountDir())
    {
        s.toolbarsXml = readFile(toolbarsPath(), 256 * 1024).value_or("");
    }
    s.keyBindingsXml = readFile(keyBindingsPath(), 256 * 1024).value_or("");
    return s;
}

WorkspaceProfile profileFromCurrent(const std::string& name)
{
    WorkspaceProfile p;
    p.name = name;
    p.kind = ProfileKind::Custom;
    p.description = "Saved from the viewer's layout.";
    p.closeOthers = true;
    if (gFloaterView)
    {
        for (LLView* child : *gFloaterView->getChildList())
        {
            LLFloater* f = dynamic_cast<LLFloater*>(child);
            if (!f || !f->getVisible() || f->getInstanceName().empty() || !LLFloaterReg::isRegistered(f->getInstanceName()))
            {
                continue;
            }
            if (f->getInstanceName() == "sw67_workspaces")
            {
                continue; // The window doing the saving.
            }
            p.windows.push_back({f->getInstanceName(), true, rectOf(f)});
        }
    }
    if (haveAccountDir())
    {
        p.toolbarsXml = readFile(toolbarsPath(), 256 * 1024).value_or("");
    }
    p.keyBindingsXml = readFile(keyBindingsPath(), 256 * 1024).value_or("");
    sanitize(p);
    return p;
}

int applyWorkspacePlan(const std::vector<Change>& plan, const WorkspaceProfile& profile)
{
    int failed = 0;
    auto rectFor = [&profile](const std::string& name) -> std::optional<Rect> {
        for (const WindowLayout& w : profile.windows)
        {
            if (w.name == name) return w.rect;
        }
        return std::nullopt;
    };
    auto place = [](LLFloater* f, const Rect& r) {
        f->setShape(LLRect(r.left, r.top, r.left + r.width, r.top - r.height));
    };
    for (const Change& c : plan)
    {
        switch (c.what)
        {
        case Change::What::OpenWindow:
        case Change::What::MoveWindow:
        {
            LLFloater* f = LLFloaterReg::showInstance(c.target);
            if (!f)
            {
                ++failed;
                break;
            }
            if (const auto r = rectFor(c.target))
            {
                place(f, *r);
            }
            break;
        }
        case Change::What::CloseWindow:
        {
            if (LLFloater* f = openFloaterNamed(c.target))
            {
                f->closeFloater();
            }
            else if (!LLFloaterReg::hideInstance(c.target))
            {
                ++failed;
            }
            break;
        }
        case Change::What::SetPreference:
        {
            const auto it = profile.preferences.find(c.target);
            if (it == profile.preferences.end() || !writePref(c.target, it->second))
            {
                ++failed;
            }
            break;
        }
        case Change::What::ReplaceToolbars:
            if (!haveAccountDir() || !writeFileAtomic(toolbarsPath(), profile.toolbarsXml) || !gToolBarView ||
                !gToolBarView->loadToolbars(false))
            {
                ++failed;
            }
            break;
        case Change::What::ReplaceShortcuts:
            if (!writeFileAtomic(keyBindingsPath(), profile.keyBindingsXml) ||
                gViewerInput.loadBindingsXML(keyBindingsPath()) <= 0)
            {
                ++failed;
            }
            break;
        case Change::What::SkipUnknownWindow:
        case Change::What::SkipPreference:
            break;
        }
    }
    return failed;
}

std::string workspacesDir()
{
    return userDir("workspaces");
}

std::vector<WorkspaceProfile> savedWorkspaces()
{
    std::vector<WorkspaceProfile> out;
    const std::string dir = workspacesDir();
    for (const std::string& file : listFiles(dir, kSuffix))
    {
        if (const auto text = readFile(joinPath(dir, file), 2 * 1024 * 1024))
        {
            ImportResult r = importProfile(*text);
            if (r.profile)
            {
                out.push_back(std::move(*r.profile));
            }
            else
            {
                LL_WARNS("SW67") << "Skipped workspace file " << file << LL_ENDL;
            }
        }
    }
    return out;
}

bool saveWorkspace(const WorkspaceProfile& profile, std::vector<Note>* notes)
{
    return writeFileAtomic(fileFor(profile.name), exportProfile(profile, notes));
}

bool deleteWorkspace(const std::string& name)
{
    return LLFile::remove(fileFor(name)) == 0;
}

} // namespace sw67viewer
