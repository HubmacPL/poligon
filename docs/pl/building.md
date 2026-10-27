# Budowanie i sprawdzanie

## Jedno polecenie

```
powershell -ExecutionPolicy Bypass -File tools\check.ps1
```

Konfiguruje, buduje i testuje wszystkie konfiguracje. Kod wyjścia 0 i ostatnia linia `CHECK PASSED` oznaczają sukces. Przy pierwszym błędzie skrypt kończy się kodem 1 i linią `FAILED: <krok>`.

Jedna konfiguracja: `tools\check.ps1 -Presets dev`.

## Konfiguracje (CMakePresets.json)

| Preset | Co to jest | Testy |
|---|---|---|
| `dev` | Debug, do codziennej pracy | tak |
| `asan` | RelWithDebInfo z AddressSanitizer | tak, plus kanarek |
| `analyze` | Debug z analizą statyczną MSVC (`/analyze`) | nie, sama kompilacja |
| `release` | RelWithDebInfo, bez dodatków | nie; poza `check.ps1` |

`release` służy do pomiaru szybkości i do oglądania ciężkich scen: `cmake --preset release`, potem `cmake --build --preset release`. Budowa Debug rysuje scenę leśną około trzy razy wolniej i ładuje ją cztery razy wolniej.

Wyniki trafiają do `build/<preset>/`; wszystkie programy i DLL do `build/<preset>/bin/`, pliki wytworzone przez testy (zrzuty klatek) do `build/<preset>/test_output/`.

## Zasady wspólne

- Każdy nasz cel (silnik, gra, przykłady, testy) linkuje `poligon_build_options`: `/W4 /WX /permissive- /utf-8`. Ostrzeżenie zatrzymuje budowę. Zależności zewnętrzne tego celu nie linkują.
- AddressSanitizer jest włączany globalnie dla wszystkiego, co buduje nasz CMake. Biblioteki z vcpkg są budowane osobno i nie są nim instrumentowane. Biblioteka uruchomieniowa sanitizera jest kopiowana obok programów.
- Test `asan_canary_detects_overflow` celowo pisze poza bufor i przechodzi tylko wtedy, gdy sanitizer to zgłosi. Jeśli zacznie padać, budowa `asan` przestała chronić.

## Opcje budowy

| Opcja | Buduje |
|---|---|
| `POLIGON_BUILD_TESTS` | `tests/` (i rejestruje je w CTest) |
| `POLIGON_BUILD_EXAMPLES` | `examples/` (każdy jest też testem CTest) |
| `POLIGON_BUILD_TOOLS` | `model_viewer`, `forest_scene`, `asset_cooker`, `terrain_tool`, `impostor_baker`, `image_tool` |
| `POLIGON_BUILD_TOY` | grę-zabawkę i jej sprawdzenia |

Wszystkie cztery są włączone, gdy Poligon jest budowanym projektem (`PROJECT_IS_TOP_LEVEL`), i wyłączone, gdy gra dodaje repozytorium przez `add_subdirectory`: wtedy budują się tylko biblioteki silnika i nic z Poligonu nie trafia do CTest gry. `image_compare` i `state_compare` budują się zawsze, gdy budują się testy, narzędzia albo zabawka. Silnik znajduje własny folder `cmake/` z `engine/CMakeLists.txt` (`POLIGON_CMAKE_DIR`), więc nie zależy od tego, który projekt jest główny.

## Środowisko

`tools\dev_env.ps1` znajduje Visual Studio 2022 (lub Build Tools) przez `vswhere` i ładuje środowisko x64 do bieżącego procesu. Stąd biorą się `cl`, `cmake` i `ninja`; nic nie jest dopisywane do systemowego PATH. Do ręcznej pracy w terminalu: `. tools\dev_env.ps1`, potem zwykłe `cmake --preset dev` itd.

## Zależności (vcpkg)

- Używamy kopii vcpkg dołączonej do Visual Studio; `dev_env.ps1` ustawia na nią `VCPKG_ROOT` na czas procesu.
- Lista zależności jest w `vcpkg.json`. Pole `builtin-baseline` przypina katalog pakietów do jednego commita, więc wersje są powtarzalne.
- Zainstalowane biblioteki trafiają do `vcpkg_installed/` w projekcie (wspólne dla wszystkich presetów). Pobrane źródła i zbudowane paczki vcpkg trzyma w swojej pamięci podręcznej w profilu użytkownika.
- Przed dodaniem zależności: poszukać gotowej biblioteki (zasada „najpierw biblioteka"), sprawdzić licencję w źródle i uzyskać zgodę autora.
- `imgui` z cechami `sdl3-binding` i `sdlgpu3-binding` (MIT; nie ciągnie nic poza SDL3): okna narzędzi.
- `ozz-animation` 0.17.0 (MIT; zależy tylko od biblioteki standardowej C++): animacja szkieletowa. Nie ma go w katalogu vcpkg, więc repozytorium niesie własny mały opis paczki w `ports/ozz-animation` (zarejestrowany w `vcpkg-configuration.json`), przypięty do wydania wersją i sumą kontrolną. Budowane są same biblioteki, jako DLL (jako statyczne nie dają się połączyć z budową AddressSanitizer): bez narzędzi, importerów i przykładów.
- `joltphysics` 5.6.0 (MIT; zależy tylko od biblioteki standardowej C++): fizyka. Budowany z naszego portu nakładkowego w `ports/joltphysics`, tak jak ozz z `ports/ozz-animation`: kopia portu z katalogu vcpkg tego samego wydania z `DOUBLE_PRECISION=ON` (pozycje w świecie w double, patrz physics.md); kod Jolta bez zmian.
- `assimp` (BSD-3; ciągnie kubazip, pugixml i rapidjson na MIT, poly2tri na BSD-3, minizip i zlib na zlib, polyclipping i utfcpp na Boost; vcpkg wymienia też ISC dla dołączonej części).
- `glm` (do wyboru MIT albo „Happy Bunny"; korzystamy na MIT).
- `flecs` (MIT; w środku fragmenty na BSD-2, MIT i jeden w domenie publicznej).
- Obecne poza tym: `sdl3` z cechą `vulkan` (zlib, z częściami na MIT, Apache 2.0 i BSD-3), `stb` (MIT lub domena publiczna), `directx-dxc` jako narzędzie budowania (NCSA; nic z niego nie jest linkowane ani dołączane do gry).

## Paczki wydania

`tools\package.ps1 [-Version 0.1.0]` buduje konfigurację `release` i pakuje
`build\release_package\poligon-WERSJA-windows-x64.zip`: `forest_scene`, `toy`, `model_viewer`,
`asset_cooker`, wszystkie przykłady, DLL z `build\release\bin`, `assets.txt` i `forest.level`
sceny `forest_scene`, `LICENSE`, `THIRD_PARTY_LICENSES.md`, teksty licencji bibliotek
(`licenses\`, z plików `copyright` vcpkg i z nagłówków shaderów z cudzym kodem) i `README.txt`.
Bez testów i bez plików `.pdb` (raporty awarii z wydanej kopii pokazują wtedy adresy bez nazw
funkcji).

- `dxcompiler.dll` nie jest potrzebny: shadery są kompilowane przy budowaniu i wbudowane
  (`dumpbin /dependents` po wszystkich programach i DLL wersji 0.1.0 nie wymienia DXC).
- Środowisko uruchomieniowe C++ jest linkowane dynamicznie (`/MD`, tak jak triplet `x64-windows`
  vcpkg buduje każdą bibliotekę): programy potrzebują `msvcp140.dll`, `vcruntime140.dll` i
  `vcruntime140_1.dll` z Microsoft Visual C++ Redistributable, który `README.txt` wymienia z
  odnośnikiem do pobrania. Linkowanie statyczne wymagałoby przebudowania każdej biblioteki
  statycznym tripletem i zmiany naszych portów DLL (Jolt, ozz-animation), a każdy DLL niósłby
  własną kopię środowiska; pakiet redystrybucyjny jest już na większości komputerów z Windows.

`tools\package_assets.ps1 [-Version 0.1.0] [-Exclude NAZWY] [-Zip]` robi archiwum zasobów
(zob. [assets.md](assets.md)).

## Układ silnika

Silnik to zestaw bibliotek statycznych, po jednej na obszar. Każdy obszar ma nagłówki publiczne w `engine/include/poligon/<obszar>/` (dołączane jako `poligon/<obszar>/<nazwa>.h`), źródła w `engine/src/<obszar>/`, z osobnym folderem na każdy moduł tam, gdzie obszar ma ich kilka, i własny `CMakeLists.txt`.

| Biblioteka | Obszar | Moduły (foldery w `engine/src/<obszar>/`) | Może używać |
|---|---|---|---|
| `poligon_core` | `core` | `app` (pętla gry, zegar, wiersz poleceń, wersja), `platform` (zdarzenia, błędy, raporty awarii), `input`, `image` (obrazy, kompresja blokowa) | niczego naszego |
| `poligon_ecs` | `ecs` | ustawienia flecs, zrzut i hash stanu | core |
| `poligon_render` | `render` | `renderer` (publiczne API i klatka, cele renderowania, potoki, rysowanie; stan w `renderer_internal.h`), `resources` (siatki, tekstury, teren, impostory, przesyłanie, szum chmur; dla każdej funkcji prywatny nagłówek stanu i jego plik: `overlay_state.h` z `overlay.cpp` i `fonts.cpp`, `decal_state.h` z `decals.cpp`, `particle_state.h` z `particles.cpp`; `dynamic_buffer.cpp` dla buforów napełnianych w każdej klatce), `passes` (po pliku na przebieg klatki, w tym `decal_pass.cpp`, `particle_pass.cpp`, `overlay_pass.cpp`), `settings` (presety, plik ustawień obrazu), `shaders/<grupa>` | core |
| `poligon_animation` | `animation` | `runtime`: szkielety, odtwarzacze, archiwa | core |
| `poligon_animation_build` | `animation` | `build`: budowanie zestawów z klipów (strona narzędzi) | core, animation |
| `poligon_physics` | `physics` | ciała, postacie, promienie | core |
| `poligon_assets` | `assets` | `runtime`: wczytywanie wypieczonych assetów po nazwie, modele na karcie | core, render, animation |
| `poligon_assets_cook` | `assets` | `cook`: pliki modeli (Assimp), lista assetów, wypiekanie, pakowanie (strona narzędzi) | core, render, animation, assets |
| `poligon_world` | `world` | `terrain` (z przesłanianiem nieba, a generator w `terrain/generator`: po pliku na etap, duża skala, mała skala i mapy), `vegetation` (z regułami sadzenia, roślinami z kodu, wydeptywaniem), `water`, `level`, `particles` (emiter), `footprints` | core, render, animation, assets, physics |
| `poligon_debug` | `debug` | okna debug, swobodna kamera | core, render, ecs |

- Strona narzędzi (`poligon_animation_build`, `poligon_assets_cook`, nagłówki `animation_build.h`, `model_import.h`, `cooking.h`) to to, bez czego wydana gra może się obejść: wczytuje tylko wypieczone assety. `poligon_runtime` łączy biblioteki strony gry i używa jej gra toy, więc każdy check dowodzi, że linkują się bez Assimp i bez części ozz do budowania. `poligon_engine` łączy wszystko, dla narzędzi, testów i przykładów.
- `tools/check_layers.ps1` zgłasza też błąd, gdy biblioteka strony gry dołącza nagłówek strony narzędzi.
- Kolumnę „może używać” sprawdza przy każdym `tools/check.ps1` skrypt `tools/check_layers.ps1`, który czyta każde dołączenie (wszystkie nagłówki leżą w jednym folderze, więc złe dołączenie i tak by się skompilowało). Renderer na przykład nic nie wie o świecie: wydeptana ziemia i przesłanianie nieba docierają do niego jako `GroundMap` przez interfejs `GroundMapSource`.
- Shadery są pogrupowane według tego, co rysują: `mesh`, `terrain`, `vegetation` (impostory), `sky` (niebo i chmury), `water`, `post` (przebiegi pełnoekranowe); `shaders/common` trzyma pliki dołączane przez kilka shaderów.

## Dodawanie rzeczy

- Kod silnika: plik w folderze obszaru (`engine/src/<obszar>/<moduł>/`), wpisany do `CMakeLists.txt` tego obszaru; nagłówek publiczny w `engine/include/poligon/<obszar>/`. Nowy obszar wymaga wpisu w `engine/CMakeLists.txt` i w `tools/check_layers.ps1`.

- Test: plik `tests/<obszar>/<nazwa>.cpp` (obszar silnika, który sprawdza), w nim `test::Expect(warunek, "opis")` z `tests/test_support.h` i `return test::ExitCode();` na końcu `main`, oraz wpis w `tests/<obszar>/CMakeLists.txt`: `poligon_add_test(<nazwa>)` albo `poligon_add_render_test(<nazwa>)` dla testu, który renderuje (uruchamia się wtedy raz na backend z warstwą diagnostyczną i dostaje backend jako pierwszy argument).
- Przykład: `examples/<obszar>/<nazwa>/main.cpp` i wpis `poligon_add_example(<nazwa>)` w `examples/<obszar>/CMakeLists.txt`. Każdy przykład jest też testem; argumenty po nazwie są przekazywane przy uruchomieniu.
