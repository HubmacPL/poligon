# Teren

Kształt terenu jako mapa wysokości, skąd się bierze i jak prawdziwe dane wysokościowe stają się terenem gry.

Nagłówki: `poligon/height_map.h`, `poligon/terrain.h`. Narzędzie: `terrain_tool`. Przykład: `examples/terrain`. Testy: `tests/height_map_test.cpp`, `tests/terrain_render_test.cpp`.

**Stan:** teren jest rysowany po horyzont z poziomami szczegółowości, teksturowany dwiema mieszanymi teksturami podłoża i kolorem stromych zboczy, oświetlany i cieniowany jak wszystko inne, a fizyka się z nim zderza. Jeszcze nie: malowana mapa tego, gdzie leży które podłoże, więcej niż dwie tekstury, wygładzanie zmiany między rozmiarami kafli, rośliny rozmieszczane regułami.

## Rysowanie

```cpp
poligon::TerrainConfig config;
config.origin = {-3000.0f, -280.0f, -3000.0f};       // położenie pierwszego punktu mapy w świecie
config.material.layers[0] = {grass_color, grass_normal, 3.0f};   // tekstura, mapa normalnych, metry na powtórzenie
config.material.layers[1] = {litter_color, litter_normal, 2.5f};
poligon::Terrain terrain(renderer, height_map, config);
// co klatkę, między BeginFrame a EndFrame:
terrain.Draw(camera);
```

- Teren nie jest jedną siatką. Jest rysowany jako kwadratowe **kafle** z drzewa kwadratów: cała mapa to jeden kwadrat; kwadrat bliższy kamerze niż jego rozmiar razy `TerrainConfig::detail` dzieli się na cztery, aż do najmniejszego rozmiaru (64 oczka mapy wysokości). Pod kamerą jest wierzchołek na każdy punkt mapy; zbocze odległe o kilometry to kilka zgrubnych kwadratów. Mapa 6 × 6 km z punktem co metr to 36 milionów punktów, a rysuje się ją kilkuset kaflami.
- Każdy kafel to ta sama płaska siatka 64 × 64 oczek. Shader wierzchołków (`terrain.vert.hlsl`) podnosi każdy wierzchołek na wysokość terenu, odczytaną z obrazu, który trzyma całą mapę na karcie graficznej (16 bitów na punkt: 72 MB dla 6000 × 6000). Z mapy nigdy nie buduje się siatki.
- Tam, gdzie drobny kafel styka się ze zgrubnym, ich krawędzie nie pasują dokładnie. Każdy kafel ma **fartuch**, zwisający brzeg, który zasłania szczeliny.
- Kierunek, w który zwrócony jest grunt, jest wyliczany dla każdego piksela z obrazu wysokości (`terrain.frag.hlsl`), więc dalekie zgrubne kafle są cieniowane równie dokładnie jak bliskie.
- Wygląd (`TerrainMaterial`): dwie tekstury podłoża mieszane w duże nieregularne plamy z szumu; każda jest czytana we własnym rozmiarze i siedem razy większa, a większy odczyt przejmuje z odległością, co ukrywa powtarzanie; zbocza między `steep_start_degrees` a `steep_end_degrees` przechodzą w `steep_color`.
- Oświetlenie jest wspólne z siatkami (`lighting.hlsli`): słońce, cienie we wszystkich czterech kaskadach, światło nieba, przyciemnianie zagłębień, mgła.
- `Terrain::GetHeight(x, z)`, `GetNormal`, `GetSlopeDegrees` odpowiadają dla pozycji w świecie; `GetStats()` liczy kafle według rozmiaru.
- Pod `Terrain` jest `Renderer::CreateTerrain` / `DrawTerrain(terrain, material, patches, count)`; wszystkie kafle jednego wywołania kosztują jedno polecenie rysowania na przebieg, a każdy jest odrzucany osobno.

## Fizyka

- `PhysicsWorld::AddHeightField(heights, size, spacing, origin)` robi grunt z kwadratowej siatki wysokości. Chodząca postać, bryły i promienie traktują go jak każdy inny grunt.
- Przy dużej mapie fizyce daje się tylko część, w której coś się dzieje (`CropHeightMap`): `forest_scene` używa 1 × 1 km wokół startu gracza, z płotami na brzegu.

## W forest_scene

`forest_scene` wczytuje `poligon-assets/terrain/batorz/batorz_6km_1m.heights` (wzgórza z szumu, jeśli pliku nie ma). Początek układu świata to szczyt najbliższego wzgórza na zachód od wsi, 70 m nad doliną; gracz startuje tam, patrząc na wschód. Około 21000 roślin i kamieni stoi w promieniu 250 m od startu, żadna na zboczu powyżej 33 stopni, z polaną na szczycie i otwartym klinem na wschód dla widoku. Zmierzone na GTX 980 z oknem: 16,6 ms na klatkę w lesie i ze skraju wzgórza, około 20 ms (chwilami poniżej 60 klatek na sekundę) na starcie. Drzewa rzucają cienie do 65 m.

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
```

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
