#include "coop/dialogue.h"
#include "coop/localization.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#define CHECK(x)                                                         \
    do {                                                                 \
        if (!(x)) {                                                      \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
            std::exit(1);                                                \
        }                                                                \
    } while (0)
namespace fs = std::filesystem;
static void write(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
    CHECK(bool(file));
}
static bool original(const char* path, char** out, size_t* size)
{
    std::string text;
    if (!std::strcmp(path, "dlg/example.dlg"))
        text = "{1}{Hello, @pcname@!}{Welcome, @pcname@!}{0}{}{2}{set_global(7,1)}\n{2}{Leave.}{}{1}{}{0}{}\n";
    else if (!std::strcmp(path, "mes/example.mes"))
        text = "{1}{Life}{2}{42}{3}{Health %d}{4}{Hello, @npcname@!}{5}{g:1000}{6}{It costs %d coins.}";
    else
        return false;
    *size = text.size();
    *out = static_cast<char*>(std::malloc(*size));
    CHECK(*out);
    std::memcpy(*out, text.data(), *size);
    return true;
}
int main(int argc, char** argv)
{
    CHECK(argc == 2);
    bool polish = !std::strcmp(argv[1], "pl");
    const auto cwd = fs::current_path();
    auto temporary = fs::temp_directory_path() / ("arcmp-language-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    fs::create_directory(temporary);
    fs::current_path(temporary);
    write("languages/pl/dlg/example.dlg", "{1}{Witaj, @pcname@!}{Witaj pani, @pcname@!}{99}{MALICIOUS_CONDITION}{999}{MALICIOUS_ACTION}\n{2}{Odejd"
                                          "\x9f"
                                          ".}{}{99}{}{999}{MALICIOUS_ACTION}\n");
    write("languages/pl/mes/example.mes", "{1}{\xaf"
                                          "ycie}{2}{99}{3}{Zdrowie %s}{4}{Witaj, @pcname@!}{5}{nie uruchamiaj}{6}{Koszt to %d monet.}");
    write("languages/pl/art/interface/morph15font.art", "font fixture");
    write("languages/pl/movies/intro.srt", "subtitle fixture");
    write("languages/pl/ui.tsv", "\"Local override\"\t\"Wlasny wpis\"\n\"Close\"\t\"Zamknij lokalnie\"\n");
    write("languages/fr/name.txt", "Francais\n");
    write("languages/../invalid/name.txt", "not a language\n");
#ifdef _WIN32
    _putenv_s("ARCMP_LANGUAGE", argv[1]);
#else
    setenv("ARCMP_LANGUAGE", argv[1], 1);
#endif
    coop_language_reader(original);
    coop_language_init();
    CHECK(coop_language_count() == 3 && coop_language_available(0) && coop_language_available(1));
    CHECK(!coop_language_available(2));
    CHECK(!std::strcmp(coop_language_active(), argv[1]));
    CHECK(!std::strcmp(coop_tr("Empty"), polish ? "Pusto" : "Empty"));
    CHECK(!std::strcmp(coop_tr("Poisoned"), polish ? "Zatrucie" : "Poisoned"));
    CHECK(!std::strcmp(coop_tr("Local override"), polish ? "Wlasny wpis" : "Local override"));
    CHECK(!std::strcmp(coop_tr("Close"), polish ? "Zamknij lokalnie" : "Close"));
    for (const char* status : {
             "Choose Host or Join to assemble your party.", "Disconnected.",
             "Hosting. Friends can join using your LAN IPv4 address.", "Connecting to the host...",
             "Connected. Choose your character and ready up.",
             "Connected to the running party. Waiting for campaign synchronization.",
             "Game content differs from the host. Use the same campaign data and patches.",
             "Game content differs from the host. Compare arcmp-content.json in both game folders.",
             "Connection refused or unreachable. Check the address, port and host firewall.",
             "This player profile is already connected.", "The party is full (four players).",
             "Incompatible co-op protocol. Use the same Arcanum MP build.",
             "The host disconnected.", "Campaign snapshot loading timed out.",
             "World object replication failed; resynchronization is required.",
             "Character updated.", "Party map note updated.", "< Previous" }) {
        CHECK((std::strcmp(coop_tr_rendered(status), status) != 0) == polish);
    }
    for (const char* status : { "Checking game files for multiplayer... 37%",
             "Connection lost. Rejoining your party automatically (23s).",
             "Preparing the shared story: 2/4 travellers ready." }) {
        CHECK((std::strcmp(coop_tr_rendered(status), status) != 0) == polish);
    }
    char progress[256];
    std::snprintf(progress, sizeof(progress), coop_tr("Checking game files for multiplayer... %u%%"), 37u);
    CHECK(!std::strcmp(coop_tr_rendered("Checking game files for multiplayer... 37%"), progress));
    CHECK(!std::strcmp(coop_language_message("mes/example.mes", 1, "Life"), polish ? "\xaf"
                                                                                     "ycie"
                                                                                   : "Life"));
    CHECK(!std::strcmp(coop_language_message("mes/example.mes", 2, "42"), "42"));
    CHECK(!std::strcmp(coop_language_message("mes/example.mes", 3, "Health %d"), "Health %d"));
    CHECK(!std::strcmp(coop_language_message("mes/example.mes", 4, "Hello, @npcname@!"), "Hello, @npcname@!"));
    CHECK(!std::strcmp(coop_language_message("mes/example.mes", 5, "g:1000"), "g:1000"));
    CHECK(!std::strcmp(coop_language_message("rules/example.mes", 1, "Life"), "Life"));
    auto localized_name = coop_language_message("mes/example.mes", 1, "Life");
    CHECK(!std::strcmp(coop_language_original(localized_name), "Life"));
    coop_language_canonical_begin();
    CHECK(!std::strcmp(coop_language_message("mes/example.mes", 1, "Life"), "Life"));
    coop_language_canonical_end();
    CoopTextRef ref = { };
    std::strcpy(ref.path, "dlg/example.dlg");
    ref.key = 1;
    ref.variant = 1;
    CHECK(coop_language_ref_valid(&ref));
    char buffer[1000];
    CHECK(coop_language_resolve(&ref, "Alice", "Virgil", nullptr, buffer, sizeof(buffer)));
    CHECK(!std::strcmp(buffer, polish ? "Witaj, Alice!" : "Hello, Alice!"));
    ref.variant = 2;
    CHECK(coop_language_resolve(&ref, "Alice", "Virgil", nullptr, buffer, sizeof(buffer)));
    CHECK(!std::strcmp(buffer, polish ? "Witaj pani, Alice!" : "Welcome, Alice!"));
    ref.variant = 1;
    ref.key = 2;
    CHECK(coop_language_resolve(&ref, "Alice", "Virgil", nullptr, buffer, sizeof(buffer)));
    CHECK(!std::strcmp(buffer, polish ? "Odejd"
                                        "\x9f"
                                        "."
                                      : "Leave."));
    CHECK(!coop_language_resolve(&ref, "Alice", "Virgil", nullptr, buffer, 2));
    CoopTextRef price = { };
    std::strcpy(price.path, "mes/example.mes");
    price.key = 6;
    CHECK(coop_language_resolve(&price, "Alice", "Virgil", "It costs 17 coins.", buffer, sizeof(buffer)));
    CHECK(!std::strcmp(buffer, polish ? "Koszt to 17 monet." : "It costs 17 coins."));
    CHECK(!coop_language_resolve(&price, "Alice", "Virgil", "different rendered text", buffer, sizeof(buffer)));
    char source[] = "Leave.";
    coop_language_source(source, "DLG\\Example.DLG", 2, 1);
    coop_language_record("Leave.", source);
    auto found = coop_language_reference("Leave.");
    CHECK(found.key == 2 && !std::strcmp(found.path, "dlg/example.dlg"));
    coop_language_record("Leave.", "synthetic text");
    CHECK(!coop_language_reference("Leave.").path[0]);
    coop_language_remember("Leave.", &ref);
    CHECK(coop_language_reference("Leave.").key == 2);
    for (const char* path : { "../dlg/example.dlg", "dlg/../../example.dlg", "rules/example.mes", "/dlg/example.dlg", "dlg/evil\n.dlg", "dlg/UPPER.dlg" }) {
        std::strcpy(ref.path, path);
        CHECK(!coop_language_ref_valid(&ref));
    }
    CHECK(coop_language_asset("art/morph15font.art", buffer, sizeof(buffer)) == polish);
    CHECK(coop_language_asset("movies/intro.srt", buffer, sizeof(buffer)) == polish);
    CHECK(!coop_language_asset("dlg/example.dlg", buffer, sizeof(buffer)));
    CHECK(!coop_language_asset("rules/example.mes", buffer, sizeof(buffer)));
    CHECK(!coop_language_asset("../languages/pl/art/interface/morph15font.art", buffer, sizeof(buffer)));
    CHECK(!coop_language_asset("art/interface/morph15font.art", buffer, 2));
    CHECK(std::strcmp(coop_tr_rendered("Alice hits Virgil: 5 HP, 2 fatigue"), "Alice hits Virgil: 5 HP, 2 fatigue") != 0 || !polish);
    CHECK(!std::strcmp(coop_tr_rendered("unmatched player text"), "unmatched player text"));
    if (polish) {
        const char* retained = coop_tr_rendered("Alice hits Virgil: 5 HP, 2 fatigue");
        const std::string expected = retained;
        for (unsigned i = 0; i < 8200; i++) {
            const std::string event = "Alice hits Virgil: " + std::to_string(i) + " HP, 2 fatigue";
            coop_tr_rendered(event.c_str());
        }
        CHECK(!std::strcmp(retained, expected.c_str()));
    }
    CHECK(coop_language_select(polish ? 0 : 1));
    CHECK(!std::strcmp(coop_language_active(), argv[1])); // Restart, never a mid-dialogue switch.
    CHECK(coop_language_pending() == (polish ? 0u : 1u));
    CHECK(!coop_language_select(2) && !coop_language_select(100));
    fs::current_path(cwd);
    fs::remove_all(temporary);
    std::puts("PASS: personal language, codepage, presentation-only resources, dialogue references, unsafe translation fallback and restart selection");
}
