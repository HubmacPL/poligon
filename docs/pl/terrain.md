# Teren

Kształt terenu jako mapa wysokości, skąd się bierze i jak prawdziwe dane wysokościowe stają się terenem gry.

Nagłówki: `poligon/height_map.h`, `poligon/terrain.h`, `poligon/terrain_generator.h`. Narzędzie: `terrain_tool`. Przykłady: `examples/terrain`, `examples/terrain_generator`. Testy: `tests/height_map_test.cpp`, `tests/terrain_render_test.cpp`, `tests/terrain_generator_test.cpp`.

**Stan:** teren jest generowany przez symulację wypiętrzania, erozji rzecznej i zboczowej, kropli deszczu i jezior, razem z mapami wody, przepływu, wilgotności, osadów i skały; jest rysowany po horyzont z poziomami szczegółowości, z maksymalnie ośmioma warstwami podłoża wybieranymi według tych map, oświetlany i cieniowany jak wszystko inne, a fizyka się z nim zderza. Jeszcze nie: powierzchnie wody (następny krok), wygładzanie zmiany między rozmiarami kafli, rośliny rozmieszczane regułami.

## Rysowanie

```cpp
poligon::TerrainConfig config;
config.origin = {-3000.0f, -280.0f, -3000.0f};       // położenie pierwszego punktu mapy w świecie
config.material.colors = renderer.CreateTextureArray(layer_colors, 4);         // jeden obraz na warstwę
config.material.normals = renderer.CreateTextureArray(layer_normals, 4, data); // data: is_color = false
config.material.layer_count = 4;
config.material.weights[0] = renderer.CreateTexture(weights[0], data);         // z MakeTerrainLayerWeights
poligon::Terrain terrain(renderer, height_map, config);
// co klatkę, między BeginFrame a EndFrame:
terrain.Draw(camera);
```

- Teren nie jest jedną siatką. Jest rysowany jako kwadratowe **kafle** z drzewa kwadratów: cała mapa to jeden kwadrat; kwadrat bliższy kamerze niż jego rozmiar razy `TerrainConfig::detail` dzieli się na cztery, aż do najmniejszego rozmiaru (64 oczka mapy wysokości). Pod kamerą jest wierzchołek na każdy punkt mapy; zbocze odległe o kilometry to kilka zgrubnych kwadratów. Mapa 6 × 6 km z punktem co metr to 36 milionów punktów, a rysuje się ją kilkuset kaflami.
- Każdy kafel to ta sama płaska siatka 64 × 64 oczek. Shader wierzchołków (`terrain.vert.hlsl`) podnosi każdy wierzchołek na wysokość terenu, odczytaną z obrazu, który trzyma całą mapę na karcie graficznej (16 bitów na punkt: 72 MB dla 6000 × 6000). Z mapy nigdy nie buduje się siatki.
- Tam, gdzie drobny kafel styka się ze zgrubnym, ich krawędzie nie pasują dokładnie. Każdy kafel ma **fartuch**, zwisający brzeg, który zasłania szczeliny.
- Kierunek, w który zwrócony jest grunt, jest wyliczany dla każdego piksela z obrazu wysokości (`terrain.frag.hlsl`), więc dalekie zgrubne kafle są cieniowane równie dokładnie jak bliskie.
- Wygląd (`TerrainMaterial`): do ośmiu warstw podłoża mieszanych obrazami wag (patrz Warstwy podłoża niżej); każda tekstura jest czytana we własnym rozmiarze i siedem razy większa, a większy odczyt przejmuje z odległością, co ukrywa powtarzanie; zbocza między `steep_start_degrees` a `steep_end_degrees` przechodzą w `steep_color`.
- Oświetlenie jest wspólne z siatkami (`lighting.hlsli`): słońce, cienie we wszystkich czterech kaskadach, światło nieba, przyciemnianie zagłębień, mgła.
- `Terrain::GetHeight(x, z)`, `GetNormal`, `GetSlopeDegrees` odpowiadają dla pozycji w świecie; `GetStats()` liczy kafle według rozmiaru.
- Pod `Terrain` jest `Renderer::CreateTerrain` / `DrawTerrain(terrain, material, patches, count)`; wszystkie kafle jednego wywołania kosztują jedno polecenie rysowania na przebieg, a każdy jest odrzucany osobno.

## Fizyka

- `PhysicsWorld::AddHeightField(heights, size, spacing, origin)` robi grunt z kwadratowej siatki wysokości. Chodząca postać, bryły i promienie traktują go jak każdy inny grunt.
- Przy dużej mapie fizyce daje się tylko część, w której coś się dzieje (`CropHeightMap`): `forest_scene` używa 1 × 1 km wokół startu gracza, z płotami na brzegu.

## W forest_scene

`forest_scene` generuje domyślny teren przy pierwszym uruchomieniu, a potem wczytuje go z `build/terrain_cache`. Gracz startuje na łagodnym terenie 150 do 500 m od największego jeziora, jak najwyżej nad nim (do 40 m) tak, by środek jeziora był widoczny, patrząc na nie; tam jest początek układu świata. Siedem warstw podłoża idzie za mapami: omszała ściółka leśna wszędzie, sucha ściółka w plamach na suchszym terenie, trawa na polanach, błoto tam, gdzie zbiera się woda, i wzdłuż brzegu, piasek i żwir na linii brzegu i w płytkiej wodzie, drobne kamienie na dnie jeziora, skała na stromych miejscach. Podszyt i kamienie stoją w promieniu 250 m od startu, a duże drzewa ciągną się do 1200 m, żadne na zboczu powyżej 33 stopni ani w jeziorze, z polaną od startu w dół do jeziora. Drzewa rzucają cienie do 65 m. (Wybór startu, polana i reguły warstw to własne decyzje sceny; reguły sadzenia czytające mapy to następne kroki.)

## Teren generowany

Nagłówek: `poligon/terrain_generator.h`. Przykład: `examples/terrain_generator`. Test: `tests/terrain_generator_test.cpp`. Od 2026-10-15 `forest_scene` stoi na terenie generowanym; dane Batorza i czytnik TIFF zostają.

Generator symuluje procesy, które kształtują prawdziwy teren, zamiast dodawać szum. Żadna biblioteka nie pasowała (sprawdzone 2026-10-15: fastscapelib ma GPL-3, RichDEM nie ma licencji, SimpleHydrology ma MIT, ale nie robi jezior, FastNoise2 ma MIT, ale jego ścieżki SIMD nie są powtarzalne co do bitu), więc to nasz kod według tych publikacji:

1. **Duża skala**, na zgrubnej siatce (`coarse_spacing`, 4 m): przebieg startuje od starszych, łagodnych wzgórz (`initial_relief`, `initial_hill_size`). Skorupa się wypiętrza (`uplift`, w szerokich plamach od 55 do 100% tej wartości); rzeki wcinają się według prawa mocy strumienia E = K A^m S (`erodibility`, `area_exponent`), rozwiązywanego niejawnie od ujść w górę (Braun i Willett 2013), więc każdy krok czasu jest stabilny; zbocza pełzną (`hillslope_diffusion`); woda spływa po terenie z każdym zagłębieniem wypełnionym po brzeg (Priority-Flood z epsilonem, Barnes, Lehman i Mulla 2014), a osad wniesiony do wypełnionego zagłębienia tam opada. Pod koniec przebiegu (`basin_start`) kilka nieregularnych niecek przestaje się wznosić (`basin_count`, `basin_radius`, `basin_sinking`), jak ląd nad uskokami albo topniejącym lodem; powstają w nich jeziora. Brzegi odpływowe (`outlet_edges`) zastępują teren, który ciągnie się za mapą: trzymają wysokości dawnych wzgórz i wznoszą się powoli, więc nie powstaje wzdłuż nich urwisko. Wynik jest skalowany do `relief` metrów.
2. **Mała skala**, na siatce wysokości (`spacing`, 1 m): zgrubny teren jest interpolowany (Catmull-Rom), dostaje nierówności `detail_height` (mniejsze na płaskim), luźny materiał zsuwa się wszędzie, gdzie zbocze przekracza `talus_degrees` (erozja termiczna, Musgrave, Kolb i Mace 1989), potem `droplets_per_square_meter` kropli deszczu spływa w dół najwyżej przez `droplet_steps` metrów, żłobiąc rynny i żleby i zostawiając materiał tam, gdzie zwalniają (Beyer 2015). Prędkość każdej kropli i jej cięcie na krok są ograniczone, a erozja wygasa przy brzegu mapy: bez tego stroma ściana sama się pogłębia w lej na setki metrów (raz się to zdarzyło, na pełnej mapie).
3. **Mapy**, na siatce map (`map_spacing`, 2 m): `water_level` (jeziora: zagłębienia wypełnione po brzeg, zostawiane, jeśli mają co najmniej `min_lake_area` i `min_lake_depth`; poziom sięga o jeden punkt za brzeg, żeby linia brzegu szła za wysokościami co 1 m), `flow` (metry kwadratowe, które spływają przez punkt, przepuszczane przez jeziora), `moisture` (z topograficznego wskaźnika wilgotności ln(a / tg nachylenia), Beven i Kirkby 1979, podnoszona przy wodzie), `sediment` (metry osadu z jezior, osuwania i kropli), `rock` (strome miejsca bez luźnej pokrywy i miejsca wymyte), `water_distance` i lista `lakes` (środek, poziom, powierzchnia, głębokość). `TerrainMaps::Sample` i `GetWaterDepth` czytają je w dowolnym miejscu.

- **Powtarzalność:** jeden wątek, własne liczby losowe (SplitMix64) i szum, bez fast-math. Te same ustawienia dają ten sam teren co do bitu; `terrain_generator_test` generuje dwa razy i porównuje skrót wszystkich map, sprawdza też zapisany plik. Budowa debug i release dały ten sam skrót.
- **Czas i rozmiar:** domyślne 6 × 6 km trwa około trzech minut w budowie `release` (duża skala około dwóch, krople większość reszty). `LoadOrGenerateTerrain(settings, folder, maps, generated, error)` trzyma wynik w pliku nazwanym skrótem ustawień i wersji generatora (`kGeneratorVersion`, podnoszona przy każdej zmianie wyniku) i wczytuje go w ułamku sekundy; plik ma 344 MB (wysokości i sześć map jako liczby 32-bitowe). `forest_scene` i `terrain_tool` trzymają go w `build/terrain_cache`.
- **Domyślny teren:** różnica wysokości 110 m, doliny rozgałęziające się co kilkaset metrów, 89% łagodniejsze niż 10 stopni i prawie nic bardziej strome niż 25 (teren miękki i stary, jak wyżyny lessowe wokół Batorza), dwa jeziora po 0,2 i 0,5 km2, około 8 m głębokości. Na takim terenie nie ma gołej skały; warstwa skały pokrywa tylko nieliczne strome ściany.
- **Dlaczego 6 × 6 km co 1 m:** tak samo jak mapa Batorza, więc rysowanie terenu, fizyka i zmierzone czasy klatek pozostają porównywalne; to duża skala co 4 m utrzymuje czas w minutach (co 1 m trwałaby 16 razy dłużej).
- **Uproszczenia:** sieć rzek jest ustalona przez zgrubną siatkę, więc koryta węższe niż około 8 m pochodzą tylko od kropli; jeziora nie mają własnego odpływu poza przelewaniem się przez brzeg, a woda nie paruje; nie ma lodowców, warstw skał o różnej twardości ani meandrujących rzek; strumienie nie są jeszcze powierzchniami wody (tylko jeziora).

## Warstwy podłoża

- `TerrainMaterial` miesza do ośmiu warstw (`kMaxTerrainLayers`). Ich tekstury to dwie tablice tekstur (`Renderer::CreateTextureArray`, wszystkie obrazy jednego rozmiaru: `colors`, `normals`), `tile_sizes` podaje powtarzanie każdej warstwy w metrach, a dwa obrazy wag (`weights`, RGBA = warstwy 0 do 3 i 4 do 7, jeden piksel na punkt mapy, rozciągnięte na całą mapę wysokości) mówią, ile której warstwy leży gdzie.
- `MakeTerrainLayerWeights(maps, rules, count, seed, weights)` wypełnia obrazy wag z map generatora: `TerrainLayerRule` dla każdej warstwy podaje miękkie zakresy (`TerrainRange`: low, high, fade) nachylenia, wysokości, wilgotności, osadu, skały, przepływu (log10 metrów kwadratowych), odległości od wody i głębokości wody, opcjonalne plamy z szumu (`patch_size`, `patch_cover`) i `strength`. Warunki się mnożą, wagi w każdym punkcie są skalowane do sumy 1, a gdzie żadna reguła nie pasuje, leży warstwa 0.
- Shader (`terrain.frag.hlsl`) czyta każdą warstwę z wagą w pikselu, z bliska i siedem razy większą jak dotąd, i miesza je **wysokością**: każda warstwa stoi tak wysoko, jak jej waga plus jasność jej tekstury w tym miejscu, i widać tylko warstwy w krótkim kroku od najwyższej. Granice wyglądają wtedy jak kamienie wystające z piasku, a nie jak przenikanie. Odczyty tekstur używają gradientów policzonych przed rozgałęzieniami, więc mipmapy są poprawne tam, gdzie warstwy zmieniają się z piksela na piksel.
- Bez tekstur grunt jest biały, bez wag leży warstwa 0; kolor stromych zboczy dalej działa (`steep_start_degrees` 90 go wyłącza, gdy strome miejsca pokrywa warstwa skały).
- Wagi są czytane tylko w najdrobniejszym poziomie; bardzo daleko mogą lekko migotać (TAA ukrywa większość). Tekstury warstw mają w `forest_scene` 1024 × 1024, dopóki kompresja tekstur nie przyjdzie z systemem assetów.
- Test: część o warstwach w `tests/terrain_render_test.cpp` i część o wagach w `tests/terrain_generator_test.cpp`.

## Mapy wysokości

- `HeightMap` to siatka wysokości o tej samej odległości (`spacing`, metry) między sąsiednimi punktami w obu kierunkach. x biegnie wzdłuż wiersza, z od wiersza do wiersza; wiersz 0 to północna krawędź prawdziwych danych, więc północ to -Z, jak wszędzie w silniku.
- `GetHeight(x, z)` miesza cztery punkty wokół miejsca; `GetNormal`, `GetSlopeDegrees` (0 płasko, 45 tak stromo, jak daleko), `GetHeightRange`.
- `MakeNoiseHeightMap(width, depth, spacing, seed, hill_size, hill_height)` robi łagodne wzgórza, takie same dla tych samych argumentów.
- Pliki: `SaveHeightMap` / `LoadHeightMap` używają własnego formatu Poligonu: wiersz tekstu (`poligon-heightmap 1 SZEROKOŚĆ GŁĘBOKOŚĆ ODSTĘP`), a po nim liczby 32-bitowe. Mapa 6000 × 6000 ma 137 MB i wczytuje się w ułamku sekundy.

## Prawdziwe dane wysokościowe

Źródło: Geoportal (GUGiK), numeryczny model terenu z lotniczego skanowania laserowego, jedna wysokość na metr, goły grunt bez drzew i budynków. Geoportal podaje, że dane są bezpłatne i można ich używać w dowolnym celu.

- Usługa (WCS 2.0.1): `https://mapy.geoportal.gov.pl/wss/service/PZGIK/NMT/GRID1/WCS/DigitalTerrainModelFormatTIFF`, warstwa `DTM_PL-KRON86-NH_TIFF`.
- Współrzędne są w polskim układzie EPSG:2180. W zapytaniu `SUBSET=x(...)` to współrzędna **wschodnia**, a `SUBSET=y(...)` **północna**; odwrotnie dostaje się plik pełen zer, bez żadnego błędu.
- `SCALEFACTOR=0.5` zwraca co drugi metr.
- Odpowiedź to TIFF z jedną nieskompresowaną liczbą 32-bitową na piksel. `LoadHeightMapFromTiff` czyta dokładnie to i nic więcej; nie jest ogólnym czytnikiem TIFF.
- Prosić o kawałki najwyżej 3 × 3 km. Jedno zapytanie o 12 × 12 km wróciło z pustą wschodnią połową.
- Kilka punktów wraca jako 0 („brak danych”). `FillMissingHeights` daje im średnią z sąsiadów.

## Narzędzie

```
terrain_tool join WYJŚCIE.heights ODSTĘP KOLUMNY WIERSZE FOLDER PREFIKS    łączy kafle PREFIKS_c<kolumna>_r<wiersz>.tif, łata dziurki
terrain_tool rescale WEJŚCIE.heights WYJŚCIE.heights POZIOMO PIONOWO      prawdziwy teren -> teren gry
terrain_tool preview WEJŚCIE.heights WYJŚCIE.png                          obraz rzeźby terenu, oświetlony z północnego zachodu
terrain_tool info WEJŚCIE.heights                                         rozmiar, zakres wysokości, jak stromy jest teren
terrain_tool generate PODRĘCZNY PODGLĄDY [NAZWA=WARTOŚĆ ...]              generuje (albo wczytuje) teren, zapisuje obrazy jego i map
```

`generate` przyjmuje dowolne ustawienie `TerrainGeneratorSettings` jako NAZWA=WARTOŚĆ (np. `seed=7 size=2048 erosion_steps=100`) i zapisuje `relief.png` (oświetlony z północnego zachodu, jeziora na niebiesko), `detail.png` (środkowe 1024 × 1024 m, piksel na metr), `flow.png`, `moisture.png`, `sediment.png` i `rock.png`. Tak się stroi generator: teren 2 × 2 km zajmuje 20 sekund.

`rescale` z POZIOMO 0.5 skraca wszystkie odległości o połowę i zostawia wysokości: kwadrat 12 km staje się 6 km, a każde zbocze robi się bardziej strome (45 stopni staje się 63). Najniższy punkt trafia na wysokość 0.

## Pierwsza prawdziwa mapa: Batorz

W `poligon-assets/terrain/batorz` (poza repozytorium):

- `tiles/`: 16 kafli po 3 × 3 km, wysokość co 2 m, 137 MB, pobrane 2026-10-14. Kwadrat 12 × 12 km wokół 50.861443 N, 22.496135 E (EPSG:2180: wschód 739948 do 751948, północ 332549 do 344549).
- `batorz_real_2m.heights`: połączone kafle, 6000 × 6000 punktów; wysokości 218,8 do 320,7 m; 21 załatanych dziurek.
- `tiles_1m/` i `batorz_6km_1m.heights`: środkowe 6 × 6 km z wysokością co 1 m (4 kafle, 137 MB, pobrane 2026-10-14; 6000 × 6000 punktów, wysokości 226,0 do 316,1 m, bez dziurek). **Tej mapy używa gra, w skali 1:1.**
- `batorz_6km_2m.heights`: ten sam obszar wycięty z danych co 2 m; zapas.
- `batorz_preview.png`: rzeźba terenu: dolina rzeki przez środek i wąwozy lessowe po obu stronach.

Jak stromo jest, z 40000 miejsc:

| | poniżej 10 stopni | 10 do 25 | 25 do 45 | powyżej 45 |
|---|---|---|---|---|
| rzeczywistość | 83,6% | 13,9% | 2,4% | 0,1% |
| ściśnięte do 0,7 | 72,3% | 22,6% | 4,4% | 0,7% |
| ściśnięte do 0,5 | 59,4% | 30,6% | 7,8% | 2,1% |

Chodząca postać wchodzi na zbocza do 50 stopni, więc przy 0,5 około jednej pięćdziesiątej terenu to ściany: zbocza wąwozów.
