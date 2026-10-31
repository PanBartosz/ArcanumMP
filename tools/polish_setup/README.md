# ArcanumMP — prosty import spolszczenia

Gotowe programy dla Windows i Linuksa przygotowują spolszczenie Eseltera 1.7
bez Pythona i bez osobnej instalacji wypakowywarki.

1. Zamknij grę. Rozpakuj paczkę narzędzia w folderze ArcanumMP, obok
   `arcanum-mp.exe` / `arcanum-mp`. Nowe paczki gry zawierają je już w `bin/`.
2. Umieść w tym samym folderze plik **`Arcanum_UAP2.0.2PL.1.exe`** z
   [oficjalnej strony paczki Eseltera](https://www.moddb.com/mods/spolszczenie-arcanum/downloads/arcanum-uap20pl).
3. Uruchom **`ArcanumMP-Polski.exe`** na Windows albo **`ArcanumMP-Polski`** na
   Linuksie. Poczekaj na komunikat „Gotowe”, potem uruchom grę.

Program sam odnajduje folder, w którym się znajduje, sprawdza sumę pobranego
pliku, przygotowuje `languages/pl` i wybiera Polski na następne uruchomienie.
Każdy gracz może mieć inny język. Powrót do angielskiego: **B → Ustawienia →
Język → Original (English)** i ponowne uruchomienie gry.

Wcześniejsza lokalizacja pozostaje w `languages/.pl-backup-…`, a wcześniejszy
wybór języka w `arcmp-language.cfg.before-polish`. Dane rozgrywki, tożsamość
gracza i zapisy nie są zmieniane. Program czyta dane z paczki Eseltera,
korzystając z wbudowanego innoextract; nie uruchamia jej programu instalacyjnego.

Brak pliku, inna wersja, uszkodzone pobranie lub brak uprawnień do zapisu
powodują czytelny komunikat. Szczegóły rozpakowywania są w
`arcmp-polish-setup.log`. Folder gry musi być zapisywalny.

Podstawowe działanie odpowiada oryginalnej kampanii używanej przez ArcanumMP.
Zaawansowany import do wspólnej instalacji UAP: `--variant uap`. Dostępne są też
`--game FOLDER`, `--archive PLIK`, `--help` i `--non-interactive` (bez okien).

## Build and provenance

The tool has its own version, 1.0.0, and does not change the game protocol.
Its C++17 source is in this directory and `../sha256.hpp`. Build independently:

```sh
cmake -S tools/polish_setup -B build/polish-setup -G Ninja -DCMAKE_BUILD_TYPE=Release -DARCMP_POLISH_EXTRACTOR=/path/to/innoextract
cmake --build build/polish-setup --parallel
ctest --test-dir build/polish-setup --output-on-failure
```

For Windows use the project's llvm-mingw toolchain and embed the Windows
innoextract binary. Linux release builds use `ARCMP_POLISH_DESKTOP_UI=ON` with
a static SDL3 package to display completion/errors on desktop launch. The tool
has no Python, SDL DLL or separately installed extractor requirement at runtime.
The prebuilt Linux helper has the same glibc requirement as the game.

innoextract 1.9 is embedded unchanged and runs as a child process in a private
temporary directory. The Windows frontend creates component-suffix directories
from its listing first, working around innoextract's backslash collisions, and
uses the system temporary folder to avoid legacy path-length limits. Prepared
language data stays on the game's volume for atomic publication with rollback.

Only the verified 192,014,985-byte Eselter package is accepted:
SHA-256 `294a1c05a88c8f910a7d23327a4efebe0605f3026294c0a77c53185828e850f2`.
Dialogue conditions/actions, scripts, rules, maps and prototypes are discarded.
The imported presentation resources match the previous Python import.

Retain the supplied licenses, notices and source archive when redistributing
the utility. ArcanumMP code uses Sustainable Use License 1.0. innoextract is by
Daniel Scharrer under the zlib license; the bundles include its original binary
package, source and dependency notices. Third-party Polish game data remains
separate and is not included in the utility downloads.
