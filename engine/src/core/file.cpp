#include "valgo/core/file.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#include "valgo/core/log.h"

namespace fs = std::filesystem;

namespace valgo {
namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

}  // namespace

std::optional<std::string> ReadTextFile(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return std::nullopt;
    }
    std::string contents;
    char buffer[4096];
    usize read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        contents.append(buffer, read);
    }
    std::fclose(file);
    return contents;
}

std::optional<std::vector<u8>> ReadBinaryFile(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return std::nullopt;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(file);
        return std::nullopt;
    }
    std::vector<u8> data(static_cast<usize>(size));
    const usize read = data.empty() ? 0 : std::fread(data.data(), 1, data.size(), file);
    std::fclose(file);
    data.resize(read);
    return data;
}

bool WriteTextFile(const std::string& path, std::string_view contents) {
    EnsureParentDirectory(path);
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        LogError("file", "cannot open '{}' for writing", path);
        return false;
    }
    const usize written = std::fwrite(contents.data(), 1, contents.size(), file);
    std::fclose(file);
    return written == contents.size();
}

bool WriteBinaryFile(const std::string& path, const void* data, usize size) {
    EnsureParentDirectory(path);
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        LogError("file", "cannot open '{}' for writing", path);
        return false;
    }
    const usize written = size == 0 ? 0 : std::fwrite(data, 1, size, file);
    std::fclose(file);
    return written == size;
}

bool FileExists(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

bool DirectoryExists(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

bool EnsureDirectory(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        return true;
    }
    fs::create_directories(path, ec);
    return !ec;
}

bool EnsureParentDirectory(const std::string& path) {
    const std::filesystem::path parent = fs::path(path).parent_path();
    if (parent.empty()) {
        return true;
    }
    return EnsureDirectory(parent.string());
}

std::string JoinPath(std::string_view left, std::string_view right) {
    if (left.empty()) {
        return std::string(right);
    }
    if (right.empty()) {
        return std::string(left);
    }
    std::string result(left);
    const char last = result.back();
    if (last != '/' && last != '\\') {
        result.push_back('/');
    }
    result.append(right);
    return result;
}

std::string FileName(const std::string& path) {
    return fs::path(path).filename().string();
}

std::string FileStem(const std::string& path) {
    return fs::path(path).stem().string();
}

std::string FileExtension(const std::string& path) {
    return ToLower(fs::path(path).extension().string());
}

std::vector<std::string> ListFiles(const std::string& directory, std::string_view extension) {
    std::vector<std::string> files;
    std::error_code ec;
    if (!fs::is_directory(directory, ec)) {
        return files;
    }
    const std::string wanted = ToLower(std::string(extension));
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string ext = ToLower(entry.path().extension().string());
        if (!wanted.empty() && ext != wanted) {
            continue;
        }
        files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string ExecutableDirectory() {
#if defined(VALGO_PLATFORM_WINDOWS)
    // GetModuleFileNameW would be used here; the game sets its working directory
    // from the executable location in Application::Run before assets are loaded.
    return std::string();
#else
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (ec) {
        return std::string();
    }
    return exe.parent_path().string() + "/";
#endif
}

}  // namespace valgo
