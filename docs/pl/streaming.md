# Strumieniowanie świata

Duży świat jest pocięty na kwadratowe **łaty**, a w pamięci jest tylko okno łat wokół kamery. Wzór to theHunter Classic: rezerwat 32 768 m to 64 x 64 łaty po 512 m, wokół kamery wczytane jest okno 9 x 9, doczytywanie idzie w tle pierścieniami od najbliższego, łaty poza oknem są zwalniane, a każdy rodzaj danych robi to osobno.

Nagłówki: `poligon/world/streaming.h` (siatka i streamer), `poligon/world/land.h` (ląd łatami). Przykłady: `examples/world/streaming` (sam streamer), `examples/world/streamed_land` (rysowany świat ze wszystkimi źródłami). Testy: `tests/world/streaming_test.cpp`, `tests/world/terrain_streaming_test.cpp` (oba backendy).

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

## Ląd i jego źródła

`LandSource` wydaje ląd łata po łacie (`LandPatch`): mapy generatora nad łatą z marginesem 8 m (`kLandPatchMargin`) i wagi warstw podłoża nad punktami mapy łaty. `LandInfo` mówi, jak ląd jest pocięty, gdzie leży (`world_origin`), jakie ma jeziora i jakie **tło**: wysokości całego lądu co 64 m, używane tam, gdzie żadna łata nie jest trzymana. `LandFromMaps` wycina łaty z całych map w pamięci i trzyma ostatnie 32 (najnowsze z przodu, najstarsze nieużywane odpadają); każde źródło czyta przez jeden LandSource, więc łata jest wycinana albo czytana raz.

Źródła, każde z własnym oknem:

| Źródło | Nagłówek | Tryb | Czym staje się łata |
|---|---|---|---|
| `TerrainPatchSource` | terrain.h | Background | stroną strumieniowanego `Terrain` |
| `TerrainPhysicsSource` | terrain.h | Immediate | jednym polem wysokości, z wysokościami całej mapy |
| `WaterPatchSource` | water.h | Background | siatką na jezioro z mokrych kwadratów łaty |
| `PlantPatchSource` | plants.h | Background | roślinami, które stawia tam `Planter`, w `Vegetation` |
| `PlantColliderSource` | plants.h | Immediate | ich pniami i bryłami w świecie fizyki |
| `SkyOcclusionPatchSource` | sky_occlusion.h | Background | kafelkiem zasłonięcia nieba (ląd i korony), łączonym dla renderera |

- **Strumieniowany teren**: `Terrain(renderer, land_info, config, pages)` trzyma do `pages` łat; każda jest stroną tablicy tekstur na karcie (`Renderer::CreateTerrainPages`, `SetTerrainPage`), z obwódką jednego punktu, żeby kierunek podłoża na krawędzi był dobry, i z własnymi wagami warstw podłoża. Każda łata to własne drzewo kwadratów (rozmiar łaty musi wynosić 64, 128, 256, 512... punktów wysokości); wszystkie trzymane łaty idą w jednym wywołaniu `DrawTerrain`, jedno rysowanie na przebieg. `GetHeight`, `GetNormal` i `GetSlopeDegrees` czytają trzymaną łatę, a tam, gdzie jej nie ma, tło.
- **Rośliny**: gra daje `Planter`, funkcję, która stawia rośliny prostokąta lądu (zwykle `PlacePlantGroupInRectangle` z regułami poziomu). Jest wołana w wątku wczytującym i przez kilka źródeł dla tej samej łaty, więc wolno jej tylko czytać. Komórki `Vegetation` muszą pasować do łat: `SetCellOrigin(początek lądu)` i rozmiar łaty będący całą liczbą komórek; `RemoveInstancesInRectangle` zabiera wtedy dokładnie rośliny łaty.
- **Zasłonięcie nieba**: kafelek łaty liczy się z łaty i jej ośmiu sąsiadów (więc `reach` nie może być dłuższy niż łata) i z koron, które planter stawia na niej i wokół niej; trzymane kafelki są łączone w jedną mapę, budowaną od nowa tylko po zmianie.
- **Pokrycie terenu (runo)** nie jest źródłem (i tak rośnie w małych komórkach wokół kamery): `GroundCover::SetLand(&land)` sprawia, że mapy każdego miejsca czyta z trzymanej łaty lądu, a komórka wyrasta dopiero, gdy każda łata, której dotyka, jest trzymana przez ląd i teren; do tego czasu jest próbowana przy każdym `Update`.
- **Mapa wydeptania** to stan gry idący za graczem, nie wczytywane dane. `TrampleMap::SetRememberOutside(true)` zachowuje wartości komórek, które wychodzą z jej siatki, i oddaje je, pomniejszone o to, co by przez ten czas odrosło, gdy siatka wróci.

Co sprawdza `terrain_streaming_test` na 512 m wygenerowanego lądu pociętego na 4 x 4 łaty po 128 m, na obu backendach:

- wysokości i normalne strumieniowanego terenu są równe tym z całego terenu co do bitu, także na granicach łat; największy skok przez granicę na 2 mm to 0,5 mm (spadek terenu, nie szew);
- ląd rysowany łatami wygląda jak ląd rysowany w całości (najwyżej 0,009% pikseli różni się o więcej niż 2 na 255; drzewa kwadratów kończą się na tych samych rozmiarach);
- kolizje: promienie przez granicę łat trafiają w wysokość lądu z dokładnością 0,3 mm; okno 2 x 2 łat w rogu lądu ma 4 pola wysokości, a ląd poza nim nie ma kolizji; gdy okno się przesuwa, stare łaty nie są ani rysowane, ani nie kolidują;
- rośliny z łat to rośliny całego lądu (2599, tyle samo pni), a obraz z nimi jest ten sam piksel w piksel, także na granicach łat;
- woda z łat pokrywa te same kwadraty co `CreateLakeSurfaces` na całym lądzie (żaden piksel się nie różni);
- zasłonięcie nieba złożone z łat równa się temu z całego lądu punkt w punkt, także z koronami roślin (973 punkty pod koronami, bez różnicy);
- runo na strumieniowanym terenie wyrasta takie jak na całym lądzie (4611 kęp, te same przekształcenia), stojąc na rogu czterech łat;
- wydeptana i opuszczona ścieżka jest po powrocie znowu na miejscu, pomniejszona o to, co odrosło.

## Znane ograniczenia

- Jeden wątek wczytujący. Wczytania nie są rozkładane na kilka wątków.
- Na razie bez histerezy: chodzenie tam i z powrotem przez granicę łaty za każdym razem wczytuje i zwalnia ten sam rząd łat.
- Strony strumieniowanego terenu są ustalane przy jego tworzeniu; okno większe niż liczba stron kończy program komunikatem.
- Zasłonięcie nieba sięga najdalej o jedną łatę (`reach` do rozmiaru łaty).
- Rośliny łaty są sadzone dwa razy, gdy strumieniowane są i rośliny rysowane, i ich kolizje (planter działa raz na źródło); kosztuje to czas wczytywania, nie pamięć.
