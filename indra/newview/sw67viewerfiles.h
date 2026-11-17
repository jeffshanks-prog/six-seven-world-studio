/**
 * @file sw67viewerfiles.h
 * @brief File helpers shared by 6|7 World Studio's floaters: where our files
 *        live, and reading and writing them safely (a write goes to a
 *        temporary file first and replaces the old one only once complete).
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

#include <optional>
#include <string>
#include <vector>

namespace sw67viewer
{
/// <user settings>/sw67/<sub>, created if missing.
std::string userDir(const std::string& sub);

/// Creates `dir` and its parents. True if it exists afterwards.
bool ensureDir(const std::string& dir);

/// The whole file, or nullopt if it can't be read or is over `maxBytes`.
std::optional<std::string> readFile(const std::string& path, size_t maxBytes = 8u * 1024u * 1024u);

/// Writes `data` to `path` through a temporary file in the same folder.
bool writeFileAtomic(const std::string& path, const std::string& data);

/// Joins with the platform's separator.
std::string joinPath(const std::string& dir, const std::string& name);

/// Files (not folders) directly in `dir` whose names end with `suffix`.
std::vector<std::string> listFiles(const std::string& dir, const std::string& suffix);
} // namespace sw67viewer
