# Roślinność

Rysowanie roślin, kamieni i innych rzeczy, które stoją w wielkich ilościach: instancjonowanie, poziomy szczegółowości, komórki.

Nagłówki: `poligon/vegetation.h`, `poligon/renderer.h` (`DrawMeshInstances`, `BakeImpostor`, `DrawImpostorInstances`), `poligon/impostor.h`. Narzędzie: `impostor_baker`. Przykłady: `examples/vegetation`, `examples/impostor`. Testy: `tests/vegetation_render_test.cpp`, `tests/impostor_render_test.cpp`, część o instancjonowaniu w `tests/scene_render_test.cpp`.

**Stan:** instancjonowanie, poziomy szczegółowości wybierane według odległości, komórki, osobne traktowanie cieni, impostory jako najdalszy poziom, płynne przejścia między poziomami, narzędzia do prostszych poziomów drzew i do impostorów. Jeszcze nie: wiatr, rozmieszczanie roślin regułami terenu, rośliny tworzone w locie wokół gracza.

## Pojęcia

- **Instancjonowanie**: jedna siatka rysowana wiele razy jednym poleceniem rysowania, każda kopia z własnym przekształceniem. Las tysiąca sosen jednego rodzaju kosztuje garść poleceń, a nie tysiące.
- **Poziom szczegółowości** (LOD): ta sama roślina jako kilka siatek, od pełnej z bliska po proste z daleka.
- **Komórka**: kwadratowy kawałek świata (domyślnie 32 m), który trzyma swoje rośliny. Całe komórki można pominąć albo przekazać bez patrzenia na pojedyncze rośliny.
- **Kartka liści**: mały płaski kawałek ze zdjęciem gałązki, wycinany przezroczystością. Korony drzew składają się z tysięcy takich kartek.
- **Impostor**: zastępca modelu widzianego z daleka: jedna płaska karta zawsze zwrócona do kamery, pokazująca zdjęcia modelu zrobione wcześniej z wielu kierunków.
- **Przenikanie kropkowane** (dithered fade): dwa poziomy rośliny rysowane naraz, każdy zostawia piksele, które drugi pomija, według stałego wzoru, więc jeden rozpływa się w drugim zamiast przeskakiwać.

## Instancjonowanie w rendererze

- `Renderer::DrawMeshInstances(mesh, material, transforms, count, options)` rysuje `count` kopii. Przekształcenia są kopiowane w chwili wywołania.
- Każda kopia jest odrzucana osobno, względem widoku kamery i względem każdej kaskady cieni (kulą wokół siatki).
- `InstanceOptions::draw_in_view = false` sprawia, że kopie tylko rzucają cienie; `shadow_cascades` to maska bitowa kaskad, do których są rysowane (0 = bez cienia). W ten sposób cienie dostają prostsze siatki niż obraz.
- `ComputeShadowCascadeEnds(near_plane, shadow_distance)` mówi, gdzie kończy się każda kaskada.
- `FrameStats` liczy `instances_submitted`, `instances_drawn`, `shadow_instances` i `instance_draw_calls`.
- Pod spodem: przekształcenia, które przeszły odrzucanie, trafiają ze wszystkich partii do jednego bufora wysyłanego raz na klatkę; `mesh_instanced.vert.hlsl` czyta przekształcenie po numerze instancji. Nie dla siatek ze skinningiem.

## System roślinności

```cpp
poligon::VegetationSpecies pine;
pine.levels = {{full_parts, 28.0f}, {medium_parts, 70.0f}, {low_parts, 450.0f}};  // części, używane do tej odległości
pine.shadow_distance = 90.0f;   // dalej bez cienia
pine.shadow_level_bias = 1;     // cienie używają poziomu o jeden prostszego niż obraz

poligon::Vegetation vegetation;                       // komórki 32 m
const int species = vegetation.AddSpecies(pine);
vegetation.AddInstance(species, transform);           // raz na roślinę
// co klatkę, między BeginFrame a EndFrame:
vegetation.Draw(renderer, camera);
```

- **Gatunek** to lista poziomów, od najbliższego. Każdy poziom ma części (siatka, materiał, przekształcenie względem stopy rośliny) i odległość, do której jest używany. Za ostatnim poziomem roślina nie jest rysowana.
- Odległości mierzy się po ziemi od kamery.
- `Draw` przechodzi komórki w stałej kolejności. Komórka, w której nic nie jest w zasięgu, jest pomijana; komórka leżąca w całości w jednym poziomie jest przekazywana w jednym kawałku; tylko komórki na granicy poziomów są przeglądane roślina po roślinie. Potem każdy gatunek i poziom staje się jednym wywołaniem `DrawMeshInstances` na część.
- `SetDistanceScale(detail, shadow)` mnoży wszystkie odległości poziomów i odległości cieni, do strojenia suwakami.
- `GetStats()`: rośliny na poziom, pominięte komórki, partie.
- Siatki i tekstury należą do wywołującego; system trzyma tylko uchwyty i przekształcenia.
- `VegetationSpecies::fade_width` (domyślnie 0,1): część odległości każdego poziomu, na której roślina przechodzi w następny poziom, a za ostatnim w nic. Roślina w 66 m poziomu kończącego się na 70 m jest rysowana na obu poziomach. Przenikanie jedzie w ostatniej wartości macierzy każdej instancji (`[3][3]`, 1 w zwykłym przekształceniu): między 0 a 1 zostaje taka część pikseli kopii, między -1 a 0 taka część jest pomijana. Każdy shader wierzchołków przekazuje ją dalej, a shadery pikseli przebiegu sceny, cieni i głębi ją stosują (`KeptByFade` w `common.hlsli`), więc cienie i mgła idą za nią. Bez czasowego wygładzania krawędzi wzór może być widoczny jako drobne ziarno na nielicznych roślinach w trakcie przejścia.
- Poziom z ustawionym `impostor` jest rysowany jako ten impostor zamiast swoich części (`DrawImpostorInstances`). Impostory nie rzucają cieni; jeśli poziomem cienia byłby impostor, cień rzuca ostatni poziom z siatkami.
- Cień rośliny też nie urywa się na `shadow_distance` w jednym kroku: na ostatniej części `fade_width` tej odległości rozpływa się tym samym wzorem. Rzucające cień powinny sięgać dalej niż pokazywane cienie (`forest_scene`: 110 m dla drzew przy zasięgu cieni 65 m), bo drzewo za zasięgiem cieni nadal rzuca cień do środka; inaczej cienie pojawiają się dopiero, gdy ich drzewo wejdzie w zasięg.

## Pokrywa ziemi

Trawa, paprocie, pokrzywy, ściółka, drobne rośliny i kamyki: za dużo, żeby je przechowywać dla mapy, więc powstają w locie wokół kamery (`poligon/ground_cover.h`). Przykład: `examples/ground_cover`; test: `tests/ground_cover_test.cpp`.

```cpp
poligon::GroundCoverLayer grass;
grass.species = grass_species;          // części na poziom, jak w Vegetation; ostatnia odległość poniżej promienia
grass.density = 1.2f;                   // na metr kwadratowy tam, gdzie wolno w pełni
grass.max_slope_degrees = 40.0f;
grass.patch_threshold = 0.3f;           // rośnie miękkimi plamami; 0 = równo wszędzie
grass.align_to_ground = true;           // pochyla się ze zboczem
poligon::GroundCover cover(terrain, 90.0f);   // rośliny istnieją w promieniu 90 m od kamery, w komórkach 16 m
cover.AddLayer(grass);
// co klatkę:
cover.Update(camera.position);          // buduje komórki, które weszły w zasięg, porzuca te, które wyszły
cover.Draw(renderer, camera);           // między BeginFrame a EndFrame
```

- Rośliny komórki wynikają z reguł (nachylenie, zakres wysokości, szum plam) i skrótu położenia komórki oraz ziarna warstwy, więc ta sama komórka zawsze ma te same rośliny, niezależnie od kolejności budowania. Wszystkie liczby losowe rośliny są losowane przed regułami, więc roślina odrzucona przez regułę nie przesuwa następnych.
- `Update` nic nie robi, dopóki kamera zostaje w jednej komórce; przejście do nowej buduje komórki, które weszły w zasięg (kilka, każda w ułamku milisekundy), i przekazuje wszystkie żywe rośliny do `Vegetation` w środku, które rysuje je z poziomami, przenikaniem, wiatrem i cieniami jak wszystko inne.
- `sink_meters` zakopuje kamyki odrobinę; `scale_min`/`scale_max` różnicują wielkość; `CollectInstances` wylicza, co istnieje (testy, narzędzia).
- W `forest_scene`: trawa (3 na m², plamami, co trzeci z 21 wariantów kępek, cienie tylko do 8 m), paprocie, pokrzywy, niskie krzewy i małe omszałe kamienie rosną w promieniu 90 m od gracza na całych 6 × 6 km, po jednej warstwie na wariant modelu; nic z tego nie jest już sadzone. Drzewa, duże krzewy, głazy, pniaki i pnie zostają posadzone (`Vegetation`), bo mają kolizje i cienie z daleka. Zmierzone (release, bez okna, 200 klatek, ten sam bieg z promieniem skróconym do 1 m jako odniesienie): 1,1 do 1,6 ms na klatkę za około 16000 dodatkowych instancji w zasięgu, z czego 4000 do 4500 w kadrze. Każda warstwa kosztuje jedno polecenie rysowania na przebieg, więc rodzaj z wieloma wariantami powinien używać kilku z nich albo mieć warianty połączone w kępy.
- Jeszcze nie: malowana mapa podłoża mówiąca, gdzie która warstwa rośnie (dziś tylko nachylenie, wysokość i szum), mapa wygniatania dla ścieżek w trawie, i płynne pojawianie się komórki, gdy wchodzi bliżej niż najdalsza odległość warstwy.

## Impostory

```cpp
poligon::Impostor pictures;
renderer.BakeImpostor(parts, part_count, 8, 128, pictures);   // 8 x 8 kierunków, po 128 px
poligon::SaveImpostor("pine.impostor", pictures);           // albo LoadImpostor
const poligon::ImpostorHandle pine_card = renderer.CreateImpostor(pictures);
pine.levels.push_back({{}, 1700.0f});                       // poziom rysowany do 1700 m ...
pine.levels.back().impostor = pine_card;                    // ... jako impostor
```

- `BakeImpostor` fotografuje model naszym własnym rendererem, bez perspektywy, z 8 × 8 kierunków rozłożonych po półkuli nad nim (układ „półoktaedryczny”: środek siatki patrzy prosto w dół, jej brzeg od horyzontu). Każde zdjęcie zawiera to, czym jest powierzchnia, a nie jak jest oświetlona: kolor z pokryciem i kierunek, w który zwrócony jest każdy piksel. W grze impostor jest oświetlany przez `lighting.hlsli` jak wszystko inne, więc idzie za słońcem, niebem i mgłą.
- Rysowanie: jedna karta na instancję, wielkości modelu, zwrócona do kamery. Dla każdego piksela czytane są cztery klatki najbliższe kierunkowi patrzenia (każda w punkcie, w którym piksel karty pada na płaszczyznę zdjęcia tej klatki) i mieszane według tego, jak blisko jest każdy kierunek. Z boku i ze wzgórza obraz zmienia się płynnie, gdy kamera się porusza.
- Instancja może być obrócona wokół pionu i równo przeskalowana; wypalone normalne obracają się razem z nią.
- Pliki: `NAZWA.impostor` (klatki, środek, promień jako tekst), `NAZWA_color.png`, `NAZWA_normal.png` (`poligon/impostor.h`).
- `impostor_baker MODEL WYJŚCIE.impostor [--frames N] [--size PIKSELE] [--keep-origin]` wypala jeden plik modelu. Stopa modelu trafia do początku układu, tak jak w `forest_scene`; `--keep-origin` zostawia ją w bok tam, gdzie ma ją plik (generowane drzewa mają tam pień).
- Jeden impostor z 8 × 8 klatek po 128 px to dwa obrazy 1024 × 1024, 11 MB na karcie graficznej z mipmapami; kompresja tekstur zmniejszy to później.
- Ograniczenia: karta jest płaska, więc jej obrys nie zmienia się, gdy kamera przesunie się tylko trochę bliżej albo w bok, i nie zasłania ziemi za sobą tak jak pełna korona; przeznaczona na 100 m i dalej. Kula modelu jest pokazana w pełnym rozmiarze w każdej klatce, więc wysokie, smukłe drzewo wypełnia tylko część klatki.

## W forest_scene

Sosna, świerk, brzoza i dąb mają impostory w `poligon-assets/impostors` (wypalone przez `impostor_baker ... --keep-origin`). Ich poziomy: pełna siatka do 28 m, średnia do 70 m, niska do 150 m, impostor do 1700 m, każdy z przejściem na ostatniej dziesiątej części. Duże drzewa stoją teraz do 1200 m od startu (około 55000; podszyt zostaje w promieniu 250 m, fizyka 500 m), a otwarty klin na wschód od startu kończy się na 400 m, więc daleki las widać w panoramie. Zmierzone (bez okna, release, 150 klatek): 7,7 ms ze skraju wzgórza z lasem po horyzont (9,9 ms z siatkami do 450 m i niczym dalej), 13,1 ms w lesie.

## Drzewa forest_scene

Sosna, świerk, brzoza, dąb, młoda sosna i młody świerk, wszystkie generowane przez `tools/assets/make_tree.mjs` z przepisów w `tools/assets/trees/` (zob. [rendering.md](rendering.md)), każde z trzema poziomami zrobionymi z tego samego drzewa. Gałązki: Poly Haven `fir_tree_01` (świerk) i `pine_tree_01` (sosna), gałązki osiki i dębu z EZ-Tree; kora: Poly Haven `fir_tree_01` (świerk), ambientCG `Bark014` (sosna), kora brzozy i dębu z EZ-Tree; wszystko CC0 albo dostarczane z EZ-Tree (MIT).

Trójkąty na poziom:

| | pełny | średni | niski |
|---|---|---|---|
| sosna 24 m | 20600 | 7900 | 2750 |
| świerk 20 m | 12000 | 4400 | 1900 |
| brzoza 17 m | 13400 | 5300 | 1900 |
| dąb 18 m | 13600 | 5500 | 1900 |
| młoda sosna 5 m | 12600 | 5000 | 1800 |
| młody świerk 4 m | 6500 | 3700 | 640 |

Skany z Poly Haven, które zastąpiły, miały od 33000 do 193000 trójkątów w pełnym detalu i do 15500 na najniższym poziomie. Mapy szorstkości gałązek z Poly Haven nie są używane: z nimi igły wyglądały na mokre i odbijały niebieskie niebo.

## Robienie uproszczonych poziomów pobranego drzewa

Dla modeli niegenerowanych przez `make_tree.mjs`: `tools/assets/simplify_model.py` ze słowem `foliage`:

```
blender -b --factory-startup -P tools/assets/simplify_model.py -- WEJŚCIE WYJŚCIE.gltf 0.1 foliage
```

- Pień i gałęzie są upraszczane przez zwijanie trójkątów, jak dotąd.
- Liście są traktowane inaczej. Zwijanie kartek liści przerzedza koronę, aż drzewo wygląda na łyse. Zamiast tego usuwana jest część kartek (dla tego samego pliku zawsze te same), a pozostałe są powiększane, żeby korona zakrywała tę samą powierzchnię. Przy jednej dziesiątej trójkątów sosna zachowuje sylwetkę.
- Siatka liczy się jako liście, gdy wszystkie jej materiały biorą przezroczystość z tekstury. Modele, w których liście i kora dzielą jedną siatkę, są zwijane w całości (takie były jacaranda i drzewo wyspowe z Poly Haven).
- Uproszczone poziomy używają materiałów pełnego modelu, dopasowanych po nazwie; `LoadModelFromFile(path, model, error, false)` czyta taki plik bez tekstur.
- Bez `foliage` skrypt najpierw zszywa narożniki leżące w tym samym miejscu: niektóre skany (`boulder_01` z Poly Haven) zapisują każdy trójkąt z własnymi narożnikami i zwijanie zatrzymuje się na nich za wcześnie.
- W `forest_scene` głaz, zestaw kamieni, pniak, przewrócony pień (do jednej dwunastej i jednej pięćdziesiątej trójkątów) oraz krzew `shrub_02` (z `foliage`) mają dwa prostsze poziomy w `poligon-assets/lods`, używane od 15 m i 45 m i rysowane do 150 m. Plik z kilkoma wariantami (zestaw kamieni ma sześć) jest łączony ze swoimi prostszymi plikami po nazwach węzłów.

## Ile to kosztuje: pomiary

`forest_scene`, około 3200 roślin i kamieni na 140 × 140 m, GTX 980, 1280 × 720, wszystkie efekty włączone, budowa release, czas klatki razem z pracą karty graficznej:

| | ms na klatkę |
|---|---|
| przed instancjonowaniem i poziomami szczegółowości | 34 |
| z nimi, obrazy cieni 2048 | 23 do 25 |
| obrazy cieni 1536 | 20 |
| obrazy cieni 1024 (tego używa `forest_scene`) | 16 |
| cienie wyłączone | 6,6 |

- Cienie są teraz zdecydowanie największym kosztem: każda kartka liści jest rysowana do czterech obrazów cieni, a koszt zależy od liczby pikseli cienia, nie od liczby trójkątów. Prostsze siatki dla cieni niewiele dają, bo powiększone kartki zakrywają tę samą powierzchnię.
- Uruchomienie bez okna mierzy całą klatkę tylko wtedy, gdy kończy się odczytaniem klatki z karty; `forest_scene` to robi. Bez tego czas obejmuje jedynie przekazanie pracy karcie.
- Na terenie Batorza z około 21000 rozmieszczonych rzeczy (bez okna, release, 150 klatek): 21,4 ms w lesie i 21,0 ms na starcie z pobranymi drzewami; 18,6 i 16,8 ms z generowanymi. Pomijanie po jednym rodzaju pokazało, gdzie idzie czas: same sosny kosztowały 5,4 ms, wszystkie drzewa razem około 12 ms, kamienie, głazy i przewrócony pień (skany z 40000 do 83000 trójkątów, bez prostszych poziomów) około 5 ms. Z prostszymi poziomami dla nich (niżej) oba widoki zajmują 14,9 ms.

## Znane ograniczenia

- Bez wiatru.
- Rośliny rozmieszcza wywołujący; reguły zależne od wysokości terenu, nachylenia i rodzaju podłoża przyjdą razem z terenem.
- Odrzucanie działa na procesorze, roślina po roślinie. Wystarcza dla dziesiątek tysięcy roślin; kilometr kwadratowy gęstego poszycia będzie go potrzebował na karcie graficznej.
- Jeszcze niezbudowane sposoby na tańsze cienie: odświeżanie dalekich kaskad tylko co kilka klatek oraz pełne bryły zastępcze koron w dalekich kaskadach.
