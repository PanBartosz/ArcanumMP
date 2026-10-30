#include "coop/content.h"
#include "../../tools/sha256.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
struct ContentJob {
    std::mutex mutex;
    std::thread worker;
    std::atomic<bool> cancel{false};
    int state = 0; // idle, working, ready, failed
    uint64_t done = 0, total = 0;
    uint8_t id[32]{};
    std::string error;
    ~ContentJob() { cancel = true; if (worker.joinable()) worker.join(); }
} job;

std::string lower(std::string text)
{
    for (char& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return text;
}

bool decode(std::string hex, uint8_t id[32])
{
    if (!hex.empty() && hex.back() == '\n') hex.pop_back();
    if (!hex.empty() && hex.back() == '\r') hex.pop_back();
    if (hex.size() != 64) return false;
    for (size_t i = 0; i < 64; i++) {
        unsigned char c = hex[i];
        unsigned n = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 16;
        if (n == 16) return false;
        if (!(i & 1)) id[i / 2] = uint8_t(n << 4);
        else id[i / 2] |= uint8_t(n);
    }
    return true;
}

std::string quote(const std::string& text)
{
    std::string result = "\"";
    for (unsigned char c : text) {
        if (c < 32 || c >= 128) throw std::runtime_error("Game asset filenames must use ASCII characters.");
        if (c == '\\' || c == '"') result += '\\';
        result += char(c);
    }
    return result + '"';
}

void write_atomic(const fs::path& path, const std::string& text)
{
    fs::path temporary = path;
    temporary += ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out.write(text.data(), text.size());
    out.close();
    if (!out) throw std::runtime_error("Cannot save game fingerprint. Use a writable game folder.");
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot save game fingerprint. Use a writable game folder.");
#else
    fs::rename(temporary, path);
#endif
}

struct Asset { fs::path path; std::string key; uint64_t size; fs::file_time_type modified; };
std::vector<Asset> inventory(const fs::path& root)
{
    std::vector<Asset> assets;
    auto add = [&](const fs::path& path) {
        auto relative = path.lexically_relative(root);
        std::vector<std::string> parts;
        for (const auto& part : relative) {
            auto name = lower(part.u8string());
            if (name == "tigcache" || name == "players" || name == "save" || name == "saves") return;
            parts.push_back(name);
        }
        if (parts.size() == 4 && parts[0] == "modules" && parts[2] == "maps" && relative.extension().empty()) return;
        auto ext = lower(relative.extension().u8string());
        if (ext == ".bik" || ext == ".mp3" || ext == ".wav") return;
        auto key = lower(relative.generic_u8string());
        quote(key);
        uint64_t size = fs::file_size(path);
        assets.push_back({path, key, size, fs::last_write_time(path)});
    };
    for (const auto& entry : fs::directory_iterator(root)) {
        if (job.cancel) return {};
        auto name = lower(entry.path().filename().u8string());
        if (entry.is_regular_file() && lower(entry.path().extension().u8string()) == ".dat") add(entry.path());
        if (entry.is_directory() && (name == "modules" || name == "data")) {
            for (const auto& child : fs::recursive_directory_iterator(entry.path())) {
                if (job.cancel) return {};
                if (child.is_regular_file()) add(child.path());
            }
        }
    }
    std::sort(assets.begin(), assets.end(), [](const Asset& a, const Asset& b) { return a.key < b.key; });
    for (size_t i = 1; i < assets.size(); i++)
        if (assets[i - 1].key == assets[i].key) throw std::runtime_error("Duplicate game asset names differ only by letter case.");
    for (const char* required : {"arcanum1.dat", "arcanum2.dat", "arcanum3.dat", "arcanum4.dat", "tig.dat", "modules/arcanum.dat"})
        if (std::none_of(assets.begin(), assets.end(), [&](const Asset& a) { return a.key == required; }))
            throw std::runtime_error(std::string("Game file missing: ") + required);
    return assets;
}

std::string inventory_id(const std::vector<Asset>& assets)
{
    Sha256 hash;
    for (const auto& asset : assets) {
        auto elapsed = asset.modified.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed - seconds);
        auto row = quote(asset.key) + ":" + std::to_string(asset.size) + ":"
            + std::to_string(seconds.count()) + ":" + std::to_string(nanos.count()) + "\n";
        hash.update(row.data(), row.size());
    }
    return hash.finish();
}

void generate(fs::path root)
{
    try {
        auto assets = inventory(root);
        if (job.cancel) return;
        auto stamp = inventory_id(assets);
        /* The timestamp inventory is local cache metadata, never a network
         * identity. Old fingerprints without it are rehashed once. A changed
         * asset, addition/removal or a torn cache write forces a full hash. */
        std::ifstream cached(root / "arcmp-content.cache", std::ios::binary);
        std::ifstream identity(root / "arcmp-content.id", std::ios::binary);
        std::string version, previous_id, previous_stamp, stored_id;
        std::getline(cached, version); std::getline(cached, previous_id);
        std::getline(cached, previous_stamp); std::getline(identity, stored_id);
        bool complete = cached.peek() == EOF && identity.peek() == EOF;
        cached.close(); identity.close();
        uint8_t decoded[32];
        if (version == "ACCONTENT1" && previous_stamp == stamp && previous_id == stored_id
            && complete && decode(stored_id, decoded)) {
            std::lock_guard<std::mutex> guard(job.mutex);
            std::memcpy(job.id, decoded, sizeof(decoded)); job.state = 2;
            return;
        }
        uint64_t total = 0;
        for (const auto& asset : assets) total += asset.size;
        { std::lock_guard<std::mutex> guard(job.mutex); job.total = total; }
        std::string manifest = "[";
        std::array<char, 65536> buffer;
        for (const auto& asset : assets) {
            std::ifstream in(asset.path, std::ios::binary);
            if (!in) throw std::runtime_error("Cannot read game data. Check the game folder.");
            Sha256 hash;
            uint64_t bytes = 0;
            while (in) {
                if (job.cancel) return;
                in.read(buffer.data(), buffer.size());
                size_t count = size_t(in.gcount());
                hash.update(buffer.data(), count);
                bytes += count;
                std::lock_guard<std::mutex> guard(job.mutex);
                job.done += count;
            }
            if (!in.eof() || bytes != asset.size) throw std::runtime_error("Game data changed while reading. Restart the game.");
            if (manifest.size() > 1) manifest += ',';
            manifest += "{\"path\":" + quote(asset.key) + ",\"sha256\":\"" + hash.finish() + "\",\"size\":" + std::to_string(bytes) + "}";
        }
        manifest += ']';
        auto digest = Sha256::text(manifest);
        auto after = inventory(root);
        if (job.cancel) return;
        if (inventory_id(after) != stamp) throw std::runtime_error("Game data changed while reading. Restart the game.");
        write_atomic(root / "arcmp-content.json", manifest + '\n');
        write_atomic(root / "arcmp-content.id", digest + '\n');
        write_atomic(root / "arcmp-content.cache", "ACCONTENT1\n" + digest + '\n' + stamp + '\n');
        std::lock_guard<std::mutex> guard(job.mutex);
        decode(digest, job.id);
        job.state = 2;
    } catch (const std::exception& error) {
        std::lock_guard<std::mutex> guard(job.mutex);
        job.error = error.what();
        job.state = 3;
    }
}
}

extern "C" bool coop_content_read(uint8_t id[32])
{
    if (!id) return false;
    std::lock_guard<std::mutex> guard(job.mutex);
    if (job.state == 0) {
        job.state = 1;
        job.cancel = false;
        try { job.worker = std::thread(generate, fs::current_path()); }
        catch (const std::exception& error) { job.state = 3; job.error = error.what(); }
    }
    if (job.state != 2) return false;
    std::memcpy(id, job.id, 32);
    return true;
}

extern "C" bool coop_content_status(char* message, size_t size)
{
    std::lock_guard<std::mutex> guard(job.mutex);
    if (job.state == 1) std::snprintf(message, size, "Checking game files for multiplayer... %u%%", job.total ? unsigned(job.done * 100 / job.total) : 0);
    else if (job.state == 3) std::snprintf(message, size, "%s", job.error.c_str());
    else { if (size) message[0] = 0; return false; }
    return true;
}

extern "C" void coop_content_shutdown(void)
{
    job.cancel = true;
    if (job.worker.joinable()) job.worker.join();
    std::lock_guard<std::mutex> guard(job.mutex);
    job.state = 0;
    job.done = job.total = 0;
    job.error.clear();
}
