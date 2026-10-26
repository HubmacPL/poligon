# Poligon

![forest_scene w zestawie high: zbocze nad jeziorem, sosny, brzozy i dęby, lis](docs/images/forest_scene_high.jpg)

*`forest_scene` w zestawie `high` w 1920 x 1080 (tu pomniejszony). Lis: przykład glTF Khronos „Fox" (model CC0, animacje CC-BY 4.0); pozostałe modele i tekstury z Poly Haven (CC0) i zrobione kodem.*

Poligon to silnik gier w C++ dla Windows, pisany pod gry z otwartym światem: duży teren, lasy,
woda, animowane zwierzęta, gracz chodzący w pierwszej osobie. Jest złożony ze sprawdzonych
bibliotek (SDL3 GPU do okna i rysowania, Jolt Physics, flecs, ozz-animation, Assimp, Dear
ImGui), a na nich stoi nasz własny renderer, generator terenu, roślinność i strumieniowanie
świata. Grę pisze się w kodzie na silniku; edytora jeszcze nie ma. Nazwa to polskie słowo
„poligon", miejsce prób.

Jest budowany do pracy z agentem AI piszącym kod, a większość jego własnego kodu to warstwa
weryfikacji, dzięki której agent sam sprawdza, czy zmiana działa: przebiegi bez okna ze
stałym krokiem czasu, zrzuty klatek do PNG porównywane z tolerancją, zrzuty stanu gry do JSON,
nagrywanie wejścia z deterministycznym odtwarzaniem (dwa odtworzenia dają identyczny stan),
raporty awarii ze stosem wywołań i jeden mały uruchamialny przykład na każdą funkcję silnika.
Każdą zmianę sprawdza jeden skrypt, który buduje i testuje trzy konfiguracje (Debug,
AddressSanitizer, analiza statyczna); testy rysowania idą na Direct3D 12 i na Vulkanie.

## Wymagania

- Windows 10 lub 11, x64.
- Karta graficzna z Direct3D 12 albo Vulkanem 1.3. Celem zestawu `medium` jest GeForce GTX
  980 (4 GB) w 1920 x 1080 i 60 klatkach na sekundę. Bez śledzenia promieni i mesh shaderów.
- Visual Studio 2022 albo Build Tools for Visual Studio 2022 z pakietem „Programowanie
  aplikacji klasycznych w języku C++", w tym ze składnikami „Narzędzia kompilacji MSVC
  x64/x86", „Narzędzia CMake języka C++ dla systemu Windows" (CMake i Ninja) i „Menedżer
  pakietów vcpkg".
- Git (potrzebny vcpkg) i internet przy pierwszym budowaniu (vcpkg pobiera biblioteki).
- Około 8 GB wolnego miejsca na foldery budowania, do tego pamięć podręczna vcpkg w
  profilu użytkownika.

## Budowanie od czystego klonu

1. Sklonuj repozytorium i otwórz PowerShell w jego folderze:
   ```
   git clone <adres repozytorium> poligon
   cd poligon
   ```
2. Zbuduj i przetestuj wszystko:
   ```
   powershell -ExecutionPolicy Bypass -File tools\check.ps1
   ```
   Skrypt znajduje Visual Studio przez `vswhere`, wczytuje jego środowisko x64 do własnego
   procesu (w systemie nic się nie zmienia), używa kopii vcpkg dołączonej do Visual Studio w
   trybie manifestu (`vcpkg.json`, katalog pakietów przypięty do jednego commita), po czym
   konfiguruje, buduje i testuje zestawy `dev` (Debug), `asan` (AddressSanitizer) i
   `analyze` (analiza statyczna MSVC). Ostatni wiersz `CHECK PASSED` oznacza sukces.
   Pełne uruchomienie od czystego klonu trwało u autora 19,6 minuty (12 procesorów logicznych), z
   bibliotekami wziętymi z pamięci podręcznej binariów vcpkg w profilu użytkownika; bez niej
   vcpkg najpierw je buduje, co dodaje czas, którego nie mierzono. Jeden zestaw jest
   szybszy: `tools\check.ps1 -Presets dev`.
3. Budowanie zoptymalizowane (do pomiarów szybkości i oglądania scen):
   ```
   . tools\dev_env.ps1
   cmake --preset release
   cmake --build --preset release
   ```

Wszystkie programy i DLL trafiają do `build\<zestaw>\bin\`. Więcej w
[docs/pl/building.md](docs/pl/building.md).

## Przykłady i testy

Każda funkcja silnika ma mały przykład w `examples\<obszar>\` (na przykład
`examples\core\game_loop`, `examples\render\renderer`, `examples\physics\physics`); budują się
jako `example_<nazwa>.exe` i uruchamiają jako testy. Testy jednego zestawu po zbudowaniu:

```
. tools\dev_env.ps1
ctest --preset dev
```

Jeden przykład ręcznie: `build\dev\bin\example_renderer.exe`. Gra-zabawka:
`build\dev\bin\toy.exe` (z oknem) albo
`build\dev\bin\toy.exe --headless --ticks 600 --dump-frame frame.png` (bez okna; standardowe
opcje w [docs/pl/game_loop.md](docs/pl/game_loop.md)).

## forest_scene i jego zasoby

`forest_scene` to scena pokazowa: 6 x 6 km terenu z generatora silnika, las nad jeziorem, lis,
woda, chmury i niebo dnia. Potrzebuje modeli i tekstur, których nie ma w tym repozytorium. Dwa
sposoby, żeby je mieć:

- **Archiwum zasobów wydania** (`poligon-0.1.0-assets.zip`): rozpakuj je tak, żeby folder
  `poligon-assets` leżał obok folderu programu albo obok folderu projektu (program szuka
  `poligon-assets` w każdym folderze nad sobą). Zawiera wypieczone zasoby w jednym archiwum
  i `ATTRIBUTION.md` z autorem, źródłem i licencją każdego.
- **Własne kopie źródeł**: połóż je w `poligon-assets\source` obok folderu projektu;
  `forest_scene` wypieka je przy starcie do `poligon-assets\cooked`. Lista tego, czego
  potrzebuje i skąd, to `tools\forest_scene\assets.txt`; źródła i licencje są w
  `ATTRIBUTION.md` archiwum zasobów.

Potem uruchom `build\release\bin\forest_scene.exe`. Klawisze: W/S/A/D chodzenie, Shift bieg,
Spacja skok, przytrzymany lewy przycisk myszy rozgląda się, F lot, Tab chowa okna debugowe;
`--quality low|medium|high|ultra` wybiera zestaw grafiki. Pierwszy start generuje i wypieka
teren (kilka minut), jeśli nie ma go w archiwum zasobów.

## Stan i czego brakuje

Wersja 0.1.0 to pierwsze wydanie. Zrobione: warstwa weryfikacji, import modeli, renderer z
kaskadowymi cieniami, materiałami fizycznymi, fizycznym niebem z porą dnia, wolumetrycznymi
chmurami i mgłą, czasowym wygładzaniem ze skalowaniem, przyciemnianiem XeGTAO, mapowaniem
tonów AgX i czterema zestawami grafiki; animacja szkieletowa; fizyka; generator terenu z
erozją i mapami wody; strumieniowany ląd; roślinność rysowana instancjami z impostorami,
rośliny sadzone regułami, trawa robiona kodem; powierzchnie wody; wypiekacz zasobów z
kompresją tekstur i archiwami; okna debugowe.

Jeszcze nie ma: interfejsu dla gier (menu, HUD poza tekstem debugowym), dźwięku, nawigacji
dla zwierząt, skryptów, skalowania FSR 2, edytora poziomów, zapisu i wczytywania gry. Silnik
jest na razie dla autora; API może się zmieniać między wersjami.

## Licencja

MIT, zob. [LICENSE](LICENSE). Biblioteki i cudzy kod wbudowane w silnik, z licencjami:
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

## Dokumentacja

[docs/pl](docs/pl) (po angielsku w [docs/en](docs/en)). [CLAUDE.md](CLAUDE.md) to zbiór zasad
i spis treści dla agenta AI, który pracuje nad silnikiem.

English version of this file: [README.md](README.md).
