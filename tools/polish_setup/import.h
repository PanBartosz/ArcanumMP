#ifndef ARCMP_POLISH_IMPORT_H
#define ARCMP_POLISH_IMPORT_H
#include <filesystem>
#include <string>
#include <vector>
namespace polish {
namespace fs = std::filesystem;
inline constexpr const char* version = "1.0.0";
inline constexpr const char* archive_name = "Arcanum_UAP2.0.2PL.1.exe";
inline constexpr const char* archive_sha = "294a1c05a88c8f910a7d23327a4efebe0605f3026294c0a77c53185828e850f2";
inline constexpr uintmax_t archive_size = 192014985;
struct File {
    std::string path, sha256;
    uintmax_t bytes;
};
bool component_matches(const std::string& expression, bool uap);
// Does not publish or touch game data. The destination must not exist.
std::vector<File> import_pack(const fs::path& extracted, const fs::path& destination, bool uap);
// Atomically replace only languages/pl. Preserve any previous directory.
fs::path publish_pack(const fs::path& game, const fs::path& staged);
bool select_polish(const fs::path& game);
std::string unique_suffix();
}
#endif
