// =============================================================================
// Project Valgo — engine core: file system helpers.
//
// Intentionally minimal and portable (std::filesystem). The asset pipeline and
// the screenshot writer are the current consumers; a virtual file system with
// pak support is a later engine milestone.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "valgo/core/types.h"

namespace valgo {

/// Reads a whole text file. Returns std::nullopt when the file cannot be read.
std::optional<std::string> ReadTextFile(const std::string& path);

/// Reads a whole binary file. Returns std::nullopt when the file cannot be read.
std::optional<std::vector<u8>> ReadBinaryFile(const std::string& path);

bool WriteTextFile(const std::string& path, std::string_view contents);
bool WriteBinaryFile(const std::string& path, const void* data, usize size);

bool FileExists(const std::string& path);
bool DirectoryExists(const std::string& path);

/// Creates the directory containing `path` (and missing parents).
bool EnsureParentDirectory(const std::string& path);
bool EnsureDirectory(const std::string& path);

/// Joins two path fragments with the native separator and normalises them.
std::string JoinPath(std::string_view left, std::string_view right);

std::string FileName(const std::string& path);
std::string FileStem(const std::string& path);
std::string FileExtension(const std::string& path);  ///< Lower-case, including the dot.

/// Lists files with the given extension (case-insensitive, e.g. ".obj") inside
/// `directory`. Non-recursive. Returns an empty vector when the directory does
/// not exist.
std::vector<std::string> ListFiles(const std::string& directory, std::string_view extension);

/// Directory of the running executable, with a trailing separator.
std::string ExecutableDirectory();

}  // namespace valgo
