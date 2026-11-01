/* Standalone driver for the production asynchronous fingerprint service. */
#include "coop/content.h"
#include "coop/content_identity.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 3) return 2;
    std::filesystem::current_path(argv[1]);
    uint8_t id[32];
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!coop_content_read(id)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            char message[256]; coop_content_status(message, sizeof(message));
            std::fprintf(stderr, "%s\n", message);
            coop_content_shutdown(); return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    for (auto byte : id) std::printf("%02x", byte);
    std::printf("\n");
    bool compatible = true;
    if (argc == 3) {
        uint8_t previous[32];
        compatible = std::strlen(argv[2]) == 64;
        for (unsigned i = 0; compatible && i < 32; i++) {
            unsigned byte = 0;
            compatible = std::sscanf(argv[2] + i * 2, "%2x", &byte) == 1;
            previous[i] = uint8_t(byte);
        }
        compatible = compatible && coop_content_matches(id, previous);
    }
    coop_content_shutdown();
    return compatible ? 0 : 3;
}
