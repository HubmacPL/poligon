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

Wyniki trafiają do `build/<preset>/`; wszystkie programy i DLL do `build/<preset>/bin/`, pliki wytworzone przez testy (zrzuty klatek) do `build/<preset>/test_output/`.

## Zasady wspólne

- Każdy nasz cel (silnik, gra, przykłady, testy) linkuje `poligon_build_options`: `/W4 /WX /permissive- /utf-8`. Ostrzeżenie zatrzymuje budowę. Zależności zewnętrzne tego celu nie linkują.
- AddressSanitizer jest włączany globalnie dla wszystkiego, co buduje nasz CMake. Biblioteki z vcpkg są budowane osobno i nie są nim instrumentowane.
- Test `asan_canary_detects_overflow` celowo pisze poza bufor i przechodzi tylko wtedy, gdy sanitizer to zgłosi. Jeśli zacznie padać, budowa `asan` przestała chronić.

## Środowisko

`tools\dev_env.ps1` znajduje Visual Studio 2022 (lub Build Tools) przez `vswhere` i ładuje środowisko x64 do bieżącego procesu. Stąd biorą się `cl`, `cmake` i `ninja`; nic nie jest dopisywane do systemowego PATH. Do ręcznej pracy w terminalu: `. tools\dev_env.ps1`, potem zwykłe `cmake --preset dev` itd.

## Zależności (vcpkg)

- Używamy kopii vcpkg dołączonej do Visual Studio; `dev_env.ps1` ustawia na nią `VCPKG_ROOT` na czas procesu.
- Lista zależności jest w `vcpkg.json`. Pole `builtin-baseline` przypina katalog pakietów do jednego commita, więc wersje są powtarzalne.
- Zainstalowane biblioteki trafiają do `vcpkg_installed/` w projekcie (wspólne dla wszystkich presetów). Pobrane źródła i zbudowane paczki vcpkg trzyma w swojej pamięci podręcznej w profilu użytkownika.
- Przed dodaniem zależności: sprawdzić licencję w źródle i uzyskać zgodę autora.
- `glm` (do wyboru MIT albo „Happy Bunny"; korzystamy na MIT).
- `flecs` (MIT; w środku fragmenty na BSD-2, MIT i jeden w domenie publicznej).
- Obecne poza tym: `sdl3` z cechą `vulkan` (zlib, z częściami na MIT, Apache 2.0 i BSD-3), `stb` (MIT lub domena publiczna), `directx-dxc` jako narzędzie budowania (NCSA; nic z niego nie jest linkowane ani dołączane do gry).

## Dodawanie rzeczy

- Test: plik `tests/<nazwa>.cpp`, w nim `test::Expect(warunek, "opis")` z `tests/test_support.h` i `return test::ExitCode();` na końcu `main`, oraz wpis `poligon_add_test(<nazwa>)` w `tests/CMakeLists.txt`.
- Przykład: `examples/<nazwa>/main.cpp` i wpis `poligon_add_example(<nazwa>)` w `examples/CMakeLists.txt`. Każdy przykład jest też testem; argumenty po nazwie są przekazywane przy uruchomieniu.
