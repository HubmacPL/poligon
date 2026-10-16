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

## Środowisko

`tools\dev_env.ps1` znajduje Visual Studio 2022 (lub Build Tools) przez `vswhere` i ładuje środowisko x64 do bieżącego procesu. Stąd biorą się `cl`, `cmake` i `ninja`; nic nie jest dopisywane do systemowego PATH. Do ręcznej pracy w terminalu: `. tools\dev_env.ps1`, potem zwykłe `cmake --preset dev` itd.

## Zależności (vcpkg)

- Używamy kopii vcpkg dołączonej do Visual Studio; `dev_env.ps1` ustawia na nią `VCPKG_ROOT` na czas procesu.
- Lista zależności jest w `vcpkg.json`. Pole `builtin-baseline` przypina katalog pakietów do jednego commita, więc wersje są powtarzalne.
- Zainstalowane biblioteki trafiają do `vcpkg_installed/` w projekcie (wspólne dla wszystkich presetów). Pobrane źródła i zbudowane paczki vcpkg trzyma w swojej pamięci podręcznej w profilu użytkownika.
- Przed dodaniem zależności: poszukać gotowej biblioteki (zasada „najpierw biblioteka"), sprawdzić licencję w źródle i uzyskać zgodę autora.
- `imgui` z cechami `sdl3-binding` i `sdlgpu3-binding` (MIT; nie ciągnie nic poza SDL3): okna narzędzi.
- `ozz-animation` 0.17.0 (MIT; zależy tylko od biblioteki standardowej C++): animacja szkieletowa. Nie ma go w katalogu vcpkg, więc repozytorium niesie własny mały opis paczki w `ports/ozz-animation` (zarejestrowany w `vcpkg-configuration.json`), przypięty do wydania wersją i sumą kontrolną. Budowane są same biblioteki, jako DLL (jako statyczne nie dają się połączyć z budową AddressSanitizer): bez narzędzi, importerów i przykładów.
- `joltphysics` 5.6.0 (MIT; zależy tylko od biblioteki standardowej C++): fizyka.
- `assimp` (BSD-3; ciągnie kubazip, pugixml i rapidjson na MIT, poly2tri na BSD-3, minizip i zlib na zlib, polyclipping i utfcpp na Boost; vcpkg wymienia też ISC dla dołączonej części).
- `glm` (do wyboru MIT albo „Happy Bunny"; korzystamy na MIT).
- `flecs` (MIT; w środku fragmenty na BSD-2, MIT i jeden w domenie publicznej).
- Obecne poza tym: `sdl3` z cechą `vulkan` (zlib, z częściami na MIT, Apache 2.0 i BSD-3), `stb` (MIT lub domena publiczna), `directx-dxc` jako narzędzie budowania (NCSA; nic z niego nie jest linkowane ani dołączane do gry).

## Układ silnika

Silnik to zestaw bibliotek statycznych, po jednej na obszar. Każdy obszar ma nagłówki publiczne w `engine/include/poligon/<obszar>/` (dołączane jako `poligon/<obszar>/<nazwa>.h`), źródła w `engine/src/<obszar>/`, z osobnym folderem na każdy moduł tam, gdzie obszar ma ich kilka, i własny `CMakeLists.txt`.

| Biblioteka | Obszar | Moduły (foldery w `engine/src/<obszar>/`) | Może używać |
|---|---|---|---|
| `poligon_core` | `core` | `app` (pętla gry, zegar, wiersz poleceń, wersja), `platform` (zdarzenia, błędy, raporty awarii), `input`, `image` (obrazy, kompresja blokowa) | niczego naszego |
| `poligon_ecs` | `ecs` | ustawienia flecs, zrzut i hash stanu | core |
| `poligon_render` | `render` | `renderer`, `settings` (presety, plik ustawień obrazu), `impostor`, `shaders/<grupa>` | core |
| `poligon_animation` | `animation` | szkielety, klipy, odtwarzacze | core |
| `poligon_physics` | `physics` | ciała, postacie, promienie | core |
| `poligon_assets` | `assets` | pliki modeli, lista assetów, wypiekanie, wczytywanie po nazwie | core, render, animation |
| `poligon_world` | `world` | `terrain` (z generatorem i przesłanianiem nieba), `vegetation` (z regułami sadzenia, roślinami z kodu, wydeptywaniem), `water`, `level` | core, render, animation, assets, physics |
| `poligon_debug` | `debug` | okna debug, swobodna kamera | core, render, ecs |

- `poligon_engine` łączy je wszystkie; gry, narzędzia, testy i przykłady linkują właśnie ją.
- Kolumnę „może używać” sprawdza przy każdym `tools/check.ps1` skrypt `tools/check_layers.ps1`, który czyta każde dołączenie (wszystkie nagłówki leżą w jednym folderze, więc złe dołączenie i tak by się skompilowało). Renderer na przykład nic nie wie o świecie: wydeptana ziemia i przesłanianie nieba docierają do niego jako `GroundMap` przez interfejs `GroundMapSource`.
- Shadery są pogrupowane według tego, co rysują: `mesh`, `terrain`, `vegetation` (impostory), `sky` (niebo i chmury), `water`, `post` (przebiegi pełnoekranowe); `shaders/common` trzyma pliki dołączane przez kilka shaderów.

## Dodawanie rzeczy

- Kod silnika: plik w folderze obszaru (`engine/src/<obszar>/<moduł>/`), wpisany do `CMakeLists.txt` tego obszaru; nagłówek publiczny w `engine/include/poligon/<obszar>/`. Nowy obszar wymaga wpisu w `engine/CMakeLists.txt` i w `tools/check_layers.ps1`.

- Test: plik `tests/<nazwa>.cpp`, w nim `test::Expect(warunek, "opis")` z `tests/test_support.h` i `return test::ExitCode();` na końcu `main`, oraz wpis `poligon_add_test(<nazwa>)` w `tests/CMakeLists.txt`.
- Przykład: `examples/<nazwa>/main.cpp` i wpis `poligon_add_example(<nazwa>)` w `examples/CMakeLists.txt`. Każdy przykład jest też testem; argumenty po nazwie są przekazywane przy uruchomieniu.
