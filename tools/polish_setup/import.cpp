#include "import.h"
#include "../sha256.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <climits>
#include <map>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace polish {
namespace {
    std::string lower(std::string s)
    {
        for (char& c : s)
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return s;
    }
    struct Expression {
        const std::string& text;
        bool uap;
        size_t at = 0;
        void space()
        {
            while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])))
                at++;
        }
        bool word(const char* s)
        {
            space();
            size_t n = std::char_traits<char>::length(s);
            if (text.compare(at, n, s) || (at + n < text.size() && (std::isalnum(static_cast<unsigned char>(text[at + n])) || text[at + n] == '_' || text[at + n] == '\\'))) return false;
            at += n;
            return true;
        }
        bool atom()
        {
            space();
            if (word("not")) return !atom();
            if (at < text.size() && text[at] == '(') {
                at++;
                bool v = expression();
                space();
                if (at == text.size() || text[at++] != ')') throw std::runtime_error("component");
                return v;
            }
            size_t start = at;
            while (at < text.size() && (std::isalnum(static_cast<unsigned char>(text[at])) || text[at] == '_' || text[at] == '\\'))
                at++;
            if (start == at) throw std::runtime_error("component");
            auto id = text.substr(start, at - start);
            return id == "main" || id == (uap ? "main\\uap" : "main\\stock");
        }
        bool term()
        {
            bool value = atom();
            while (word("and")) {
                bool next = atom();
                value = value && next;
            }
            return value;
        }
        bool expression()
        {
            bool value = term();
            while (word("or")) {
                bool next = term();
                value = value || next;
            }
            return value;
        }
    };
    using Rows = std::map<int, std::vector<std::string>>;
    Rows fields(const std::string& bytes, unsigned width)
    {
        Rows rows;
        std::vector<std::string> values;
        size_t at = 0;
        while (at < bytes.size()) {
            if (bytes.compare(at, 2, "//") == 0) {
                auto n = bytes.find('\n', at);
                at = n == std::string::npos ? bytes.size() : n + 1;
                continue;
            }
            if (bytes[at++] != '{') continue;
            auto end = bytes.find('}', at);
            if (end == std::string::npos) throw std::runtime_error("Niezamkniete pole tekstu.");
            values.push_back(bytes.substr(at, end - at));
            at = end + 1;
            if (values.size() != width) continue;
            const auto& id = values[0];
            size_t pos = 0;
            while (pos < id.size() && std::isspace(static_cast<unsigned char>(id[pos])))
                pos++;
            uint64_t key = 0;
            size_t first = pos;
            while (pos < id.size() && id[pos] >= '0' && id[pos] <= '9') {
                key = key * 10 + id[pos++] - '0';
                if (key > INT_MAX) throw std::runtime_error("Za duzy numer tekstu.");
            }
            if (pos == first) throw std::runtime_error("Nieprawidlowy numer tekstu.");
            while (pos < id.size() && std::isspace(static_cast<unsigned char>(id[pos])))
                pos++;
            if (pos != id.size() || !rows.emplace(static_cast<int>(key), values).second) throw std::runtime_error("Powtorzony lub nieprawidlowy numer tekstu.");
            values.clear();
        }
        if (!values.empty()) throw std::runtime_error("Niepelny rekord tekstu.");
        return rows;
    }
    std::string read(const fs::path& path)
    {
        auto size = fs::file_size(path);
        if (size > 256 * 1024 * 1024) throw std::runtime_error("Plik danych jest zbyt duzy.");
        std::ifstream file(path, std::ios::binary);
        std::string data(static_cast<size_t>(size), '\0');
        file.read(data.data(), static_cast<std::streamsize>(size));
        if (!file) throw std::runtime_error("Nie mozna odczytac: " + path.u8string());
        return data;
    }
    void write(const fs::path& path, const std::string& value)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file.write(value.data(), static_cast<std::streamsize>(value.size()));
        file.close();
        if (!file) throw std::runtime_error("Nie mozna zapisac: " + path.u8string());
    }
    std::string quote(const std::string& text)
    {
        std::string out = "\"";
        for (unsigned char c : text) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += c;
            } else if (c == '\n')
                out += "\\n";
            else if (c == '\r')
                out += "\\r";
            else if (c == '\t')
                out += "\\t";
            else if (c < 32)
                throw std::runtime_error("Nieprawidlowa nazwa pliku.");
            else
                out += c;
        }
        return out + '"';
    }
    bool starts(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }
    bool candidate(const std::string& relative, bool uap, std::string& key, int& priority)
    {
        auto path = lower(relative);
        auto hash = path.find('#');
        if (hash != std::string::npos) {
            auto component = path.substr(hash + 1);
            // Windows treats a component suffix's backslash as a path separator.
            std::replace(component.begin(), component.end(), '/', '\\');
            if (!component_matches(component, uap)) return false;
            path.resize(hash);
        }
        if (starts(path, "app/")) path.erase(0, 4);
        if (starts(path, "data/"))
            path.erase(0, 5);
        else if (starts(path, "modules/arcanum/"))
            path.erase(0, 16);
        else if (starts(path, "modules/"))
            return false;
        auto dollar = path.find('$');
        bool duplicate = dollar != std::string::npos;
        if (duplicate) path.resize(dollar);
        if (path.empty() || path.front() == '/' || path.find(':') != std::string::npos || path.find("..") != std::string::npos || path.find('\\') != std::string::npos) return false;
        auto ext = fs::u8path(path).extension().string();
        bool allowed = (starts(path, "dlg/") && ext == ".dlg") || (starts(path, "mes/") && ext == ".mes")
            || (starts(path, "art/interface/") && ext == ".art") || (starts(path, "movies/") && (ext == ".bik" || ext == ".srt"))
            || (starts(path, "sound/speech/") && (ext == ".mp3" || ext == ".wav"));
        if (!allowed) return false;
        key = path;
        priority = (hash != std::string::npos ? 20 : 0) + (duplicate ? 1 : 0);
        return true;
    }
}
bool component_matches(const std::string& value, bool uap)
{
    try {
        auto normalized = lower(value);
        Expression parser { normalized, uap };
        bool result = parser.expression();
        parser.space();
        return parser.at == normalized.size() && result;
    } catch (const std::exception&) {
        return false;
    }
}
std::string unique_suffix()
{
    static unsigned counter;
    return std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "-" + std::to_string(counter++);
}
std::vector<File> import_pack(const fs::path& extracted, const fs::path& destination, bool uap)
{
    if (!fs::is_directory(extracted) || fs::exists(destination)) throw std::runtime_error("Brak danych do importu lub katalog docelowy juz istnieje.");
    std::map<std::string, std::pair<int, fs::path>> selected;
    std::vector<fs::path> paths;
    for (const auto& entry : fs::recursive_directory_iterator(extracted)) {
        if (entry.is_symlink()) continue;
        if (entry.is_regular_file()) paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());
    for (const auto& path : paths) {
        std::string key;
        int priority;
        if (candidate(path.lexically_relative(extracted).generic_u8string(), uap, key, priority)) {
            auto found = selected.find(key);
            if (found == selected.end() || priority > found->second.first) selected[key] = { priority, path };
        }
    }
    unsigned dialogues = 0, messages = 0;
    for (const auto& entry : selected) {
        dialogues += fs::u8path(entry.first).extension() == ".dlg";
        messages += fs::u8path(entry.first).extension() == ".mes";
    }
    if (!dialogues || !messages) throw std::runtime_error("Nie znaleziono polskich tekstow kampanii.");
    fs::create_directories(destination);
    std::vector<File> files;
    try {
        for (const auto& [key, entry] : selected) {
            auto data = read(entry.second);
            auto ext = fs::u8path(key).extension().string();
            if (ext == ".dlg") {
                auto rows = fields(data, 7);
                data.clear();
                for (const auto& [id, row] : rows)
                    data += "{" + std::to_string(id) + "}{" + row[1] + "}{" + row[2] + "}{}{}{}{}\n";
            } else if (ext == ".mes")
                fields(data, 2);
            write(destination / fs::u8path(key), data);
            files.push_back({ key, Sha256::text(data), data.size() });
        }
        write(destination / "name.txt", "Polski\n");
        write(destination / "encoding.txt", "cp1250\n");
        std::ostringstream json;
        json << "{\n  \"language\": \"pl\",\n  \"name\": \"Polski\",\n  \"base_variant\": \"" << (uap ? "uap" : "stock") << "\",\n  \"encoding\": \"cp1250\",\n  \"presentation_only\": true,\n  \"setup_version\": " << quote(version) << ",\n  \"source_sha256\": " << quote(archive_sha) << ",\n  \"files\": [\n";
        for (size_t i = 0; i < files.size(); i++) {
            const auto& f = files[i];
            json << "    {\"path\": " << quote(f.path) << ", \"bytes\": " << f.bytes << ", \"sha256\": " << quote(f.sha256) << "}" << (i + 1 < files.size() ? "," : "") << "\n";
        }
        json << "  ]\n}\n";
        write(destination / "pack.json", json.str());
        write(destination / "SOURCE.txt", "Third-party translation data, kept separate from ArcanumMP.\nEselter Arcanum UAP2.0.2PL 1.7 uses the original Polish localization.\nhttps://www.moddb.com/mods/spolszczenie-arcanum/downloads/arcanum-uap20pl\nOriginal translation and art retain their respective authors/terms.\nThe engine license does not relicense these assets.\nPresentation resources only; no translated gameplay is used.\n");
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(destination, ignored);
        throw;
    }
    return files;
}
fs::path publish_pack(const fs::path& game, const fs::path& staged)
{
    auto languages = game / "languages", target = languages / "pl";
    if (fs::is_symlink(fs::symlink_status(languages)) || fs::is_symlink(fs::symlink_status(target))) throw std::runtime_error("Folder languages/pl jest dowiazaniem. Nie zmieniono jego zawartosci.");
    if (fs::exists(target) && !fs::is_directory(target)) throw std::runtime_error("languages/pl nie jest folderem.");
    fs::create_directories(languages);
    fs::path backup;
    if (fs::exists(target)) {
        backup = languages / (".pl-backup-" + unique_suffix());
        fs::rename(target, backup);
    }
    try {
        fs::rename(staged, target);
    } catch (...) {
        if (!backup.empty()) {
            std::error_code error;
            fs::rename(backup, target, error);
        }
        throw;
    }
    return backup;
}
bool select_polish(const fs::path& game)
{
    auto temporary = game / ("arcmp-language.cfg.tmp-" + unique_suffix());
    std::error_code error;
    try {
        write(temporary, "pl\n");
        if (fs::exists(game / "arcmp-language.cfg")) fs::copy_file(game / "arcmp-language.cfg", game / "arcmp-language.cfg.before-polish", fs::copy_options::overwrite_existing);
#ifdef _WIN32
        bool good = MoveFileExW(temporary.c_str(), (game / "arcmp-language.cfg").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        fs::rename(temporary, game / "arcmp-language.cfg", error);
        bool good = !error;
#endif
        fs::remove(temporary, error);
        return good;
    } catch (const std::exception&) {
        fs::remove(temporary, error);
        return false;
    }
}
}
