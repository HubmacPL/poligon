# Strumieniowanie świata

Duży świat jest pocięty na kwadratowe **łaty**, a w pamięci jest tylko okno łat wokół kamery. Wzór to theHunter Classic: rezerwat 32 768 m to 64 x 64 łaty po 512 m, wokół kamery wczytane jest okno 9 x 9, doczytywanie idzie w tle pierścieniami od najbliższego, łaty poza oknem są zwalniane, a każdy rodzaj danych robi to osobno.

Nagłówek: `poligon/world/streaming.h`. Przykład: `examples/world/streaming`. Test: `tests/world/streaming_test.cpp`.

## Pojęcia

- **Łata**: kwadrat świata o boku `StreamingGridSettings::patch_size` metrów, nazwany kolumną i wierszem (`PatchCoord`). Łata (0, 0) zaczyna się w `StreamingGridSettings::origin`; świat ma ich `patch_count_x` x `patch_count_z`.
- **Okno**: łaty w promieniu `radius` łat od tej, w której leży pozycja, kwadrat (2 radius + 1)^2 (promień 4 = 9 x 9). `StreamingGrid::GetWindow` podaje je od najbliższego **pierścienia** (pierścień 0 to sama łata, pierścień 1 jej ośmiu sąsiadów...).
- **Źródło** (`PatchSource`): jeden rodzaj danych wczytywany łatami. `Load` czyta albo liczy łatę, `Attach` ożywia ją w głównym wątku (wysyła obrazy, dodaje rośliny albo ciała), `Detach` ją zwalnia.

## Jak to działa

- `WorldStreamer` ma jeden wątek wczytujący (`poligon/core/worker.h`: `SDL_CreateThread`, `SDL_Mutex` i `SDL_Condition` z SDL3; SDL3 nie ma gotowej kolejki zadań, więc kolejka jest nasza, około 150 wierszy).
- `AddSource(source, radius, mode)` daje każdemu źródłu jego własne okno.
- `Update(observer)`, raz na klatkę, w ustalonym miejscu przed rysowaniem: gdy obserwator wszedł na inną łatę, łaty, które wypadły z okna źródła **Background**, są odłączane, czekające wczytania odrzucane, a brakujące łaty kolejkowane pierścień po pierścieniu przez wszystkie źródła, więc każdy rodzaj danych blisko obserwatora idzie pierwszy. Potem najwyżej `SetMaxAttachPerFrame` (4) skończonych wczytań jest dołączanych. Wczytanie, które skończy się, gdy jego łata już opuściła okno, jest odrzucane (`StreamingStats::discarded`).
- `RequirePatches(positions)`, przed tickiem: każde źródło **Immediate** dostaje każdą łatę w swoim promieniu od dowolnej z pozycji, wczytaną i dołączoną od razu w wątku wołającym, w ustalonej kolejności, i zwalnia łaty, których żadna pozycja nie potrzebuje.
- `SetSynchronous(true)` sprawia, że `Update` wczytuje od razu także źródła Background: dla przebiegów bez okna, których obrazy nie mogą zależeć od czasu. `FinishLoading` czeka na wszystkie zakolejkowane wczytania i je dołącza.

## Determinizm

Stan gry nie może zależeć od tego, jak szybko szło wczytywanie:

- To, na czym stoi symulacja (wysokości terenu dla fizyki, kolizje drzew i kamieni), jest źródłem **Immediate**. Gra prosi o nie przed każdym tickiem z pozycjami wszystkiego, co się rusza (`RequirePatches({gracz, zwierzęta, rzucone rzeczy})`), więc jest wczytywane i zwalniane wyłącznie według stanu gry. Numery ciał w świecie fizyki przychodzą przez to w tej samej kolejności w każdym przebiegu.
- Wszystko, co jest tylko rysowane, to źródła **Background**; ich czas zmienia obrazy, nigdy stan.
- `Load` źródła działa w wątku wczytującym: wolno mu tylko czytać dane, których główny wątek nie zmienia (wypieczone pliki, całe mapy, reguły).
- `streaming_test` prowadzi postać przez 1 km wzgórz pociętych na łaty po 128 m, z rzucanymi co jakiś czas skrzyniami, raz z wczytywaniem w tle trwającym 0 ms, raz 15 ms: skrót stanu fizyki jest ten sam we wszystkich 600 tickach, choć wolny przebieg przekazał 10 łat grafiki, a szybki 25.

## Łaty liczone z całości

Generator terenu symuluje cały ląd naraz (rzeki i erozja nie są lokalne), więc ląd powstaje w całości przy wypiekaniu i wtedy jest cięty na łaty (decyzja z 2026-10-26). Wszystko, co liczy się z niego na łatę, daje dokładnie to, co całość:

- `CutHeightMap` i `CutTerrainMaps` wycinają okno mapy wysokości albo wszystkich map generatora. Okno zachowuje współrzędne całej mapy (`HeightMap::first_x`, `TerrainMaps::map_first_x`), więc wewnątrz niego `GetHeight`, `Sample`, `GetWaterDepth` i `MapConditions::Weight` dają liczby całej mapy co do bitu, o ile okno ma margines dwóch punktów mapy wokół łaty.
- `PlacePlantsInRectangle` i `PlacePlantGroupInRectangle` sadzą rośliny reguły tylko wewnątrz prostokąta łaty. Każda komórka siatki decyduje ze swoich współrzędnych i ziarna, a miejsce należy do dokładnie jednego prostokąta, więc łaty razem dają rośliny całej mapy, każdą raz, w dowolnej kolejności łat. `plant_rules_test` sprawdza to co do bitu na 6 x 6 łatach po 100 m (tnących komórki siatki 4 m), odwiedzanych w pomieszanej kolejności.

## Znane ograniczenia

- Jeden wątek wczytujący. Wczytania nie są rozkładane na kilka wątków.
- Na razie bez histerezy: chodzenie tam i z powrotem przez granicę łaty za każdym razem wczytuje i zwalnia ten sam rząd łat.
