# Poligon

Silnik gier na PC (Windows) pod pracę z agentem AI. Nazwa jest celowa: to miejsce, w którym agent sprawdza swój kod.
Ten plik to spis treści i zasady. Szczegóły tematów trafiają do osobnych plików, do których stąd odsyłamy.

## Spis treści

- Budowanie, konfiguracje, dodawanie testów i przykładów: [docs/building.md](docs/building.md)
- Pętla gry, tick i klatka, tryb bez okna, wiersz poleceń, błędy krytyczne: [docs/game_loop.md](docs/game_loop.md)
- Renderer, konwencje matematyczne, shadery HLSL, backend, zrzut klatki i porównywanie obrazów: [docs/rendering.md](docs/rendering.md)
- ECS (flecs): reguły, zmiany w trakcie systemu, zrzut stanu do JSON, skrót stanu, test powtarzalności: [docs/ecs.md](docs/ecs.md)
- Wejście jako akcje, wiązania klawiszy, nagrywanie i odtwarzanie, format pliku nagrania: [docs/input.md](docs/input.md)
- Determinizm: co gwarantujemy, jak porównać dwa przebiegi i znaleźć tick rozjazdu, co go psuje: [docs/determinism.md](docs/determinism.md)
- Awarie: raport ze stosem, kody wyjścia programów, jak czytać raport: [docs/crashes.md](docs/crashes.md)
- Układ: `engine/` (biblioteka `poligon_engine`, nagłówki w `engine/include/poligon/`), `game/` (gra-zabawka `toy`), `examples/`, `tests/`, `tools/` (skrypty oraz `image_compare` i `state_compare`), `cmake/`.
- Po każdej zmianie w kodzie: `powershell -ExecutionPolicy Bypass -File tools\check.ps1` i dopiero `CHECK PASSED` znaczy, że działa.

## Stan projektu

- Faza: po pierwszym kamieniu milowym. Renderer i ECS wybrane. Repozytorium gita istnieje (gałąź `main`), bez zdalnego. Commit i push tylko na polecenie autora.
- Kamień milowy 1: scena-zabawka uruchamiana bez okna na N klatek ze stałym krokiem czasu, nagranie i odtworzenie wejścia, zrzut stanu do JSON, zrzut klatki do PNG, test identyczności dwóch odtworzeń.
- Kroki (po każdym raport i stop): 1 szkielet budowania, 2 pętla ze stałym krokiem i tryb bez okna, 3 SDL3 GPU i PNG (tu wybór backendu D3D12/Vulkan po teście obu), 4 flecs i zrzut JSON (w tym wczesny test: dwa uruchomienia tej samej sekwencji tworzenia, usuwania ze środka i doczepiania komponentów dają identyczny JSON; przy rozjeździe stop i raport), 5 nagrywanie i odtwarzanie wejścia, 6 test determinizmu, 7 zapis awarii, ctest, pierwszy przykład.
- Kamień milowy 1 ukończony (kroki 1 do 7).
- Kamień milowy 2, modele i tekstury (po każdym kroku raport i stop): 1 GLM zamiast własnej matematyki (zrobione), 2 tekstury, 3 modele glTF przez cgltf (tylko nieruchome), 4 zabawka z wczytanym modelem, błędy dla złych plików, dokumentacja. Poza zakresem: PBR i mapy normalnych, animacja szkieletowa, tekstury skompresowane.
- Zasoby testowe generujemy sami; cudzych plików w repozytorium nie ma. Wczesny test kolejności flecs przeszedł (`tests/ecs_order_probe.cpp`), flecs zostaje.

## Czym to ma być

- Framework/biblioteka w C++, na której grę pisze się w kodzie. Edytor poziomów to osobny program.
- Na razie tylko dla autora. Niczego nie robimy pod obcych użytkowników, ale nie zamykamy drogi do publikacji.
- Budujemy z gotowych klocków open source, nie od zera.
- Docelowe gry: otwarty teren, roślinność, animowane zwierzęta i humanoidy (theHunter Classic, gra wędkarska, survival w otwartym świecie). Głównie 3D, z możliwością 2D.
- Kluczowa własna część: warstwa weryfikacji dla agenta. Uruchomienie bez okna, zrzuty klatek, zrzut stanu gry do pliku, nagrywanie wejścia i deterministyczne odtwarzanie (ta sama maszyna i ten sam plik wykonywalny). Wszystko, co pozwala agentowi samemu stwierdzić, czy zmiana działa, jest ważniejsze niż wygoda pisania ręcznego.

## Decyzje podjęte (nie wracać)

- Język: C++. Rust z Bevy odrzucony.
- Własny rdzeń z osobnych klocków. Wicked Engine jako baza odrzucony.
- MSVC z Visual Studio 2022, x64, CMake, budowanie z konsoli. Edycja w VS Code.
- Zależności silnika i gry tylko na licencjach MIT, BSD, Apache 2.0, zlib, Boost, MPL 2.0. Nic na GPL ani LGPL. Licencję każdej nowej zależności sprawdzić i podać przed dodaniem. Osobno uruchamiane narzędzia (np. Blender) mogą mieć dowolną licencję.
- Silnik i gra w osobnych folderach. Gra zna silnik, silnik nie zna gry.
- Historia repozytorium czysta od początku: żadnych cudzych zasobów, kodu bez licencji, haseł ani prywatnych ścieżek.
- Bezpieczeństwo pamięci przejmuje warstwa weryfikacji: AddressSanitizer w budowie testowej, ostrzeżenia jako błędy, analiza statyczna, zapis awarii ze stosem i nazwami funkcji.
- Renderer: własny, nad SDL3 GPU (zlib). SDL3 daje też okno, wejście i dźwięk. Shadery w zwykłym HLSL, kompilowane offline. 2D jako własny rysownik sprite'ów na tej samej warstwie. bgfx, Filament i Diligent odrzucone.
- Backend graficzny: Direct3D 12 domyślnie, bo jego warstwa diagnostyczna działa na tej maszynie od ręki; Vulkan budowany i testowany jako drugi. Każdy test obrazu przechodzi na obu.
- Matematyka: GLM (na licencji MIT), używane wprost w API silnika, zawsze przez `poligon/math.h`. Własnej matematyki nie piszemy.
- Wczytywanie modeli: cgltf (MIT). Obrazy tekstur: `stb_image` z paczki stb.
- ECS: flecs (MIT). Reguła: tylko typowane API C++, żadnych zapytań w napisach (język zapytań flecs jest zakazany). Każdy komponent rejestruje swoje pola, bo od tego zależy zrzut stanu. Serwer REST flecs tylko w budowie deweloperskiej. EnTT odrzucony. Zrzut stanu jest własny (`poligon/state_dump.h`), bo `world.to_json()` z flecs obcina liczby zmiennoprzecinkowe.
- flecs i determinizm: wszystkie systemy rejestrujemy na starcie, zanim jakikolwiek obiekt zostanie usunięty, i każdy jawnie podpinamy pod fazę. Powód: flecs porządkuje systemy po identyfikatorze, a identyfikator odzyskany po usuniętym obiekcie psuje kolejność.
- Kompilator shaderów DXC (licencja NCSA, spoza listy) jest dopuszczony wyłącznie jako narzędzie uruchamiane przy budowaniu. Gdyby `dxcompiler.dll` miał jechać razem z grą (np. kompilacja shaderów w działającej grze), to jest nowa zależność i osobna decyzja autora.
- vcpkg: kopia dołączona do Build Tools, tryb manifestu, katalog pakietów przypięty do commita w `vcpkg.json`. Osobnej kopii vcpkg autora (poza projektem) nie ruszamy. Instalacji Visual Studio nie zmieniamy (komunikaty kompilatora zostają po polsku).
- Test determinizmu: identyczny JSON stanu z dwóch odtworzeń to wymóg twardy. PNG porównujemy z tolerancją, bez wymogu identyczności bit w bit.
- Agent nie zna API silnika z treningu, więc: mały działający przykład na każdą funkcję silnika, budowany i uruchamiany przy każdym sprawdzeniu; API nudne i przewidywalne, nazwy jak w znanych silnikach, bez własnych minijęzyków i sprytnych makr.

## Kandydaci na klocki (wstępnie)

Fizyka: Jolt. Animacja szkieletowa: ozz-animation. Nawigacja: Recast/Detour. Narzędzia i podgląd: Dear ImGui. Zależności przez vcpkg.

## Nierozstrzygnięte

- Teren, roślinność, strumieniowanie świata, woda: największa własna praca. Start od 1 km².
- Edytor terenu i poziomów.
- Skrypty (np. Luau) i przeładowanie kodu bez restartu: później.
- Sieć: później.
- Licencja silnika: przy publikacji.

## Sprzęt

Windows 10, GTX 980 (DirectX 12 bez ray tracingu, Vulkan), Visual Studio 2022. Przed zaproponowaniem techniki renderowania sprawdzić, czy ta karta ją obsługuje.

## Zasady pracy

- Rozmowa po polsku. Kod, komentarze w kodzie i komunikaty commitów po angielsku.
- Szacunki w godzinach sesji, nie w tygodniach pracy zespołu.
- Żadnych skrótów bez wiedzy autora. Tańszą drogę zgłosić przed zrobieniem, z kosztem obu wersji, i poczekać na decyzję. W raporcie po pracy nazwać wprost każde uproszczenie.
- Przed uruchomieniem równoległych agentów zapytać i podać koszt w tokenach.
- Jeśli coś jest z pamięci, a nie sprawdzone, powiedzieć to. Stan bibliotek i licencje sprawdzać w sieci albo w repozytorium.
- Nie tworzyć dokumentów planów ani analiz bez prośby. Wyjątek: ten plik.
- Autor zna C++. Pojęcia z silników gier tłumaczyć przy pierwszym użyciu.
