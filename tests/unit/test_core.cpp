// =============================================================================
// Core: formatting, logging, randomness, files, images, frame timing.
// =============================================================================
#include "../framework/test_framework.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "valgo/core/file.h"
#include "valgo/core/format.h"
#include "valgo/core/image.h"
#include "valgo/core/log.h"
#include "valgo/core/random.h"
#include "valgo/core/time.h"

using namespace valgo;

namespace {

/// Captures log traffic so tests never depend on stdout.
class MemorySink final : public ILogSink {
public:
    void Write(const LogMessage& message) override { messages.push_back(message); }
    std::vector<LogMessage> messages;
};

std::string TempPath(const char* name) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "valgo_tests";
    std::filesystem::create_directories(directory);
    return (directory / name).string();
}

}  // namespace

// ---------------------------------------------------------------------------
// Format
// ---------------------------------------------------------------------------
VALGO_TEST("core", "format_substitutes_placeholders_in_order") {
    VALGO_REQUIRE(Format("{} + {} = {}", 1, 2, 3) == std::string("1 + 2 = 3"));
    VALGO_CHECK_EQ(Format("no placeholders"), std::string("no placeholders"));
    VALGO_CHECK_EQ(Format("{}", "text"), std::string("text"));
    VALGO_CHECK_EQ(Format("{}", true), std::string("true"));
    VALGO_CHECK_EQ(Format("seen {}", LogLevelName(LogLevel::Warn)), std::string("seen warn"));
    // Unsupported printf-style specifiers are preserved verbatim (documented
    // limitation: Format is positional-only).
    VALGO_CHECK_EQ(Format("{:.1f}", 1.0f), std::string("{:.1f}"));
}

VALGO_TEST("core", "format_handles_extra_and_missing_arguments") {
    VALGO_CHECK_EQ(Format("{}", 1, 2, 3), std::string("1"));
    VALGO_CHECK_EQ(Format("{} {}"), std::string("{} {}"));
    VALGO_CHECK_EQ(Format("{}", static_cast<const void*>(nullptr)), ToString(static_cast<const void*>(nullptr)));
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
VALGO_TEST("core", "logger_filters_by_level_and_keeps_history") {
    Logger& logger = Logger::Get();
    MemorySink sink;
    logger.AddSink(&sink);
    const LogLevel previous = logger.level();
    logger.SetLevel(LogLevel::Info);

    LogDebug("test", "debug message {}", 1);
    LogInfo("test", "info message {}", 2);
    LogError("test", "error message {}", 3);
    logger.SetLevel(previous);
    logger.RemoveSink(&sink);

    VALGO_REQUIRE(sink.messages.size() == static_cast<usize>(2));
    VALGO_CHECK_EQ(sink.messages[0].text, std::string("info message 2"));
    VALGO_CHECK_EQ(sink.messages[0].category, std::string("test"));
    VALGO_CHECK_EQ(sink.messages[1].level, LogLevel::Error);

    const std::vector<LogMessage> recent = logger.RecentMessages(8);
    VALGO_CHECK(!recent.empty());
    VALGO_CHECK_EQ(recent.back().text, std::string("error message 3"));
}

VALGO_TEST("core", "logger_history_is_bounded") {
    Logger& logger = Logger::Get();
    const LogLevel previous = logger.level();
    logger.SetLevel(LogLevel::Trace);
    for (int i = 0; i < 600; ++i) {
        LogTrace("bounded", "entry {}", i);
    }
    const std::vector<LogMessage> recent = logger.RecentMessages(1000);
    logger.SetLevel(previous);
    VALGO_CHECK_MSG(recent.size() <= 256u, "log history ring buffer is unbounded");
    VALGO_CHECK_EQ(recent.back().text, std::string("entry 599"));
}

// ---------------------------------------------------------------------------
// Random
// ---------------------------------------------------------------------------
VALGO_TEST("core", "random_is_deterministic_per_seed") {
    Random a(1337);
    Random b(1337);
    Random c(1338);
    bool differs = false;
    for (int i = 0; i < 64; ++i) {
        const u32 left = a.NextU32();
        const u32 right = b.NextU32();
        VALGO_CHECK_EQ(left, right);
        differs = differs || left != c.NextU32();
    }
    VALGO_CHECK_MSG(differs, "different seeds produced identical streams");
}

VALGO_TEST("core", "random_ranges_are_respected") {
    Random rng(99);
    for (int i = 0; i < 5000; ++i) {
        const f32 unit = rng.NextFloat();
        VALGO_CHECK(unit >= 0.0f && unit < 1.0f);
        const f32 ranged = rng.NextFloat(-4.0f, 7.0f);
        VALGO_CHECK(ranged >= -4.0f && ranged < 7.0f);
        const i32 integer = rng.NextInt(3, 9);
        VALGO_CHECK(integer >= 3 && integer < 9);
    }
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------
VALGO_TEST("core", "file_roundtrip_and_path_helpers") {
    const std::string text_path = TempPath("roundtrip.txt");
    VALGO_REQUIRE(WriteTextFile(text_path, std::string_view("valgo engine")));
    VALGO_CHECK(FileExists(text_path));
    const std::optional<std::string> text = ReadTextFile(text_path);
    VALGO_REQUIRE(text.has_value());
    VALGO_CHECK_EQ(*text, std::string("valgo engine"));

    const std::vector<u8> payload{1, 2, 3, 4, 250};
    const std::string binary_path = TempPath("roundtrip.bin");
    VALGO_REQUIRE(WriteBinaryFile(binary_path, payload.data(), payload.size()));
    const std::optional<std::vector<u8>> binary = ReadBinaryFile(binary_path);
    VALGO_REQUIRE(binary.has_value());
    VALGO_CHECK(binary->size() == payload.size());
    VALGO_CHECK_EQ((*binary)[4], static_cast<u8>(250));

    VALGO_CHECK(!ReadTextFile(TempPath("does_not_exist.txt")).has_value());

    VALGO_CHECK_EQ(JoinPath("assets", "meshes/crate.obj"), std::string("assets/meshes/crate.obj"));
    VALGO_CHECK_EQ(FileName("a/b/crate.obj"), std::string("crate.obj"));
    VALGO_CHECK_EQ(FileStem("a/b/crate.obj"), std::string("crate"));
    VALGO_CHECK_EQ(FileExtension("a/b/crate.OBJ"), std::string(".obj"));
    std::filesystem::remove(text_path);
    std::filesystem::remove(binary_path);
}

// ---------------------------------------------------------------------------
// Images
// ---------------------------------------------------------------------------
VALGO_TEST("core", "image_layout_and_checksum") {
    Image image(4, 3, 10, 20, 30, 255);
    VALGO_REQUIRE(image.byte_size() == static_cast<usize>(4 * 3 * 4));
    VALGO_CHECK_EQ(image.At(0, 0)[0], static_cast<u8>(10));
    VALGO_CHECK_EQ(image.At(3, 2)[2], static_cast<u8>(30));

    const u64 checksum = image.Checksum();
    image.At(1, 1)[0] = 200;
    VALGO_CHECK_MSG(image.Checksum() != checksum, "checksum ignored a pixel change");
}

VALGO_TEST("core", "png_encoding_produces_a_valid_stream") {
    Image image(8, 4, 255, 0, 0, 255);
    const std::vector<u8> png = EncodePng(image.width, image.height, image.pixels.data());
    VALGO_REQUIRE(!png.empty());

    const std::vector<u8> signature{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    VALGO_CHECK(std::equal(signature.begin(), signature.end(), png.begin()));

    // IHDR must be the first chunk and declare the right dimensions.
    VALGO_CHECK_EQ(std::string(reinterpret_cast<const char*>(png.data() + 12), 4), std::string("IHDR"));
    const auto read_be32 = [&](usize offset) {
        return (static_cast<u32>(png[offset]) << 24) | (static_cast<u32>(png[offset + 1]) << 16) |
               (static_cast<u32>(png[offset + 2]) << 8) | static_cast<u32>(png[offset + 3]);
    };
    VALGO_CHECK_EQ(read_be32(16), 8u);
    VALGO_CHECK_EQ(read_be32(20), 4u);

    // Every chunk CRC must be correct or decoders reject the file.
    u32 expected_crc = 0xFFFFFFFFu;
    const auto update = [&](u32 crc, u8 byte) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) != 0u ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
        return crc;
    };
    usize offset = 8;
    int chunk_count = 0;
    while (offset + 12 <= png.size()) {
        const u32 length = read_be32(offset);
        expected_crc = 0xFFFFFFFFu;
        for (usize i = 0; i < length + 4; ++i) {
            expected_crc = update(expected_crc, png[offset + 4 + i]);
        }
        const u32 stored = read_be32(offset + 8 + length);
        VALGO_CHECK_EQ(stored, expected_crc ^ 0xFFFFFFFFu);
        offset += 12 + length;
        ++chunk_count;
    }
    VALGO_CHECK_EQ(offset, png.size());
    VALGO_CHECK_MSG(chunk_count >= 2, "PNG is missing chunks");

    const std::string path = TempPath("encode.png");
    VALGO_REQUIRE(image.SavePng(path));
    const std::optional<std::vector<u8>> written = ReadBinaryFile(path);
    VALGO_REQUIRE(written.has_value());
    VALGO_CHECK(written->size() == png.size());
    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
VALGO_TEST("core", "frame_timer_produces_stable_fixed_steps") {
    FrameTimer timer(1.0 / 60.0);
    const FrameContext first = timer.Advance();
    VALGO_CHECK_EQ(first.frame_index, static_cast<u64>(0));
    VALGO_CHECK_MSG(first.fixed_steps <= 1u, "first frame produced extra simulation steps");
    VALGO_CHECK(first.fixed_step_seconds > 0.016f && first.fixed_step_seconds < 0.017f);
    VALGO_CHECK(first.interpolation_alpha >= 0.0f && first.interpolation_alpha <= 1.0f);

    // Advancing many frames must keep total simulated time monotonic. The timer
    // reads the real clock, so the test has to actually let time pass.
    f64 previous_total = first.total_seconds;
    u32 total_steps = 0;
    for (int i = 0; i < 60; ++i) {
        SleepMilliseconds(2);
        const FrameContext frame = timer.Advance();
        VALGO_CHECK(frame.total_seconds >= previous_total);
        VALGO_CHECK_MSG(frame.fixed_steps <= 16u, "frame timer produced a stall spiral");
        previous_total = frame.total_seconds;
        total_steps += frame.fixed_steps;
    }
    VALGO_CHECK_MSG(previous_total > 0.05, "frame timer did not accumulate elapsed time");
    VALGO_CHECK_MSG(total_steps > 0u, "frame timer never produced a simulation step");
}

VALGO_TEST("core", "stopwatch_measures_elapsed_time") {
    Stopwatch watch;
    SleepMilliseconds(6);
    const f64 elapsed = watch.Seconds();
    VALGO_CHECK(elapsed >= 0.003);
    VALGO_CHECK(elapsed < 2.0);
    VALGO_CHECK(watch.Milliseconds() > 0.0);
    const f64 before = MonotonicNowSeconds();
    VALGO_CHECK(MonotonicNowNanoseconds() > 0u);
    VALGO_CHECK(MonotonicNowSeconds() >= before);
}
