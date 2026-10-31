#include "../sha256.hpp"
#include "import.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef ARCMP_POLISH_SDL
#include <SDL3/SDL.h>
#endif
#endif
#include "extractor_data.h"
namespace {
namespace fs = std::filesystem;
struct Options {
    fs::path game, archive;
    bool uap = false, quiet = false;
};
std::string lower(std::string s)
{
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
#ifdef _WIN32
std::string utf8(const std::wstring& value)
{
    if (value.empty()) return { };
    int n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), n, nullptr, nullptr);
    return result;
}
std::wstring wide(const std::string& value) { return fs::u8path(value).wstring(); }
#endif
fs::path executable()
{
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!n || n >= path.size()) throw std::runtime_error("Nie można odnaleźć programu.");
    path.resize(n);
    return path;
#else
    std::vector<char> path(32768);
    auto n = readlink("/proc/self/exe", path.data(), path.size());
    if (n <= 0 || static_cast<size_t>(n) == path.size()) throw std::runtime_error("Nie można odnaleźć programu.");
    return fs::path(std::string(path.data(), n));
#endif
}
fs::path find(const fs::path& dir, const std::string& name)
{
    for (const auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file() && lower(e.path().filename().u8string()) == lower(name)) return e.path();
    return { };
}
void notify(const std::string& text, bool error, bool quiet)
{
    (error ? std::cerr : std::cout) << text << std::endl;
    if (quiet) return;
#ifdef _WIN32
    MessageBoxW(nullptr, wide(text).c_str(), L"ArcanumMP — język polski", MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
#elif defined(ARCMP_POLISH_SDL)
    if (!isatty(STDOUT_FILENO)) SDL_ShowSimpleMessageBox(error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION, "ArcanumMP — język polski", text.c_str(), nullptr);
#endif
}
struct Work {
    fs::path path;
    explicit Work(const fs::path& root)
    {
        for (unsigned i = 0; i < 20; i++) {
            auto p = root / (".arcmp-polish-work-" + polish::unique_suffix());
            if (fs::create_directory(p)) {
                path = p;
                fs::permissions(path, fs::perms::owner_all);
                return;
            }
        }
        throw std::runtime_error("Nie można utworzyć katalogu roboczego.");
    }
    ~Work()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};
struct Lock {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    explicit Lock(const fs::path& game)
    {
        handle = CreateFileW((game / "arcmp-polish-setup.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Nie można rozpocząć importu. Zamknij drugie okno narzędzia i sprawdź uprawnienia do folderu gry.");
    }
    ~Lock()
    {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
#else
    int fd = -1;
    explicit Lock(const fs::path& game)
    {
        fd = open((game / "arcmp-polish-setup.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (fd < 0) throw std::runtime_error("Brak uprawnień do zapisu w folderze gry.");
        if (flock(fd, LOCK_EX | LOCK_NB)) {
            close(fd);
            fd = -1;
            throw std::runtime_error("Import już trwa. Zamknij drugie okno narzędzia.");
        }
    }
    ~Lock()
    {
        if (fd >= 0) close(fd);
    }
#endif
};
#ifdef _WIN32
std::wstring argument(const std::wstring& value)
{
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') {
            slashes++;
            continue;
        }
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}
#endif
bool extract(const fs::path& program, const fs::path& archive, const fs::path& output, const fs::path& log, bool list = false)
{
    std::vector<fs::path> args = { program, list ? "--list" : "--extract", "--lowercase", "--collisions=rename", list ? "--quiet" : "--silent", "--color=0", "--progress=0", "--output-dir", output, archive };
#ifdef _WIN32
    std::wstring command;
    for (const auto& arg : args) {
        if (!command.empty()) command += L' ';
        command += argument(arg.wstring());
    }
    SECURITY_ATTRIBUTES attributes = { sizeof(attributes), nullptr, TRUE };
    HANDLE file = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
        return false;
    }
    STARTUPINFOW start = { };
    start.cb = sizeof(start);
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdOutput = start.hStdError = file;
    start.hStdInput = input;
    PROCESS_INFORMATION process = { };
    bool good = CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &start, &process) != 0;
    CloseHandle(file);
    CloseHandle(input);
    if (!good) return false;
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return code == 0;
#else
    std::vector<std::string> values;
    for (const auto& arg : args)
        values.push_back(arg.string());
    std::vector<char*> argv;
    for (auto& arg : values)
        argv.push_back(arg.data());
    argv.push_back(nullptr);
    pid_t pid = fork();
    if (pid < 0) return false;
    if (!pid) {
        int fd = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) _exit(126);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
        execv(program.c_str(), argv.data());
        _exit(127);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}
std::string setup(Options options)
{
    auto base = executable().parent_path();
    auto game = fs::absolute(options.game.empty() ? base : options.game);
    if (!fs::is_directory(game) || (find(game, "arcanum-mp.exe").empty() && find(game, "arcanum-mp").empty() && find(game, "arcanum1.dat").empty()))
        throw std::runtime_error("Przenieś ArcanumMP-Polski do folderu gry, obok arcanum-mp.exe / arcanum-mp.\nW tym samym folderze umieść plik " + std::string(polish::archive_name) + ".");
    auto archive = options.archive.empty() ? find(game, polish::archive_name) : fs::absolute(options.archive);
    if (archive.empty() && game != base) archive = find(base, polish::archive_name);
    if (archive.empty() || !fs::is_regular_file(archive)) throw std::runtime_error("Brak pliku spolszczenia Eseltera 1.7.\nUmieść " + std::string(polish::archive_name) + " obok tego programu w folderze gry i uruchom go ponownie.\n\nPobierz plik ze strony autora:\nhttps://www.moddb.com/mods/spolszczenie-arcanum/downloads/arcanum-uap20pl");
    Lock lock(game);
    std::ofstream log(game / "arcmp-polish-setup.log", std::ios::binary);
    if (!log) throw std::runtime_error("Nie można zapisać plików w folderze gry.");
    auto status = [&](const std::string& text) {std::cout<<text<<std::endl;log<<text<<'\n';log.flush(); };
    status("ArcanumMP Polski " + std::string(polish::version) + " — Eselter 1.7\nSprawdzam plik spolszczenia...");
    if (fs::file_size(archive) != polish::archive_size || Sha256::file(archive) != polish::archive_sha) throw std::runtime_error("Plik spolszczenia jest niepełny albo pochodzi z innej wersji.\nPobierz Eseltera 1.7 i spróbuj ponownie.\nWymagany plik: " + std::string(polish::archive_name));
    if (!extractor_size) throw std::runtime_error("Ta kompilacja nie zawiera narzędzia rozpakowującego. Pobierz gotowy ArcanumMP-Polski z GitHub Releases.");
    Work work(game);
#ifdef _WIN32
    // Keep the legacy extractor away from MAX_PATH when the game is in a
    // deeply nested folder. The prepared pack remains on the game's volume
    // so publication can still be an atomic directory rename.
    Work extraction(fs::temp_directory_path());
    auto unpack = extraction.path;
#else
    auto unpack = work.path;
#endif
#ifdef _WIN32
    auto program = unpack / "innoextract.exe";
#else
    auto program = unpack / "innoextract";
#endif
    {
        std::ofstream file(program, std::ios::binary);
        file.write(reinterpret_cast<const char*>(extractor_data), extractor_size);
        file.close();
        if (!file) throw std::runtime_error("Nie można zapisać narzędzia rozpakowującego.");
    }
    fs::permissions(program, fs::perms::owner_all);
#ifdef _WIN32
    // innoextract 1.9 appends component names such as #main\UAP after
    // creating normal directories. On Windows this introduces another path
    // level; create these parents from its listing before extraction.
    if (!extract(program, archive, unpack / "extracted", unpack / "list.log", true))
        throw std::runtime_error("Nie można odczytać zawartości spolszczenia.");
    {
        std::ifstream listing(unpack / "list.log", std::ios::binary);
        std::string line;
        while (std::getline(listing, line)) {
            if (line.rfind(" - \"", 0) != 0 || line.find('#') == std::string::npos) continue;
            auto first = line.find('"'), last = line.rfind('"');
            if (first == std::string::npos || last <= first) continue;
            auto relative = fs::u8path(line.substr(first + 1, last - first - 1));
            if (relative.is_absolute() || relative.has_root_name())
                throw std::runtime_error("Nieprawidłowa ścieżka w spolszczeniu.");
            for (const auto& part : relative)
                if (part == "..") throw std::runtime_error("Nieprawidłowa ścieżka w spolszczeniu.");
            try {
                fs::create_directories((unpack / "extracted" / relative).parent_path());
            } catch (...) {
                log << "Nie można przygotować ścieżki: " << line << std::endl;
                throw;
            }
        }
    }
#endif
    status("Rozpakowuję dane. To może potrwać chwilę...");
    bool good = extract(program, archive, unpack / "extracted", unpack / "extract.log");
    {
        std::ifstream text(unpack / "extract.log", std::ios::binary);
        if (text) log << text.rdbuf();
    }
    if (!good) throw std::runtime_error("Nie udało się rozpakować danych. Szczegóły: arcmp-polish-setup.log.\nZamknij grę i spróbuj ponownie.");
    status("Przygotowuję polskie teksty, czcionki i grafiki...");
    auto files = polish::import_pack(unpack / "extracted/app", work.path / "pl", options.uap);
    if (!options.uap && files.size() != 903) throw std::runtime_error("Rozpakowano niekompletny zestaw danych. Nie zmieniono dotychczasowej lokalizacji.");
    auto backup = polish::publish_pack(game, work.path / "pl");
    bool selected = polish::select_polish(game);
    std::string result = "Gotowe! Wgrano spolszczenie Eseltera 1.7.\n";
    result += selected ? "Przy następnym uruchomieniu ArcanumMP będzie po polsku." : "W grze wybierz: B → Settings → Language → Polski, a następnie uruchom ją ponownie.";
    result += "\n\nJeśli gra jest otwarta, zamknij ją i uruchom ponownie.\nKażdy gracz może używać swojego języka.";
    if (!backup.empty()) result += "\n\nPoprzednią lokalizację zachowano w:\n" + backup.u8string();
    status("Zainstalowane pliki: " + std::to_string(files.size()) + "\n" + result);
    return result;
}
int run(const std::vector<std::string>& args)
{
    Options options;
    for (const auto& arg : args)
        if (arg == "--non-interactive") options.quiet = true;
    try {
        for (size_t i = 1; i < args.size(); i++) {
            if (args[i] == "--non-interactive") continue;
            if (args[i] == "--help") {
                std::cout << "ArcanumMP-Polski " << polish::version << "\nUmieść program i " << polish::archive_name << " w folderze gry, a następnie uruchom.\nOpcje: --game FOLDER --archive PLIK --variant stock|uap --non-interactive\n";
                return 0;
            }
            if ((args[i] == "--game" || args[i] == "--archive" || args[i] == "--variant") && i + 1 < args.size()) {
                auto option = args[i++];
                if (option == "--game")
                    options.game = fs::u8path(args[i]);
                else if (option == "--archive")
                    options.archive = fs::u8path(args[i]);
                else {
                    if (args[i] != "stock" && args[i] != "uap") throw std::runtime_error("Nieznany wariant danych: " + args[i]);
                    options.uap = args[i] == "uap";
                }
            } else
                throw std::runtime_error("Nieznana lub niepełna opcja: " + args[i]);
        }
        notify(setup(options), false, options.quiet);
        return 0;
    } catch (const std::exception& e) {
        notify(std::string("Nie udało się wgrać spolszczenia.\n\n") + e.what(), true, options.quiet);
        return 1;
    }
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleW(L"ArcanumMP — język polski");
    std::vector<std::string> args;
    for (int i = 0; i < argc; i++)
        args.push_back(utf8(argv[i]));
    return run(args);
}
#else
int main(int argc, char** argv)
{
    std::vector<std::string> args;
    for (int i = 0; i < argc; i++)
        args.emplace_back(argv[i]);
    return run(args);
}
#endif
