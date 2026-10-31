#include "coop/localization.h"
#include "coop/localization_codepages.inc"
#include "language_catalog.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
struct Language {
    std::string id, name;
    std::string display;
};
std::vector<Language> languages = { { "original", "Original (English)", { } }, { "pl", "Polski", { } } };
std::string active = "original";
unsigned pending;
const uint32_t* codepage = cp1252;
bool initialized;
unsigned canonical_depth;
CoopLanguageRead reader;
using Row = std::vector<std::string>;
using Resource = std::map<int, Row>;
std::map<std::string, Resource> resources;
std::map<std::string, std::string> ui;
struct Pattern {
    std::regex expression;
    std::string translated;
};
std::vector<Pattern> patterns;
std::map<std::string, std::string> translated_lines;
const std::regex printf_spec(R"(%[-+#0]*(?:[0-9]+)?(?:\.[0-9]+)?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcsp])");
bool format_pattern(const std::string& source, std::string& pattern)
{
    unsigned letters = 0;
    pattern = "^";
    for (size_t i = 0; i < source.size();) {
        if (source[i] == '%') {
            if (i + 1 < source.size() && source[i + 1] == '%') {
                pattern += '%';
                i += 2;
                continue;
            }
            std::smatch match;
            auto rest = source.substr(i);
            if (!std::regex_search(rest, match, printf_spec, std::regex_constants::match_continuous)) return false;
            pattern += "([\\s\\S]*?)";
            i += match.length();
            continue;
        }
        char c = source[i++];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) letters++;
        if (std::strchr(".^$|()[]{}*+?\\", c)) pattern += '\\';
        pattern += c;
    }
    pattern += '$';
    return letters >= 3;
}
std::map<const char*, CoopTextRef> sources;
std::map<std::string, CoopTextRef> rendered;
std::string canonical(std::string value)
{
    for (char& c : value) {
        if (c == '\\')
            c = '/';
        else if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    }
    if (value.empty() || value.size() >= COOP_TEXT_PATH || value.front() == '/' || value.find(':') != std::string::npos || value.find("..") != std::string::npos) return { };
    return value;
}
std::string read(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return { };
    file.seekg(0, std::ios::end);
    auto n = file.tellg();
    if (n < 0 || n > 16 * 1024 * 1024) return { };
    std::string bytes(static_cast<size_t>(n), '\0');
    file.seekg(0);
    file.read(bytes.data(), n);
    return file ? bytes : std::string();
}
/* The native formats are brace fields with // comments between records. Only
 * text fields are consumed: translated conditions and actions are ignored. */
Resource parse(const std::string& data, unsigned width)
{
    Resource rows;
    Row row;
    size_t at = 0;
    while (at < data.size()) {
        if (data[at] == '/' && at + 1 < data.size() && data[at + 1] == '/') {
            at = data.find('\n', at);
            if (at == std::string::npos) break;
            continue;
        }
        if (data[at++] != '{') continue;
        auto end = data.find('}', at);
        if (end == std::string::npos) break;
        row.push_back(data.substr(at, end - at));
        at = end + 1;
        if (row.size() == width) {
            char* tail = nullptr;
            long key = std::strtol(row[0].c_str(), &tail, 10);
            while (tail && *tail && std::isspace(static_cast<unsigned char>(*tail)))
                tail++;
            if (tail && !*tail && key >= 0 && key <= INT32_MAX && !rows.count(static_cast<int>(key))) rows.emplace(static_cast<int>(key), row);
            row.clear();
        }
    }
    return rows;
}
Resource& resource(const std::string& path, bool translated)
{
    std::string cache = (translated ? active : "original") + ":" + path;
    auto found = resources.find(cache);
    if (found != resources.end()) return found->second;
    std::string data;
    if (translated && active != "original")
        data = read(fs::path("languages") / active / path);
    else if (reader) {
        char* bytes = nullptr;
        size_t size = 0;
        if (reader(path.c_str(), &bytes, &size) && bytes && size <= 16 * 1024 * 1024) data.assign(bytes, size);
        std::free(bytes);
    }
    return resources.emplace(cache, parse(data, path.size() > 4 && path.substr(path.size() - 4) == ".dlg" ? 7 : 2)).first->second;
}
std::string template_text(const CoopTextRef& ref, bool translated)
{
    auto path = canonical(ref.path);
    if (path.empty()) return { };
    auto& rows = resource(path, translated);
    auto row = rows.find(ref.key);
    if (row == rows.end()) return { };
    unsigned column = ref.variant == 2 ? 2 : 1;
    if (column >= row->second.size()) return { };
    auto value = row->second[column];
    if (ref.variant == 2 && value.find_first_not_of(" \t\r\n") == std::string::npos) value = row->second[1];
    return value;
}
std::vector<std::string> tokens(const std::string& value)
{
    std::vector<std::string> result;
    for (size_t at = 0; (at = value.find('@', at)) != std::string::npos;) {
        auto end = value.find('@', at + 1);
        if (end == std::string::npos) {
            result.push_back("INVALID");
            break;
        }
        auto token = value.substr(at, end - at + 1);
        for (auto& c : token)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        result.push_back(token);
        at = end + 1;
    }
    std::sort(result.begin(), result.end());
    return result;
}
bool usable(const std::string& base, const std::string& value)
{
    return !value.empty() && value.size() < 1900 && value.find('\0') == std::string::npos
        && tokens(base) == tokens(value) && !(base.size() > 1 && base[1] == ':');
}
bool format_safe(const std::string& source, const std::string& translated)
{
    if (std::count(source.begin(), source.end(), '%') != std::count(translated.begin(), translated.end(), '%')) return false;
    static const std::regex dangerous(R"(%[0-9$ +#.*hljztL-]*n)");
    if (std::regex_search(translated, dangerous)) return false;
    std::vector<std::string> a, b;
    for (auto i = std::sregex_iterator(source.begin(), source.end(), printf_spec); i != std::sregex_iterator(); ++i)
        a.push_back(i->str());
    for (auto i = std::sregex_iterator(translated.begin(), translated.end(), printf_spec); i != std::sregex_iterator(); ++i)
        b.push_back(i->str());
    return a == b;
}
std::string unquote(const std::string& text)
{
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') return { };
    std::string out;
    for (size_t i = 1; i + 1 < text.size(); i++) {
        char c = text[i];
        if (c == '\\' && i + 2 < text.size()) {
            c = text[++i];
            if (c == 'n')
                c = '\n';
            else if (c == 'r')
                c = '\r';
            else if (c == 't')
                c = '\t';
        }
        out += c;
    }
    return out;
}
/* The original bitmap font indexes bytes. Polish catalog files are UTF-8;
 * translated retail files already use their language's original code page. */
std::string polish_bytes(const std::string& utf8)
{
    std::string out;
    for (size_t at = 0; at < utf8.size();) {
        unsigned c = static_cast<unsigned char>(utf8[at++]);
        if (c < 128) {
            out += char(c);
            continue;
        }
        unsigned count = c >= 0xF0 ? 3 : c >= 0xE0 ? 2
                                                   : 1,
                 code = c & ((1u << (6 - count)) - 1);
        while (count-- && at < utf8.size())
            code = (code << 6) | (static_cast<unsigned char>(utf8[at++]) & 63);
        auto found = std::find(codepage, codepage + 128, code);
        if (found != codepage + 128 && code != 0xFFFD)
            out += char(128 + (found - codepage));
        else if (code == 0x2014 || code == 0x2013)
            out += '-';
        else if (code == 0x2019 || code == 0x2018)
            out += '\'';
        else if (code == 0x201C || code == 0x201D)
            out += '"';
        else if (code <= 255)
            out += char(code);
        else
            out += '?';
    }
    return out;
}
void load_ui()
{
    std::string text = read(fs::path("languages") / active / "ui.tsv");
    if (text.empty() && active == "pl") text = reinterpret_cast<const char*>(builtin_polish_ui);
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        auto source = unquote(line.substr(0, tab)), value = unquote(line.substr(tab + 1));
        if (!source.empty() && !value.empty() && format_safe(source, value)) ui.emplace(source, polish_bytes(value));
    }
    for (auto& [source, value] : ui)
        if (source.find('%') != std::string::npos) {
            std::string pattern;
            if (format_pattern(source, pattern)) patterns.push_back({ std::regex(pattern), value });
        }
}
}
extern "C" void coop_language_init(void)
{
    if (initialized) return;
    initialized = true;
    std::error_code error;
    if (fs::is_directory("languages", error))
        for (auto& entry : fs::directory_iterator("languages", error)) {
            auto id = entry.path().filename().string();
            if (id == "pl" || id == "original" || id.size() > 15 || id.empty() || !std::all_of(id.begin(), id.end(), [](unsigned char c) { return (c >= 'a' && c <= 'z') || c == '-'; })) continue;
            auto name = read(entry.path() / "name.txt");
            if (name.empty()) continue;
            name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return c == '\n' || c == '\r'; }), name.end());
            if (name.size() < 48) languages.push_back({ id, name, { } });
        }
    std::sort(languages.begin() + 2, languages.end(), [](auto& a, auto& b) { return a.id < b.id; });
    auto value = read("arcmp-language.cfg");
    value.erase(std::remove_if(value.begin(), value.end(), [](char c) { return c == '\r' || c == '\n'; }), value.end());
    if (const char* override = std::getenv("ARCMP_LANGUAGE")) value = override;
    for (unsigned i = 0; i < languages.size(); i++)
        if (value == languages[i].id && coop_language_available(i)) {
            active = value;
            pending = i;
            break;
        }
    auto encoding = read(fs::path("languages") / active / "encoding.txt");
    codepage = encoding.find("1251") != std::string::npos || active == "ru" ? cp1251 : encoding.find("1250") != std::string::npos || active == "pl" ? cp1250
                                                                                                                                                    : cp1252;
    for (auto& language : languages) {
        language.display = polish_bytes(language.name);
        if (language.display.find('?') != std::string::npos) language.display = language.id;
    }
    load_ui();
}
extern "C" void coop_language_reader(CoopLanguageRead fn) { reader = fn; }
extern "C" unsigned coop_language_count(void)
{
    coop_language_init();
    return static_cast<unsigned>(languages.size());
}
extern "C" const char* coop_language_name(unsigned i)
{
    coop_language_init();
    return i < languages.size() ? languages[i].display.c_str() : "";
}
extern "C" unsigned coop_language_pending(void)
{
    coop_language_init();
    return pending;
}
extern "C" const char* coop_language_active(void)
{
    coop_language_init();
    return active.c_str();
}
extern "C" bool coop_language_select(unsigned i)
{
    coop_language_init();
    if (i >= languages.size() || !coop_language_available(i)) return false;
    {
        std::ofstream file("arcmp-language.cfg.tmp", std::ios::binary);
        file << languages[i].id << '\n';
        file.close();
        if (!file) return false;
    }
#ifdef _WIN32
    if (!MoveFileExW(L"arcmp-language.cfg.tmp", L"arcmp-language.cfg", MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return false;
#else
    std::error_code error;
    fs::rename("arcmp-language.cfg.tmp", "arcmp-language.cfg", error);
    if (error) return false;
#endif
    pending = i;
    return true;
}
extern "C" const char* coop_tr(const char* text)
{
    coop_language_init();
    if (!text) return "";
    auto found = ui.find(text);
    return found == ui.end() ? text : found->second.c_str();
}
extern "C" bool coop_language_available(unsigned i)
{
    coop_language_init();
    if (i >= languages.size()) return false;
    if (i == 0) return true;
    std::error_code error;
    auto root = fs::path("languages") / languages[i].id;
    return fs::is_directory(root / "mes", error) && fs::is_directory(root / "dlg", error);
}
extern "C" const char* coop_tr_rendered(const char* text)
{
    const char* fixed = coop_tr(text);
    if (!text || active == "original" || fixed != text) return fixed;
    auto cached = translated_lines.find(text);
    if (cached != translated_lines.end()) return cached->second.c_str();
    for (auto& pattern : patterns) {
        std::cmatch values;
        if (!std::regex_match(text, values, pattern.expression)) continue;
        std::string result;
        unsigned argument = 1;
        for (size_t i = 0; i < pattern.translated.size();) {
            if (pattern.translated[i] != '%') {
                result += pattern.translated[i++];
                continue;
            }
            if (i + 1 < pattern.translated.size() && pattern.translated[i + 1] == '%') {
                result += '%';
                i += 2;
                continue;
            }
            std::smatch spec;
            auto rest = pattern.translated.substr(i);
            if (!std::regex_search(rest, spec, printf_spec, std::regex_constants::match_continuous) || argument >= values.size()) {
                result.clear();
                break;
            }
            auto value = values[argument++].str();
            result += coop_tr(value.c_str());
            i += spec.length();
        }
        if (!result.empty() && argument == values.size()) {
            /* Callers may use several returned strings in one printf. Keep
             * their storage for the process lifetime, just like the UI
             * catalog; clearing here could invalidate an earlier argument. */
            return translated_lines.emplace(text, result).first->second.c_str();
        }
    }
    return text;
}
extern "C" bool coop_language_ref_valid(const CoopTextRef* ref)
{
    if (!ref || !std::memchr(ref->path, 0, sizeof(ref->path)) || ref->key < 0 || ref->variant > 2) return false;
    if (!*ref->path) return ref->key == 0 && ref->variant == 0;
    auto path = canonical(ref->path);
    if (path.empty() || path != ref->path || std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32 || c >= 127; })) return false;
    return ref->variant ? path.rfind("dlg/", 0) == 0 && path.size() > 4 && path.substr(path.size() - 4) == ".dlg"
                        : path.rfind("mes/", 0) == 0 && path.size() > 4 && path.substr(path.size() - 4) == ".mes";
}
extern "C" void coop_language_source(const char* pointer, const char* path, int key, unsigned variant)
{
    if (!pointer || !path) return;
    auto name = canonical(path);
    CoopTextRef ref = { };
    ref.key = key;
    ref.variant = static_cast<uint8_t>(variant);
    if (name.size() >= sizeof(ref.path)) return;
    std::memcpy(ref.path, name.c_str(), name.size() + 1);
    if (coop_language_ref_valid(&ref)) sources[pointer] = ref;
}
extern "C" void coop_language_record(const char* result, const char* source)
{
    auto found = sources.find(source);
    if (!result || !*result) return;
    if (found == sources.end()) {
        rendered.erase(result);
        return;
    }
    if (rendered.size() > 8192) rendered.clear();
    rendered[result] = found->second;
}
extern "C" CoopTextRef coop_language_reference(const char* text)
{
    if (text) {
        auto source = sources.find(text);
        if (source != sources.end()) return source->second;
        auto found = rendered.find(text);
        if (found != rendered.end()) return found->second;
    }
    return { };
}
extern "C" void coop_language_remember(const char* result, const CoopTextRef* reference)
{
    if (result && *result && coop_language_ref_valid(reference) && *reference->path) rendered[result] = *reference;
}
extern "C" const char* coop_language_message(const char* filename, int key, const char* original)
{
    coop_language_init();
    auto path = canonical(filename ? filename : "");
    if (path.rfind("mes/", 0) != 0 || !original) return original;
    coop_language_source(original, path.c_str(), key, 0);
    if (active == "original" || path.rfind("mes/gd_", 0) == 0) return original;
    auto& rows = resource(path, true);
    auto row = rows.find(key);
    if (row == rows.end() || row->second.size() != 2 || !usable(original, row->second[1]) || !format_safe(original, row->second[1])) return original;
    /* Numeric/configuration entries retain their authoritative values. */
    if (std::all_of(original, original + std::strlen(original), [](unsigned char c) { return !std::isalpha(c); })) return original;
    if (std::strlen(original) < 128 && !std::strchr(original, '%') && !std::strchr(original, '@')) ui.try_emplace(original, row->second[1]);
    coop_language_source(row->second[1].c_str(), path.c_str(), key, 0);
    return canonical_depth ? original : row->second[1].c_str();
}
extern "C" const char* coop_language_original(const char* text)
{
    auto found = sources.find(text);
    if (found == sources.end()) return text;
    auto ref = found->second;
    auto& rows = resource(ref.path, false);
    auto row = rows.find(ref.key);
    unsigned column = ref.variant == 2 ? 2 : 1;
    if (row == rows.end() || row->second.size() <= column) return text;
    const char* value = row->second[column].c_str();
    sources[value] = ref;
    return value;
}
extern "C" void coop_language_canonical_begin(void) { canonical_depth++; }
extern "C" void coop_language_canonical_end(void)
{
    if (canonical_depth) canonical_depth--;
}
extern "C" bool coop_language_resolve(const CoopTextRef* ref, const char* pc, const char* npc, const char* rendered_text, char* out, size_t size)
{
    coop_language_init();
    if (!out || !size || !coop_language_ref_valid(ref) || !*ref->path) return false;
    auto base = template_text(*ref, false);
    if (base.empty()) return false;
    auto text = active == "original" ? base : template_text(*ref, true);
    if (!usable(base, text) || !format_safe(base, text)) text = base;
    /* Prices and [skill/spell] choices are formatted by the authoritative
     * engine. Capture its displayed arguments without running a printf from
     * translated data or losing the quoted amount. */
    if (std::regex_search(base, printf_spec)) {
        std::string expression;
        if (!rendered_text || !format_pattern(base, expression)) return false;
        std::cmatch values;
        if (!std::regex_match(rendered_text, values, std::regex(expression))) return false;
        std::string formatted;
        unsigned argument = 1;
        for (size_t i = 0; i < text.size();) {
            if (text[i] != '%') {
                formatted += text[i++];
                continue;
            }
            if (i + 1 < text.size() && text[i + 1] == '%') {
                formatted += '%';
                i += 2;
                continue;
            }
            std::smatch spec;
            auto rest = text.substr(i);
            if (!std::regex_search(rest, spec, printf_spec, std::regex_constants::match_continuous) || argument >= values.size()) return false;
            auto value = values[argument++].str();
            formatted += coop_tr(value.c_str());
            i += spec.length();
        }
        if (argument != values.size()) return false;
        text = formatted;
    }
    std::string result;
    for (size_t at = 0; at < text.size();) {
        if (text[at] == '@') {
            auto end = text.find('@', at + 1);
            if (end == std::string::npos) return false;
            auto token = text.substr(at + 1, end - at - 1);
            for (char& c : token)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            if (token == "pcname")
                result += pc ? pc : "";
            else if (token == "npcname")
                result += npc ? npc : "";
            else
                return false;
            at = end + 1;
        } else
            result += text[at++];
    }
    if (result.size() >= size) return false;
    std::memcpy(out, result.c_str(), result.size() + 1);
    return true;
}
extern "C" bool coop_language_asset(const char* filename, char* out, size_t capacity)
{
    coop_language_init();
    if (active == "original" || !filename || !out) return false;
    auto path = canonical(filename);
    if (path == "art/morph15font.art") path = "art/interface/morph15font.art";
    auto extension = fs::path(path).extension().string();
    bool allowed = (path.rfind("art/interface/", 0) == 0 && extension == ".art")
        || (path.rfind("movies/", 0) == 0 && (extension == ".bik" || extension == ".srt"))
        || (path.rfind("sound/speech/", 0) == 0 && (extension == ".mp3" || extension == ".wav"));
    if (!allowed) return false;
    auto file = fs::path("languages") / active / path;
    std::error_code error;
    auto value = file.generic_string();
    if (value.size() >= capacity || !fs::is_regular_file(file, error)) return false;
    std::memcpy(out, value.c_str(), value.size() + 1);
    return true;
}
