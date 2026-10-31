#include "../sha256.hpp"
#include "import.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#define CHECK(x)                                         \
    do {                                                 \
        if (!(x)) {                                      \
            std::cerr << __LINE__ << ": " << #x << '\n'; \
            std::exit(1);                                \
        }                                                \
    } while (0)
namespace fs = std::filesystem;
static void write(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
    CHECK(bool(file));
}
int main()
{
    auto temp = fs::temp_directory_path() / ("arcmp-polish-test-" + polish::unique_suffix());
    fs::create_directory(temp);
    CHECK(polish::component_matches("main\\stock and not main\\UAP", false));
    CHECK(!polish::component_matches("main\\stock and not main\\UAP", true));
    CHECK(polish::component_matches("main and (main\\uap or main\\stock)", true));
    CHECK(!polish::component_matches("main or garbage trailing", false));
    CHECK(!polish::component_matches("main and (", false));
    auto raw = temp / "raw";
    write(raw / "modules/arcanum/dlg/npc.dlg", "// comment\n{1}{English/default}{Female}{0}{gf99 1}{999}{EFFECT}\n");
    write(raw / "modules/arcanum/dlg/npc.dlg#main\\UAP", "{1}{UAP}{UAP female}{1}{}{2}{SCRIPT}");
    write(raw / "data/mes/name.mes", "{1}{common}");
    write(raw / "data/mes/name.mes#main/stock", "{1}{stock}"); // Windows's renamed component path.
    write(raw / "data/art/interface/font.art", "border");
    write(raw / "data/art/interface/font.art$0", "no border");
    write(raw / "data/rules/backgrnd.mes", "{1}{GAMEPLAY}");
    write(raw / "data/scr/logic.scr", "SCRIPT");
    write(raw / "modules/other/dlg/npc.dlg", "{1}{WRONG}{}{}{}{}{}");
    auto pack = temp / "pack";
    auto files = polish::import_pack(raw, pack, false);
    CHECK(files.size() == 3);
    CHECK(Sha256::file(pack / "dlg/npc.dlg") == Sha256::text("{1}{English/default}{Female}{}{}{}{}\n"));
    CHECK(Sha256::file(pack / "mes/name.mes") == Sha256::text("{1}{stock}"));
    CHECK(Sha256::file(pack / "art/interface/font.art") == Sha256::text("no border"));
    auto uap = temp / "uap";
    polish::import_pack(raw, uap, true);
    CHECK(Sha256::file(uap / "dlg/npc.dlg") == Sha256::text("{1}{UAP}{UAP female}{}{}{}{}\n"));
    auto game = temp / "game";
    write(game / "data/control", "game data");
    write(game / "languages/pl/custom.txt", "preserve me");
    write(game / "arcmp-language.cfg", "original\n");
    auto backup = polish::publish_pack(game, pack);
    CHECK(!backup.empty() && fs::exists(backup / "custom.txt") && fs::exists(game / "languages/pl/dlg/npc.dlg"));
    CHECK(polish::select_polish(game));
    CHECK(Sha256::file(game / "arcmp-language.cfg") == Sha256::text("pl\n"));
    CHECK(Sha256::file(game / "arcmp-language.cfg.before-polish") == Sha256::text("original\n"));
    CHECK(Sha256::file(game / "data/control") == Sha256::text("game data"));
    bool rejected = false;
    try {
        polish::publish_pack(game, temp / "missing");
    } catch (const std::exception&) {
        rejected = true;
    }
    CHECK(rejected && fs::exists(game / "languages/pl/dlg/npc.dlg"));
    auto invalid = temp / "invalid";
    write(invalid / "mes/test.mes", "{1}{a}{1}{b}");
    write(invalid / "dlg/test.dlg", "{1}{a}{}{}{}{}{}");
    rejected = false;
    try {
        polish::import_pack(invalid, temp / "failed", false);
    } catch (const std::exception&) {
        rejected = true;
    }
    CHECK(rejected && !fs::exists(temp / "failed"));
    fs::remove_all(temp);
    std::cout << "PASS: component selection, Windows paths, text-only import, backups, rollback and personal language selection\n";
}
