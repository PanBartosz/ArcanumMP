#include "coop/content.h"
#include "coop/content_identity.h"
#include "../../tools/sha256.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <regex>
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
    std::vector<std::array<uint8_t, 32>> legacy;
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

/* These are not resources loaded by the campaign. In particular, native map
 * preprocessing and tile-variant generation write files inside repositories;
 * being below data/ or modules/ does not make a file immutable game content. */
bool runtime_directory(const std::string& name)
{
    return name == "tigcache" || name == "players" || name == "save" || name == "saves"
        || name == "__pycache__" || (!name.empty() && name.front() == '.');
}

bool ignored(const fs::path& relative)
{
    std::vector<std::string> parts;
    for (const auto& part : relative) {
        auto name = lower(part.u8string());
        if (runtime_directory(name)) return true;
        parts.push_back(name);
    }
    if (parts.empty()) return true;
    const auto& name = parts.back();
    if (name == "thumbs.db") return true;
    if (parts.size() >= 3 && parts[parts.size() - 3] == "art" && parts[parts.size() - 2] == "tile"
        && name == "tilevariant.dat") return true;
    if ((parts.size() == 4 && parts[0] == "modules" && parts[2] == "maps")
        || (parts.size() == 3 && parts[0] == "data" && parts[1] == "maps")) return true;
    const auto ext = lower(relative.extension().u8string());
    for (const char* excluded : {".bik", ".mp3", ".wav", ".cfg", ".ini", ".log", ".tmp", ".bak", ".old", ".orig", ".deleted", ".md"})
        if (ext == excluded) return true;
    return name.back() == '~' || name.find(".tmp-") != std::string::npos;
}

struct Asset { fs::path path; std::string key; uint64_t size; fs::file_time_type modified; };
std::vector<Asset> inventory(const fs::path& root)
{
    std::vector<Asset> assets;
    auto add = [&](const fs::path& path) {
        auto relative = path.lexically_relative(root);
        if (ignored(relative)) return;
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
            for (fs::recursive_directory_iterator it(entry.path()), end; it != end; ++it) {
                if (job.cancel) return {};
                if (it->is_directory()) {
                    if (runtime_directory(lower(it->path().filename().u8string()))) it.disable_recursion_pending();
                } else if (it->is_regular_file()) add(it->path());
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

std::string read_manifest(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (size < 0 || size > 64 * 1024 * 1024) return {};
    std::string text(size_t(size), '\0');
    file.seekg(0);
    file.read(text.data(), size);
    if (!file) return {};
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return text;
}

/* Accept a retired identity only when its recorded manifest, after removing
 * ignored files, is byte-for-byte the freshly verified current manifest. Never
 * relax a peer's network ID or accept a real script/map/prototype difference. */
std::string filtered_manifest(const std::string& text)
{
    static const std::regex row(R"ROW(\{"path":"((?:[^"\\]|\\["\\])*)","sha256":"[0-9a-f]{64}","size":[0-9]+\})ROW");
    std::string result = "[";
    if (text.size() < 2 || text.front() != '[' || text.back() != ']') return {};
    size_t at = 1;
    while (at + 1 < text.size()) {
        std::cmatch match;
        if (!std::regex_search(text.c_str() + at, match, row, std::regex_constants::match_continuous)) return {};
        std::string path;
        const auto quoted = match[1].str();
        for (size_t i = 0; i < quoted.size(); i++) {
            if (quoted[i] == '\\') i++;
            path += quoted[i];
        }
        if (!ignored(fs::u8path(path))) {
            if (result.size() > 1) result += ',';
            result += match.str();
        }
        at += match.length();
        if (at + 1 == text.size()) break;
        if (text[at++] != ',' || at + 1 == text.size()) return {};
    }
    return result + ']';
}

void restore_legacy(const fs::path& root, const std::string& digest, const std::string& previous = {})
{
    job.legacy.clear();
    auto add = [&](const std::string& text) {
        std::array<uint8_t, 32> id{};
        if (text != digest && decode(text, id.data()) && job.legacy.size() < COOP_CONTENT_LEGACY_MAX
            && std::find(job.legacy.begin(), job.legacy.end(), id) == job.legacy.end()) job.legacy.push_back(id);
    };
    std::ifstream file(root / "arcmp-content.compat", std::ios::binary);
    std::string version, base, line;
    std::getline(file, version); std::getline(file, base);
    if (version == "ACCONTENT-COMPAT1" && (base == digest || (!previous.empty() && base == previous)))
        while (std::getline(file, line)) add(line);
    if (!previous.empty()) add(previous);
}

void save_legacy(const fs::path& root, const std::string& digest)
{
    std::string text = "ACCONTENT-COMPAT1\n" + digest + '\n';
    for (const auto& id : job.legacy) {
        for (const auto byte : id) {
            char pair[3]; std::snprintf(pair, sizeof(pair), "%02x", byte); text += pair;
        }
        text += '\n';
    }
    write_atomic(root / "arcmp-content.compat", text);
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
        if (version == "ACCONTENT2" && previous_stamp == stamp && previous_id == stored_id
            && complete && decode(stored_id, decoded)) {
            restore_legacy(root, stored_id);
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
        auto previous_manifest = read_manifest(root / "arcmp-content.json");
        const auto retired = filtered_manifest(previous_manifest) == manifest ? Sha256::text(previous_manifest) : std::string();
        restore_legacy(root, digest, retired);
        save_legacy(root, digest);
        write_atomic(root / "arcmp-content.json", manifest + '\n');
        write_atomic(root / "arcmp-content.id", digest + '\n');
        write_atomic(root / "arcmp-content.cache", "ACCONTENT2\n" + digest + '\n' + stamp + '\n');
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
    coop_content_legacy_set(job.id, job.legacy.empty() ? nullptr : job.legacy.front().data(), job.legacy.size());
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
    job.legacy.clear();
    coop_content_legacy_set(nullptr, nullptr, 0);
}
