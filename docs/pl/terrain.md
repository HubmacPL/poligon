# Teren

Kształt terenu jako mapa wysokości, skąd się bierze i jak prawdziwe dane wysokościowe stają się terenem gry.

Nagłówki: `poligon/height_map.h`. Narzędzie: `terrain_tool`. Test: `tests/height_map_test.cpp`.

**Stan: strona danych.** Mapy wysokości można tworzyć, czytać, łączyć, naprawiać, przeskalowywać i oglądać. Rysowanie terenu, teksturowanie, kolizje i sadzenie na nim to następne kroki.

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
- `batorz_game_1m.heights`: ściśnięte o połowę, 6 × 6 km z punktem co metr, wysokości 0 do 101,9 m.
- `batorz_preview.png`: rzeźba terenu: dolina rzeki przez środek i wąwozy lessowe po obu stronach.

Jak stromo jest, z 40000 miejsc:

| | poniżej 10 stopni | 10 do 25 | 25 do 45 | powyżej 45 |
|---|---|---|---|---|
| rzeczywistość | 83,6% | 13,9% | 2,4% | 0,1% |
| ściśnięte do 0,7 | 72,3% | 22,6% | 4,4% | 0,7% |
| ściśnięte do 0,5 | 59,4% | 30,6% | 7,8% | 2,1% |

Chodząca postać wchodzi na zbocza do 50 stopni, więc przy 0,5 około jednej pięćdziesiątej terenu to ściany: zbocza wąwozów.
