/* Standalone driver for the production asynchronous fingerprint service. */
#include "coop/content.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
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
    coop_content_shutdown();
    return 0;
}
