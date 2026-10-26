# Assety i poziomy

Jak gra nazywa, przygotowuje i wczytuje swoje tekstury i modele, jak z modeli powstają rośliny, oraz plik poziomu.

Nagłówki: `poligon/assets/assets.h` (wczytywanie), `poligon/assets/cooking.h` (lista, wypiekanie, pakowanie; strona narzędzi), `poligon/core/texture_compression.h`, `poligon/world/plants.h`, `poligon/world/level.h`. Narzędzie: `asset_cooker`. Przykład: `examples/assets/assets`. Test: `tests/assets/assets_test.cpp`.

**Stan (ustalone 2026-10-14, zbudowane od 2026-10-15 do 2026-10-17, w całości):** jedna lista nazywa każdy asset z jego źródłem i ustawieniami; wypiekacz zamienia źródła w szybko wczytujące się pliki ze skompresowanymi teksturami; gra wczytuje po nazwie przez jedną bibliotekę, z folderu wypieczonego albo z archiwum; rośliny z modeli robi silnik; plik poziomu trzyma ustawienia terenu, start gracza, rzeczy postawione ręcznie i reguły sadzenia ([vegetation.md](vegetation.md)). Klipy animacji są wypiekane gotowe, jako archiwum ozz-animation. Folder danych został podzielony na połówki źródłową i wypieczoną 2026-10-16 (zob. niżej).

## Folder danych

Dane gry leżą poza repozytorium, w `poligon-assets` obok folderu projektu, w dwóch połówkach:

- `source/`: to, na co wskazują linie `source` listy assetów, względem niego. Pobrane pliki (`polyhaven/`, `ambientcg/`, `sketchfab/`, `khronos/`), pliki zrobione naszymi narzędziami (`generated/` drzewa, `lods/` prostsze poziomy, `impostors/`, `prepared/` naprawione tekstury) i dane wysokości (`terrain/`).
- `cooked/`: to, co zapisuje wypiekacz i wczytuje gra. Można go w każdej chwili usunąć; następne uruchomienie wypiecze go od nowa.

Obok są foldery robocze, które nie są danymi gry: `previews/` i `thumbnails/` (obrazki do oceny), `tools/` (EZ-Tree i three.js dla make_tree.mjs), `superseded/` (stare wersje zachowane do porównań). Wygenerowany teren `forest_scene` to pamięć podręczna w `build/terrain_cache`.

## Lista assetów

Plik tekstowy w repozytorium, w stylu pliku ustawień obrazu (przykład: `tools/forest_scene/assets.txt`):

```
poligon-assets 1

texture ground/forest_floor_color
source polyhaven/textures/forrest_ground_01/forrest_ground_01_diff_2k.jpg
format bc1

model trees/pine
source generated/pine/pine.gltf
lod lods/pine/lod1/pine.gltf
lod lods/pine/lod2/pine.gltf
impostor impostors/pine.impostor
```

- Wiersz `texture NAZWA` albo `model NAZWA` zaczyna asset; wiersze po nim go ustawiają. Nazwy składają się z liter, cyfr, `_ - . /` i nie mogą wyjść poza folder; ścieżki są względne wobec folderu źródeł.
- Ustawienia tekstury: `source`, `format` (`bc1` kolor, `bc3` kolor z przezroczystością, `bc4` jeden kanał, `bc5` mapa normalnych, `rgba8` bez kompresji), `color 0|1` (kolory sRGB albo dane), `cutout` (próg alfy, dla mipmap zachowujących pokrycie), `max_size` (większe obrazy są połowione, aż się zmieszczą).
- Ustawienia modelu: `source` (dowolny format czytany przez Assimp), `lod` (prostsze poziomy, od najdokładniejszego; ich węzły są dopasowywane do pełnego modelu po nazwie), `impostor` (plik wypalony przez `impostor_baker`), `max_size` (największy rozmiar tekstur modelu).
- Nieznane słowo, brak źródła, nazwa użyta dwa razy albo ustawienie złego rodzaju to błąd z numerem wiersza (`LoadAssetList`, `ParseAssetList`).

## Wypiekanie

- `CookAssets(list, list_path, source_folder, cooked_folder)` zapisuje jeden plik na asset, nazwany od niego (`trees/pine` staje się `trees/pine.pmodel`, `ground/forest_floor_color` staje się `ground/forest_floor_color.ptex`). Asset jest wypiekany ponownie tylko wtedy, gdy jego plik nie istnieje albo jest starszy od listy lub któregoś źródła, albo gdy zmienił się sam wypiekacz (`kCookVersion` w `cook_version.txt`). Nieudany asset jest zgłaszany, a reszta idzie dalej. `forest_scene` wypieka przy starcie: 31 assetów w 15 s za pierwszym razem, potem nic.
- `asset_cooker LISTA FOLDER_ŹRÓDEŁ FOLDER_WYPIECZONY [--level POZIOM [--terrain-cache FOLDER]] [--pack ARCHIWUM]` robi to samo z wiersza poleceń; `--level` wypieka też strumieniowany świat, który wskazuje poziom (`CookLevelWorld`, patrz [streaming.md](streaming.md)): ląd jest generowany (trzymany w pamięci podręcznej terenu), wagi jego warstw podłoża robione z bloków `ground` poziomu, a całość cięta na pliki łat `NAZWA/X_Z.ppatch` z opisem `NAZWA.pworld`. Wypiekany jest od nowa tylko wtedy, gdy zmienią się ustawienia generatora, warstwy podłoża albo rozmiar łaty.
- Tekstury (`.ptex`, `SaveCompressedImage`): kompresja blokowa przez stb_dxt (domena publiczna / MIT) z wszystkimi mipmapami, liczonymi tak jak w `Renderer::CreateTexture` (z zachowaniem pokrycia dla wycinanych alfą); łańcuchy skompresowane kończą się na 4 × 4, bo Direct3D 12 nie umie kopiować ich mniejszych poziomów. Rozmiary muszą być wielokrotnością 4.
- Modele (`.pmodel`): siatki, instancje, materiały, szkielet, klipy animacji zbudowane przez ozz-animation i zapisane jako jego archiwum (`AnimationSet::SaveArchive`; gra wczytuje je gotowe do odtwarzania, bez budowania, jako `LoadedModel::animations`, a `model.animations` zostaje puste), wartości każdego materiału (z emisją) i jego cztery obrazy skompresowane (kolor BC1 albo BC3 dla wycinanych; mapa normalnych BC5; okluzja-szorstkość-metaliczność BC1; emisja BC1), siatki prostszych poziomów i dwa obrazy impostora (BC3). Mapy normalnych trzymają tylko x i y; shadery odtwarzają z (`mesh.frag.hlsl`, `terrain.frag.hlsl`), co działa też dla map nieskompresowanych.
- Rozmiary: 31 assetów `forest_scene` zajmuje po wypieczeniu 225 MB. Skompresowane warstwy podłoża znów pozwalają na tekstury warstw 2048 pikseli, a klatka przyspieszyła (15,3 do 13,9 ms na starcie w high), bo karta czyta mniej pamięci.
- `PackAssets(cooked_folder, archive)` pakuje folder w jeden plik dla wydanej gry: `POLIPAK1`, tabela (64-bitowy skrót FNV-1a nazwy, przesunięcie, rozmiar), potem pliki. Nazwy nie są zapisywane i nic nie jest szyfrowane (wybór autora z 2026-10-14: odstrasza przypadkowe ręce, jak archiwa theHunter). Kopiuje pliki jeden po drugim, a `AssetLibrary` przy otwieraniu archiwum czyta tylko tabelę, a każdy plik dopiero, gdy jest potrzebny (od 2026-10-26; wcześniej całe archiwum siedziało w pamięci, co przy setkach megabajtów łat strumieniowanego świata odpada). Odczyty mogą przychodzić z kilku wątków naraz.

## Wczytywanie po nazwie

```cpp
poligon::AssetLibrary library;
library.Open("../poligon-assets/cooked", error);  // folder wypieczony albo archiwum
const poligon::LoadedModel* pine = library.LoadModel(renderer, "trees/pine", error);
poligon::TextureHandle floor = library.LoadTexture(renderer, "ground/forest_floor_color", error);
const poligon::CompressedImage* layer = library.LoadImage("ground/litter_color", error);  // np. do tablic tekstur
```

- Drugie wczytanie tej samej nazwy daje ten sam obiekt. Brakujący albo uszkodzony asset nic nie zwraca i ustawia `error`.
- `LoadedModel` trzyma dane modelu (wierzchołki dla fizyki i rozstawiania, szkielet, klipy), jego `RenderModel`, siatki prostszych poziomów (także ze szkieletem, pozowane szkieletem pełnego modelu) i impostor. `MakeMeshLodGroup(loaded, instance, distances)` zamienia jedną siatkę i jej prostsze poziomy w `MeshLodGroup` dla `Renderer::DrawMesh` lub `DrawSkinnedMesh` (patrz rendering.md).
- `Renderer::CreateTexture(const CompressedImage&)` i `CreateTextureArray(const CompressedImage*, count)` przyjmują wypieczone obrazy tak, jak są; `CreateImpostor` ma wersję dla skompresowanych obrazów.

## Rośliny z modeli

`MakePlantKind(model, settings, vegetation)` (`poligon/world/plants.h`) zamienia wczytany model w rodzaj rośliny, kamienia albo martwego drewna i dodaje jego odmiany do `Vegetation`:

- `split_variants`: plik z rzędem różnych roślin daje jedną odmianę na grupę siatek, których pudełka nakładają się widziane z góry (pień i liście jednego drzewa zostają razem); każda jest przesuwana tak, by środek jej podstawy był w początku układu, chyba że `keep_origin`.
- Poziomy: pełny model, każdy prostszy poziom (siatki węzłów nazwanych jak w odmianie), potem impostor, w odległościach `level_distances`; poziom, którego asset nie ma, jest pomijany, a poprzedni sięga jego odległości; bez impostora ostatni poziom z siatką sięga `distance_without_impostor`.
- `wind_bend` i `wind_flutter` trafiają do materiałów (drżenie tylko do wycinanych alfą); `shadow_distance` i `shadow_level_bias` do gatunku.
- `AddPlantCollider(physics, kind, variant, foot, turn, scale)` dodaje to, co zatrzymuje gracza: `PlantCollider::Trunk` (stojący walec o promieniu `trunk_radius`, najwyżej 8 m wysokości), `Block` (pudełko trochę mniejsze niż odmiana) albo nic.

To przeniosło z `forest_scene` grupowanie odmian, szukanie prostszych poziomów i impostorów po folderach oraz wybór kolizji; pętla sadzenia stała się regułami sadzenia (`poligon/world/plant_rules.h`).

## Plik poziomu

```
poligon-level 1

terrain.seed 1               # dowolne TerrainGeneratorSettings po nazwie
terrain.relief 110
render_settings look.txt     # opcjonalnie, względem tego pliku
player_start lake            # albo: player_start X Z OBRÓT (metry na mapie, stopnie)
object rocks/boulder 3538 5112 40 2.2    # asset, x i z na mapie, obrót, skala
world worlds/forest          # opcjonalnie: ląd jest strumieniowany z tego wypieczonego świata
world.patch_size 512
world.background_spacing 64

ground ground/forest_floor   # warstwa podłoża: jej tekstury NAZWA_color i NAZWA_normal
tile 3                       # co ile metrów się powtarzają
strength 0.35
moisture 0 0.45 0.15         # warunki na mapach, jak w regułach sadzenia
patch 45 0.45
```

- `LoadLevel`, `ParseLevel`, `SaveLevel` (`poligon/world/level.h`). Nieznane słowa i złe wartości to błędy z numerem wiersza; wiersze, których pierwsze słowo bierze inny czytelnik (np. reguły sadzenia), trafiają do `Level::other_lines`.
- `player_start lake` używa `FindLakeViewpoint` (`poligon/world/terrain_generator.h`): łagodny teren 150 do 500 m od największego jeziora, jak najwyżej nad nim, z jego środkiem w widoku.
- `SetTerrainGeneratorSetting` / `GetTerrainGeneratorSettings` czytają i zapisują ustawienia generatora po nazwie; `terrain_tool generate` używa tych samych nazw.
- Warstwy podłoża (`ParseGroundLayers`, `poligon/world/terrain.h`) to bloki jak reguły sadzenia, najwyżej osiem, numerowane w kolejności pliku; oba czytniki omijają swoje bloki nawzajem. Wybierają, gdzie każda tekstura podłoża pokrywa ląd (`TerrainLayerRule`); strumieniowany świat wypieka ich wagi w swoje łaty.
- `forest_scene` czyta `tools/forest_scene/forest.level`: domyślny teren, start nad jeziorem, trzy głazy postawione ręcznie, 7 warstw podłoża, 18 reguł sadzenia i strumieniowany świat, w który jest wypiekany (`worlds/forest`).
