# Strumieniowanie świata

Duży świat jest pocięty na kwadratowe **łaty**, a w pamięci jest tylko okno łat wokół kamery. Wzór to theHunter Classic: rezerwat 32 768 m to 64 x 64 łaty po 512 m, wokół kamery wczytane jest okno 9 x 9, doczytywanie idzie w tle pierścieniami od najbliższego, łaty poza oknem są zwalniane, a każdy rodzaj danych robi to osobno.

Nagłówki: `poligon/world/streaming.h` (siatka i streamer), `poligon/world/land.h` (ląd łatami). Przykłady: `examples/world/streaming` (sam streamer), `examples/world/streamed_land` (rysowany świat ze wszystkimi źródłami). Testy: `tests/world/streaming_test.cpp`, `tests/world/terrain_streaming_test.cpp` (oba backendy).

## Pojęcia

- **Łata**: kwadrat świata o boku `StreamingGridSettings::patch_size` metrów, nazwany kolumną i wierszem (`PatchCoord`). Łata (0, 0) zaczyna się w `StreamingGridSettings::origin`; świat ma ich `patch_count_x` x `patch_count_z`.
- **Okno**: łaty w promieniu `radius` łat od tej, w której leży pozycja, kwadrat (2 radius + 1)^2 (promień 4 = 9 x 9). `StreamingGrid::GetWindow` podaje je od najbliższego **pierścienia** (pierścień 0 to sama łata, pierścień 1 jej ośmiu sąsiadów...).
- **Źródło** (`PatchSource`): jeden rodzaj danych wczytywany łatami. `Load` czyta albo liczy łatę, `Attach` ożywia ją w głównym wątku (wysyła obrazy, dodaje rośliny albo ciała), `Detach` ją zwalnia.

## Jak to działa

- `WorldStreamer` ma jeden wątek wczytujący (`poligon/core/worker.h`: `SDL_CreateThread`, `SDL_Mutex` i `SDL_Condition` z SDL3; SDL3 nie ma gotowej kolejki zadań, więc kolejka jest nasza, około 150 wierszy; przykład `examples/core/worker`).
- `AddSource(source, radius, mode)` daje każdemu źródłu jego własne okno.
- `Update(observer)`, raz na klatkę, w ustalonym miejscu przed rysowaniem: gdy obserwator wszedł na inną łatę, łaty, które wypadły z okna źródła **Background**, są odłączane, czekające wczytania odrzucane, a brakujące łaty kolejkowane pierścień po pierścieniu przez wszystkie źródła, więc każdy rodzaj danych blisko obserwatora idzie pierwszy. Potem najwyżej `SetMaxAttachPerFrame` (4) skończonych wczytań jest dołączanych. Wczytanie, które skończy się, gdy jego łata już opuściła okno, jest odrzucane (`StreamingStats::discarded`).
- **Histereza**: łata wchodzi, gdy leży w promieniu `radius` łat od łaty obserwatora, a wychodzi dopiero, gdy obserwator jest dalej niż `radius` + 1/2 łaty od jej kwadratu (`StreamingGrid::GetDistance`); to samo dotyczy źródeł Immediate i ich pozycji. Chodzenie tam i z powrotem przez granicę łaty nie wczytuje więc niczego ponownie: `streaming_test` przesuwa obserwatora o 2 m na obie strony granicy przez 600 ticków i po pierwszym przejściu (42 wczytania w źródle Background i Immediate) nie ma ani jednego wczytania czy zwolnienia więcej. Źródło trzyma do (2 `radius` + 2)^2 łat, i tyle stron potrzebuje strumieniowany `Terrain`.
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
- **Pokrycie terenu (runo)** nie jest źródłem (i tak rośnie w małych komórkach wokół kamery): `GroundCover::SetLand(&land)` sprawia, że mapy każdego miejsca czyta z łaty lądu w tym miejscu, a komórka wyrasta dopiero, gdy każdą łatę, której dotyka, trzyma teren; do tego czasu jest próbowana przy każdym `Update` (łata, którą źródło lądu w międzyczasie zwolniło, jest czytana ponownie).
- **Mapa wydeptania** to stan gry idący za graczem, nie wczytywane dane. `TrampleMap::SetRememberOutside(true)` zachowuje wartości komórek, które wychodzą z jej siatki, i oddaje je, pomniejszone o to, co by przez ten czas odrosło, gdy siatka wróci.

## Wypieczony ląd

Gra czyta ląd wypieczony (`poligon/world/land.h`): poziom wskazuje swój świat (`world worlds/forest`, `world.patch_size`, `world.background_spacing`, patrz [assets.md](assets.md)), a `CookLevelWorld` (albo `asset_cooker --level`) generuje ląd, robi jego wagi warstw podłoża z bloków `ground` poziomu i zapisuje:

- `NAZWA.pworld`: wiersz nagłówka `poligon-world 2`, liczbę nazywającą ustawienia, z których powstał (generatora, warstw podłoża, rozmiar łaty: gdy się zmienią, jest wypiekany od nowa), potem `LandInfo` (łaty, odstępy, rozmiar, jeziora, wysokości i wagi tła);
- `NAZWA/X_Z.ppatch`: wiersz nagłówka `poligon-patch 2`, potem jedna `LandPatch` (jej okno map, wagi, potem bajt flagi i, gdy wynosi 1, indeksy warstw z ich wagami) jako surowe liczby. Pliki wersji 1 (bez flagi i indeksów) dalej się czytają (`land_files_test`).

`LandFromFiles(library, name, world_origin)` czyta je przez `AssetLibrary`, z wypieczonego folderu albo archiwum `POLIPAK1` (czytanego plik po pliku, więc wydana gra trzyma tylko łaty, których używa), w dowolnym wątku. Odczytana łata równa się łacie wyciętej z całych map co do bitu (`land_files_test`, z folderu i z archiwum). Pliki binarne pisze `poligon/core/binary_io.h` (przykład `examples/core/binary_io`), wspólny z plikami assetów.

## Ląd z mapy wysokości

Gra z lądem zrobionym gdzie indziej (prawdziwe dane wysokościowe, własne mapy: wysokość co 4 m, do 32 × 32 km, łaty po 512 m) używa zamiast generatora `LandFromHeightMap(map, patch_size, world_origin, ground, ground_spacing, background_spacing)`. Łaty są wycinane z mapy wysokości (z tym samym marginesem co ląd generowany); tło i podłoga zasłaniania powstają z wysokości; `CookLand` zapisuje go jak ląd generowany, `LandFromFiles` czyta z powrotem, a teren, jego kolizje (`TerrainPhysicsSource`), zasłanianie nieba i dalekie tło działają na nim bez zmian (`height_map_land_test`, przykład `examples/world/height_map_land`).

- **Podłoże:** własny `PatchGroundSource` gry, jedna metoda `GetGround(patch, ground)` wypełniająca `LandPatchGround` (obrazy na własność: dwa obrazy wag albo indeksy warstw z ich wagami) w punktach podłoża łaty, `patch_size / ground_spacing + 1` na bok. Jest wołana w wątku wczytującym, więc może tylko czytać. Nic w silniku nie robi indeksów warstw z map. Podłoże jest wypiekane do plików łat i podawane terenowi przez `TerrainPatchSource`.
- **Czego taki ląd nie ma:** jezior i powierzchni wody, map generatora (wilgotność, przepływ, osad, skała, odległość od wody, poziom wody), a więc warstw podłoża z reguł (`MakeTerrainLayerWeights`, bloki `ground` poziomu) i punktu widokowego na jezioro. `LandPatch::maps` trzyma tylko wysokości i siatkę podłoża; każdy warunek na brakującej mapie jest niespełniony (`MapConditions::Weight` daje 0, zamiast czytać 0, które znaczyłoby "sucho" albo "przy wodzie"), więc reguły roślin i rośliny przyziemne na nachyleniu i wysokości działają, a reguły na innych mapach nic nie sadzą.
- **Dalekie tło** nie ma łat, więc jego podłoże pochodzi z materiału terenu: `TerrainMaterial::layer_indices` (albo wagi) nad całym lądem, na przykład w punktach tła (tak robi przykład); bez nich pokrywa je warstwa 0.

## Dalekie tło

Za oknem ląd nadal jest: strumieniowany `Terrain` rysuje go z `LandInfo::background` (wysokości całego lądu co 64 m, z wagami warstw podłoża w tych samych punktach) wszędzie tam, gdzie żadna łata nie jest trzymana. Zgrubne wysokości są doprowadzone do 64 komórek na łatę (8 m przy łatach 512 m), żeby najmniejszy kwadrat tła był dokładnie jedną łatą; kwadrat z trzymanymi łatami w środku jest dzielony aż do pojedynczych łat, a te są pomijane, a dalekie kwadraty rosną do 2, 4, 8... łat według tej samej reguły odległości co kwadraty samych łat. To jedno wywołanie `DrawTerrain` więcej, czyli jedno rysowanie więcej na przebieg; mgiełka i mgła renderera kryją je jak każdy ląd. `SetBackgroundVisible(false)` je wyłącza; `TerrainStats::background_patches` liczy jego kwadraty.

Impostory lasu na dalekich łatach nie są jeszcze rysowane (patrz Znane ograniczenia).

Co sprawdza `terrain_streaming_test` na 512 m wygenerowanego lądu pociętego na 4 x 4 łaty po 128 m, na obu backendach:

- wysokości i normalne strumieniowanego terenu są równe tym z całego terenu co do bitu, także na granicach łat; największy skok przez granicę na 2 mm to 0,5 mm (spadek terenu, nie szew);
- ląd rysowany łatami wygląda jak ląd rysowany w całości (najwyżej 0,009% pikseli różni się o więcej niż 2 na 255; drzewa kwadratów kończą się na tych samych rozmiarach);
- kolizje: promienie przez granicę łat trafiają w wysokość lądu z dokładnością 0,3 mm; okno 2 x 2 łat w rogu lądu ma 4 pola wysokości, a ląd poza nim nie ma kolizji; gdy okno się przesuwa, stare łaty nie są ani rysowane, ani nie kolidują;
- rośliny z łat to rośliny całego lądu (2599, tyle samo pni), a obraz z nimi jest ten sam piksel w piksel, także na granicach łat;
- woda z łat pokrywa te same kwadraty co `CreateLakeSurfaces` na całym lądzie (żaden piksel się nie różni);
- zasłonięcie nieba złożone z łat równa się temu z całego lądu punkt w punkt, także z koronami roślin (973 punkty pod koronami, bez różnicy);
- runo na strumieniowanym terenie wyrasta takie jak na całym lądzie (4611 kęp, te same przekształcenia), stojąc na rogu czterech łat;
- wydeptana i opuszczona ścieżka jest po powrocie znowu na miejscu, pomniejszona o to, co odrosło;
- przy oknie 2 x 2 łat reszta lądu jest rysowana z tła (tu punkty co 16 m): 12 kwadratów, a 0,07% pikseli szerokiego widoku z góry różni się od całego lądu o więcej niż 16 na 255.

## forest_scene na strumieniowaniu

Od 2026-10-26 `forest_scene` strumieniuje swój ląd 6 x 6 km: poziom wskazuje `worlds/forest` (12 x 12 łat po 512 m), wypiekany do `poligon-assets/cooked` przy pierwszym uruchomieniu (około 10 s poza generowaniem lądu); warstwy podłoża przeszły z kodu do pliku poziomu. Okna: teren i woda 9 x 9 łat wokół kamery, rośliny 7 x 7, zasłonięcie nieba 5 x 5, kolizje lądu oraz pni i kamieni 3 x 3 wokół gracza i każdej skrzyni (`RequirePatches` przed każdym tickiem). Wysokości dla tego, co stoi na lądzie (zwierzęta, skrzynie, ślady), są czytane z łat lądu, nigdy z rysowanego terenu. Przebiegi bez okna wczytują synchronicznie, więc ich obrazy nie zależą od czasu; powtórka tego samego nagrania daje ten sam stan co do bitu.

Zmierzone na GTX 980 w 1920 x 1080, high, w czterech stałych widokach (przed: ląd trzymany w całości):

| | przed | po |
|---|---|---|
| karta, tekstury (własne liczniki renderera) | 614 MB | 491 MB |
| karta, bufory | 30-31 MB | 30-31 MB |
| pamięć procesu (private bytes) | 1750-1960 MB | 1390-1660 MB |
| klatka, medium | 6,2-8,8 ms | 6,5-9,4 ms |
| klatka, high | 8,7-13,3 ms | 9,0-13,7 ms |
| procesor na klatkę | 2,6-5,0 ms | 2,7-5,0 ms |
| krok fizyki | 0,048-0,055 ms | 0,061-0,071 ms |

W porównaniu z lądem trzymanym w całości obrazy czterech widoków różniły się na 0,2 do 2,6% pikseli o więcej niż 12 na 255, przy 200 tickach i tak samo przy 600 (więc nie chodziło o to, co zdążyło się wczytać: przebiegi bez okna wczytują synchronicznie). Trzy przyczyny, znalezione przez porównanie z wersją z lądem trzymanym w całości przy tych samych shaderach:

- Wiatr: faza podmuchu każdej rośliny była skrótem jej pozycji, a komórki roślinności, przesunięte, żeby pasowały do łat, zaokrąglają pozycje inaczej (przy wyłączonym wietrze widok lasu spadł z 2,6 do 0,42%). Faza pochodzi teraz z gładkiego szumu (patrz rendering.md).
- Woda: szum fal był kotwiczony do początku każdej powierzchni wody zaokrąglonego do siatki 4096 m; jezioro, teraz po jednej powierzchni na łatę, dostawało różne kotwice na różnych łatach, co dawało też szwy w falach na niektórych granicach łat. Cała woda klatki ma teraz wspólną kotwicę kamery (widok brzegu: z 1,7 do 0,17%).
- Fizyka: ląd koliduje jako pola wysokości na łatę zamiast jednego, a gracz (z kamerą) i skrzynie spoczywają o ułamek milimetra inaczej (kamera 0,12 mm); krawędzie skrzyń i źdźbeł przesuwają się o ułamek piksela. To zostaje.

Co zostało wobec lądu trzymanego w całości przy tych samych shaderach: 0,18% (start), 0,31% (las), 0,17% (brzeg), 1,39% (tył: stos skrzyń i trawa przed kamerą) pikseli różniących się o więcej niż 12 na 255; poza fizyką gładkie różnice cieniowania siatki zasłonięcia nieba (zaczyna się w rogach łat, metr od dawnego miejsca) i pojedyncze piksele na krawędziach wycinanych liści. Krok fizyki urósł, bo kolizje obejmują teraz 3 x 3 łaty wokół gracza (2,4 km², około 13 000 pni i kamieni) zamiast stałego kwadratu 1 km wokół startu; `forest_scene` tworzy świat fizyki z miejscem na 65 536 ciał.

## Znane ograniczenia

- Jeden wątek wczytujący. Wczytania nie są rozkładane na kilka wątków.
- Strony strumieniowanego terenu są ustalane przy jego tworzeniu; okno większe niż liczba stron kończy program komunikatem.
- Zasłonięcie nieba sięga najdalej o jedną łatę (`reach` do rozmiaru łaty).
- Bez lasu poza oknem roślin: dalekie łaty pokazują goły ląd tła. Impostory stawiane z mapy gęstości lasu nie zostały zbudowane: w `forest_scene`, jedynej mierzonej scenie, reguły sadzą drzewa tylko do 1,2 km od startu, więc nie ma dalekiego lasu, którego koszt dałoby się zmierzyć wobec granicy 1 ms w high.
- Tam, gdzie trzymana łata styka się z tłem, drobny i zgrubny ląd mogą różnić się o kilka metrów; fartuchy łat kryją szczelinę, nie stopień.
- Rośliny łaty są sadzone dwa razy, gdy strumieniowane są i rośliny rysowane, i ich kolizje (planter działa raz na źródło); kosztuje to czas wczytywania, nie pamięć.
