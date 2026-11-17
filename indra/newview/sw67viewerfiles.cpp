/**
 * @file sw67viewerfiles.cpp
 * @brief File helpers shared by 6|7 World Studio's floaters.
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

#include "sw67viewerfiles.h"

#include "lldir.h"
#include "llfile.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace sw67viewer
{

std::string userDir(const std::string& sub)
{
    const std::string dir = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "sw67", sub);
    ensureDir(dir);
    return dir;
}

bool ensureDir(const std::string& dir)
{
    std::error_code ec;
    const std::filesystem::path p = fsyspath(dir);
    std::filesystem::create_directories(p, ec);
    return std::filesystem::is_directory(p, ec);
}

std::optional<std::string> readFile(const std::string& path, size_t maxBytes)
{
    std::error_code ec;
    const std::filesystem::path p = fsyspath(path);
    if (!std::filesystem::is_regular_file(p, ec))
    {
        return std::nullopt;
    }
    const auto size = std::filesystem::file_size(p, ec);
    if (ec || size > maxBytes)
    {
        return std::nullopt;
    }
    llifstream in(path.c_str(), std::ios::in | std::ios::binary);
    if (!in.is_open())
    {
        return std::nullopt;
    }
    std::string data(static_cast<size_t>(size), '\0');
    in.read(data.data(), static_cast<std::streamsize>(size));
    if (static_cast<size_t>(in.gcount()) != size)
    {
        return std::nullopt;
    }
    return data;
}

bool writeFileAtomic(const std::string& path, const std::string& data)
{
    const std::string tmp = path + ".tmp";
    {
        llofstream out(tmp.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            return false;
        }
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.close();
        if (out.fail())
        {
            LLFile::remove(tmp);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(fsyspath(tmp), fsyspath(path), ec);
    if (ec)
    {
        LLFile::remove(tmp);
        return false;
    }
    return true;
}

std::string joinPath(const std::string& dir, const std::string& name)
{
    return dir + gDirUtilp->getDirDelimiter() + name;
}

std::vector<std::string> listFiles(const std::string& dir, const std::string& suffix)
{
    std::vector<std::string> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(fsyspath(dir), ec), end; !ec && it != end; it.increment(ec))
    {
        if (!it->is_regular_file(ec))
        {
            continue;
        }
        const std::string name = fsyspath(it->path().filename()).string();
        if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
        {
            out.push_back(name);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace sw67viewer
