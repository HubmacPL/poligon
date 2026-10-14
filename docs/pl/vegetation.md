# Roślinność

Rysowanie roślin, kamieni i innych rzeczy, które stoją w wielkich ilościach: instancjonowanie, poziomy szczegółowości, komórki.

Nagłówki: `poligon/vegetation.h`, `poligon/renderer.h` (`DrawMeshInstances`). Przykład: `examples/vegetation`. Testy: `tests/vegetation_render_test.cpp`, część o instancjonowaniu w `tests/scene_render_test.cpp`.

**Stan: fundament.** Gotowe: instancjonowanie, poziomy szczegółowości wybierane według odległości, komórki, osobne traktowanie cieni, narzędzie robiące uproszczone poziomy drzew. Jeszcze nie: płynne przejścia między poziomami, płaskie obrazki dla najdalszych drzew (impostory), wiatr, rozmieszczanie roślin regułami terenu.

## Pojęcia

- **Instancjonowanie**: jedna siatka rysowana wiele razy jednym poleceniem rysowania, każda kopia z własnym przekształceniem. Las tysiąca sosen jednego rodzaju kosztuje garść poleceń, a nie tysiące.
- **Poziom szczegółowości** (LOD): ta sama roślina jako kilka siatek, od pełnej z bliska po proste z daleka.
- **Komórka**: kwadratowy kawałek świata (domyślnie 32 m), który trzyma swoje rośliny. Całe komórki można pominąć albo przekazać bez patrzenia na pojedyncze rośliny.
- **Kartka liści**: mały płaski kawałek ze zdjęciem gałązki, wycinany przezroczystością. Korony drzew składają się z tysięcy takich kartek.

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

## Robienie uproszczonych poziomów drzewa

`tools/assets/simplify_model.py` ze słowem `foliage`:

```
blender -b --factory-startup -P tools/assets/simplify_model.py -- WEJŚCIE WYJŚCIE.gltf 0.1 foliage
```

- Pień i gałęzie są upraszczane przez zwijanie trójkątów, jak dotąd.
- Liście są traktowane inaczej. Zwijanie kartek liści przerzedza koronę, aż drzewo wygląda na łyse. Zamiast tego usuwana jest część kartek (dla tego samego pliku zawsze te same), a pozostałe są powiększane, żeby korona zakrywała tę samą powierzchnię. Przy jednej dziesiątej trójkątów sosna zachowuje sylwetkę.
- Siatka liczy się jako liście, gdy wszystkie jej materiały biorą przezroczystość z tekstury. Modele, w których liście i kora dzielą jedną siatkę, są zwijane w całości (obecnie jacaranda i drzewo wyspowe).
- Uproszczone poziomy używają materiałów pełnego modelu, dopasowanych po nazwie; `LoadModelFromFile(path, model, error, false)` czyta taki plik bez tekstur.

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

## Znane ograniczenia

- Roślina zmienia poziom w jednej klatce; nie ma jeszcze płynnego przejścia, więc zmianę widać w marszu.
- Jeszcze bez impostorów: najdalszy poziom to wciąż siatka.
- Bez wiatru.
- Rośliny rozmieszcza wywołujący; reguły zależne od wysokości terenu, nachylenia i rodzaju podłoża przyjdą razem z terenem.
- Odrzucanie działa na procesorze, roślina po roślinie. Wystarcza dla dziesiątek tysięcy roślin; kilometr kwadratowy gęstego poszycia będzie go potrzebował na karcie graficznej.
- Jeszcze niezbudowane sposoby na tańsze cienie: odświeżanie dalekich kaskad tylko co kilka klatek oraz pełne bryły zastępcze koron w dalekich kaskadach.
