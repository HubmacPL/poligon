# Renderowanie

Nagłówki: `poligon/render/renderer.h`, `poligon/render/graphics_quality.h`, `poligon/world/sky_occlusion.h`, `poligon/core/math.h`, `poligon/core/platform.h`.
Przykłady: `examples/renderer`, `examples/texture`, `examples/environment`, `examples/model`, `examples/temporal_aa`, `examples/graphics_quality`, `examples/water`, `examples/clouds`. Shadery: `engine/src/render/shaders/<grupa>/*.hlsl`.

Dodatkowo `poligon/core/image.h` (obrazy w pamięci, PNG i JPEG) i przykład `examples/texture`.

## Jak to działa

- `Renderer` rysuje każdą klatkę do obrazu poza ekranem (1280x720 domyślnie). Z oknem ten obraz jest potem kopiowany do okna; bez okna można go tylko zapisać. Piksele są w obu trybach te same.
- Kolejność w klatce: `BeginFrame(camera, light)`, dowolnie wiele `DrawMesh(mesh, transform, material)` (albo z samym kolorem), `EndFrame()`. Po `EndFrame` można wywołać `SaveFrameToPng(path)` albo `CaptureFrame(image)`.
- Siatki i tekstury żyją do zniszczenia renderera; tworzymy je poza parą `BeginFrame`/`EndFrame`.
- Kolor tła (`RendererConfig::clear_color`) ustala się raz przy tworzeniu renderera.
- Błędy nie do naprawienia (brak karty, zła kolejność wywołań) kończą proces przez `FatalError`.
- Nagłówki silnika nie pokazują typów SDL.

## Etapy klatki

Klatkę nagrywają wywołania `DrawMesh`, a wykonuje ją `EndFrame`, w tych przebiegach (wyłączone są pomijane):

1. **Cienie.** Głębia sceny widziana ze słońca, cztery razy (**kaskady**): widoczny zakres do `Environment::shadow_distance` jest pocięty na cztery części, bliskie ostre, dalekie zgrubne (podział liczy się od 1 m, żeby najostrzejsza kaskada nie szła na pierwszy krok przed kamerą). Wszystkie cztery dzielą jeden obraz głębi jako kafelki 2 x 2. Każda kaskada to kula wokół swojej części widoku, przesuwana tylko o całe piksele cienia, więc cienie nie pełzną przy obrocie i ruchu kamery. Odczyt to filtr namiotowy szeroki na 5 pikseli cienia (9 dwuliniowych porównań), z punktem odsuniętym od powierzchni wzdłuż normalnej o zasięg filtru, co daje miękkie krawędzie bez schodków i bez samozacienienia. Na ostatniej dziesiątej części swojego zakresu każda kaskada płynnie przechodzi w następną, a ostatnia w pełne światło, więc nie widać linii tam, gdzie zmienia się ostrość ani gdzie cienie się kończą. Fartuchy terenu (rąbki zasłaniające szczeliny między kaflami) są pomijane w przebiegu cieni: widziane ze słońca są ścianami i na zboczu zacieniałyby teren poniżej w kratkę.
2. **Cienie chmur** (przy `Environment::clouds` i `cloud_shadows`). Na 8 km wokół kamery obraz 256 × 256 części światła słońca, którą chmury przepuszczają (`cloud_shadow.frag.hlsl`, osiem kroków w stronę słońca przez warstwę chmur). Każda oświetlona powierzchnia i mgła objętościowa mnożą przez niego słońce, więc cienie chmur płyną po terenie, a smugi światła w mgle gasną pod nimi.
3. **Głębia i przyciemnianie zagłębień** (ambient occlusion, `Environment::ambient_occlusion`). Najpierw rysowana jest głębia sceny widziana z kamery, z jedną próbką na piksel. Z niej renderer wylicza dla każdego piksela, na ile pobliska geometria zasłania otoczenie (szczeliny, wnętrze korony drzewa, styk pnia z ziemią), i rozmywa wynik. Przebieg sceny daje takim miejscom mniej światła otoczenia; bezpośrednie światło słońca się nie zmienia. `ambient_occlusion_radius` to zasięg w metrach.
4. **Scena.** Najpierw niebo, potem siatki, w kolorach liniowych o szerokim zakresie (wartości powyżej 1 zostają), z multisamplingiem (`RendererConfig::msaa_samples`, domyślnie 4). Materiały wycinane alfą używają **alpha to coverage**, więc krawędzie liści są gładkie.
5. **Woda** (`Renderer::DrawWater(mesh, transform, WaterMaterial)`; jeziora terenu generowanego dostają siatki z `CreateLakeSurfaces` w `poligon/world/water.h`). Rysowana po scenie, na jej obrazie z jedną próbką na piksel, z testem i zapisem obrazu głębi, więc mgła i TAA traktują powierzchnię jak każdą inną. Czyta kopie koloru i głębi sceny takie, jakie były bez niej (`water.frag.hlsl`): drobne fale przechylają powierzchnię (trzy rozmiary ruchomego szumu, dryfujące z wiatrem; `wave_size`, `wave_strength`); to, co leży pod wodą, jest czytane trochę w bok tam, gdzie fale załamują widok (`refraction`; nie tam, gdzie trafiłoby to na coś nad wodą), i przyciemniane osobno dla każdego koloru według drogi przez wodę (`absorption`, czerwony najpierw), a własny kolor wody, oświetlony niebem i słońcem, przejmuje z głębokością (`color`, `scattering`); promień odbity jest prowadzony przez obraz głębi (**odbicia w przestrzeni ekranu**, `Environment::water_reflection_steps` do `water_reflection_distance` metrów) i odbija to, co spotka na ekranie, a gdzie indziej niebo; prawo Fresnela miesza jedno z drugim, słońce dodaje odblask cieniowany jak wszystko inne, a tam, gdzie woda ma tylko centymetry głębokości, powierzchnia przechodzi w grunt pod nią (`shore_fade`). Ograniczenia: odbić można tylko to, co jest na ekranie (drzewa na drugim brzegu tak, to, co za kamerą, nie); bez odbić płaskich; kamera pod wodą nie jest obsługiwana; strumienie nie są powierzchniami wody. Test: `tests/water_render_test.cpp`; przykład: `examples/water`.
6. **Chmury** (`Environment::clouds`, tylko przy `draw_sky`). Tam, gdzie scena pokazuje niebo, linia wzroku jest przechodzona przez warstwę powietrza (od `cloud_bottom` do `cloud_top`) w mniejszym obrazie (`RendererConfig::cloud_resolution`, domyślnie ćwierć rysowanego rozmiaru), według Schneidera (Horizon Zero Dawn, 2015) i Hillaire'a (Frostbite, 2016): mapa pogody mówi, gdzie stoją chmury i jakiego są typu (`cloud_coverage` ją przesuwa), objętość Perlina-Worleya 64 × 64 × 64 daje ich kształt (`cloud_size` metrów na powtórzenie), a objętość Worleya 32 × 32 × 32 wygryza ich brzegi; światło w każdym kroku to słońce przez chmurę nad nim (kilka kroków w stronę słońca), ukształtowane dwupłatową funkcją fazy (srebrny brzeg pod słońce) i przyciemnieniem cienkich brzegów („powder”), plus światło nieba silniejsze u góry; każdy krok jest całkowany z zachowaniem energii. Kroki zaczynają się co klatkę od innego przesunięcia, a TAA to uśrednia. Daleko przy horyzoncie chmury znikają w mgiełce. Płyną z wiatrem z prędkością `cloud_speed` według `wind_time`, więc powtórki się zgadzają. Objętości szumu są robione kodem (około sekundy; `Renderer::PrepareClouds` robi to przy wczytywaniu). Chmury czynią też światło nieba bardziej szarym, do połowy przy pełnym zachmurzeniu. Koszt na GTX 980 w `forest_scene` w high: około 1 ms. Test: część o chmurach w `tests/effects_render_test.cpp`; przykład: `examples/clouds`. Ograniczenia: płaska warstwa (bez krzywizny Ziemi), bez światła w chmurach od błyskawic czy od ziemi, w odbiciach na wodzie tylko to, co widać na ekranie, kamera zawsze pod warstwą.
7. **Mgła objętościowa** (`Environment::volumetric_fog`, używa `fog_density` i `fog_color`). W połowie rozdzielczości każda linia wzroku jest przechodzona w 32 krokach; każdy krok dodaje światło, które mgła tam rozprasza, i przyciemnia to, co leży za nim. Kroki w cieniu słońca dodają mniej (`fog_shadow_brightness`), co widać jako **snopy światła** między drzewami. Mgła jaśnieje wokół słońca (`fog_sun_glow`), może rzednąć z wysokością (`fog_height_falloff`, `fog_base_height`) i przykrywa też niebo. Zastępuje prostą mgłę od odległości z przebiegu sceny.
8. **Czasowe wygładzanie krawędzi** (TAA, `PostSettings::temporal_aa`, domyślnie wyłączone). W każdej klatce kamera jest przesuwana o inny ułamek piksela (8 punktów ciągu Haltona), a nowa klatka jest mieszana z poprzednim wynikiem (`temporal_aa_blend`, 0,1: liczy się mniej więcej dziesięć ostatnich klatek). Dla każdego piksela obraz głębi daje punkt, który piksel pokazuje, nieprzesunięta kamera poprzedniej klatki daje, gdzie ten punkt był, i tam odczytywany jest poprzedni wynik filtrem Catmulla-Roma. O ruchu decyduje najbliższa powierzchnia w otoczeniu 3 × 3, żeby krawędzie bliskich obiektów szły razem z nimi. To, co ruszyło się w inny sposób (wiatr, zwierzęta), załatwia przycinanie historii do zakresu kolorów, jaki ma otoczenie piksela w nowej klatce (średnia plus minus 1,25 odchylenia standardowego w YCoCg, po ściśnięciu kolorów według jasności), co usuwa smugi. Szybciej ruszające się piksele biorą więcej z nowej klatki. Przy włączonym TAA wzory szumu przyciemniania zagłębień, kroków mgły i kropkowanych przejść zmieniają się co klatkę i uśredniają się. Przebieg głębi działa zawsze, gdy TAA jest włączone. MSAA nie jest przy nim potrzebne (`forest_scene` je wyłącza i oszczędza 3 ms); `sharpen` oddaje część miękkości. Skok kamery o ponad 25 m albo `Renderer::ResetTemporalHistory()` zaczyna od nowa. Jeszcze nie: ruch siatek animowanych i obiektów przesuwanych przez `DrawMesh` nie jest śledzony dla każdego piksela (tylko ruch kamery), więc szybko poruszające się rzeczy polegają na przycinaniu i mogą się lekko rozmazywać. Test: `tests/temporal_aa_test.cpp`; przykład: `examples/temporal_aa`.
9. **Poświata** (bloom, `RendererConfig::bloom`, `bloom_strength`). Scena jest zmniejszana do łańcucha coraz mniejszych, coraz bardziej rozmytych kopii, które potem są składane z powrotem i domieszane do obrazu: jasne miejsca dostają miękką poświatę.
10. **Mapowanie tonów i korekcja barwna.** Obraz o szerokim zakresie jest zamieniany na kolory do wyświetlenia: `exposure` skaluje jasność, **automatyczna ekspozycja** (`RendererConfig::auto_exposure`) najpierw sprowadza średnią jasność obrazu do `auto_exposure_target`, w każdej klatce pokonując `auto_exposure_speed` drogi (w pierwszej klatce całą), `ToneMapping::Aces` łagodnie spłaszcza jasne partie, `ToneMapping::None` obcina (dokładne; używane w testach). Korekcja barwna (`PostSettings`): `temperature` i `tint` (balans bieli, dzielony przez własną jasność, żeby nie zmieniał ekspozycji) oraz `saturation` działają w świetle liniowym przed krzywą, `contrast` wokół średniej szarości w skali pierwiastkowej po niej, potem `vignette`; `sharpen` działa na wygładzonym obrazie przed ekspozycją, ograniczone do zakresu czterech sąsiadów, żeby krawędzie nie dzwoniły.

**Powtarzalne klatki przy efektach czasowych.** TAA (a później chmury) korzysta z poprzednich klatek, a uruchomienie bez okna ma dawać za każdym razem ten sam obraz. Tak jest, bo wszystko, od czego zależy klatka, wynika z klatek przed nią, a te są renderowane tak samo: przesunięcie o ułamek piksela i przesunięcie szumu biorą się z licznika klatek renderera (nigdy z zegara), historia to dwa obrazy zapisywane na zmianę wewnątrz renderera, a nowy renderer startuje bez historii. Ten sam program, ustawienia i ciąg klatek dają więc te same piksele co do bitu na jednej maszynie i backendzie (sprawdza to `temporal_aa_test`, który renderuje te same 17 klatek dwa razy i porównuje bajty). Testy sprawdzające pojedyncze piksele zostawiają TAA wyłączone (domyślnie), więc ich pierwsza klatka nie jest z niczym mieszana; `forest_scene --headless --ticks N` renderuje klatkę w każdym ticku, więc zrzut po N tickach to zawsze N-ta klatka tego samego ciągu.

**Wiatr** porusza roślinami w każdym przebiegu (więc cienie też się ruszają). `Environment::wind_direction`, `wind_strength` (1 = umiarkowany wiatr, 0 = brak) i `wind_time` (własny czas gry w sekundach, żeby powtórki się zgadzały) opisują wiatr klatki; `Material::wind_bend` i `wind_flutter` mówią, jak bardzo siatka mu ulega. W shaderach wierzchołków (`ApplyWind` w `common.hlsli`) cała roślina pochyla się z wiatrem o `wind_bend` × siła × około 3 cm na metr nad stopą (początkiem układu modelu), z podmuchami wędrującymi po terenie jako dwie fale, startującymi dla każdej rośliny w nieco innej chwili, a każdy wierzchołek drży osobno o `wind_flutter` × siła × około 4 cm. Drzewa dostają pochylenie 1 i drżenie 1 na liściach; niskie rośliny większe pochylenie (trawa 6), bo gną się bardziej na metr. Przykład: `examples/wind`; test: część o wietrze w `tests/effects_render_test.cpp`.

Przed przebiegami siatki są **odrzucane** (culling): siatka, której pudełko otaczające leży w całości poza widokiem kamery, nie jest rysowana, a każda kaskada cieni pomija siatki, które nie mogą rzucić w nią cienia (siatka poza widokiem nadal może). `Renderer::GetFrameStats()` zwraca, ile siatek zgłoszono i narysowano w ostatniej klatce.

Wiele kopii jednej siatki rysuje `DrawMeshInstances` (jedno polecenie rysowania na przebieg, każda kopia odrzucana osobno); lasy idą przez zbudowany na tym system roślinności. Patrz [vegetation.md](vegetation.md).

## Jakość grafiki

Nagłówek: `poligon/render/graphics_quality.h`. Przykład: `examples/graphics_quality`.

- Scena decyduje, jak obraz wygląda (kolor mgły, zasięg cieni, jak daleko rysowane są rośliny); `GraphicsQuality` decyduje, ile może kosztować. Cztery gotowe zestawy: `low`, `medium`, `high`, `ultra`. `GetGraphicsPreset(preset)` daje wartości zestawu; każdą z nich można potem zmienić osobno.
- Zestaw wybiera się na trzy sposoby, z tym samym skutkiem: `quality.preset medium` w pliku ustawień obrazu (stosowany najpierw, gdziekolwiek stoi; wartości, które plik wymienia przed nim albo po nim, wygrywają), `--quality medium` w wierszu poleceń (gra przekazuje `CommandLineOptions::quality` do `ParseGraphicsPreset`) albo w oknie ustawień, gdzie wybór zestawu ustawia wszystkie jego wartości.
- Wartości, które wymagają nowego renderera (rozmiar obrazu cieni, multisampling, rozdzielczość rysowania), trafiają do `RendererConfig` przez `ApplyGraphicsQuality(quality, config)`. Przełączniki i liczby kroków ograniczają każdą klatkę przez `ApplyGraphicsQuality(quality, environment, post)`: przełącznik tylko wyłącza efekt, o który prosiła scena, zasięg cieni jest mnożony. Skale dla roślin, pokrywy ziemi i terenu gra przekazuje swoim systemom (`forest_scene` robi to w `ApplyQualityToSystems`).
- **Skala rysowania** (`render_scale`, tylko z TAA): scena, głębia, przyciemnianie zagłębień i mgła są rysowane w takiej części szerokości i wysokości obrazu wyjściowego; TAA, które działa w rozmiarze wyjściowym, odtwarza szczegóły z przesunięć podpikselowych kolejnych klatek (każdy piksel wyjściowy waży narysowane próbki ich odległością od swojego środka). Poświata i mapowanie tonów działają w rozmiarze wyjściowym. Bez TAA rysuje się zawsze w rozmiarze wyjściowym.
- **Dalekie kaskady** (`far_shadow_scale`, `RendererConfig::far_shadow_scale`): dwie dalekie kaskady cieni używają tylko takiej części szerokości i wysokości swojego kafelka (lewej górnej). Pokrywają duże obszary, gdzie drobny szczegół cienia i tak ginie, a w lesie koszt obrazu cieni rośnie z liczbą jego pikseli, bo korony drzew widziane ze słońca nakładają się wielokrotnie.
- Dwie rzeczy w rendererze tanieją wszystkie zestawy. Bez multisamplingu przebieg głębi działa zawsze, a przebieg sceny sprawdza tę głębię przed cieniowaniem („równa albo bliższa”, bez zapisu głębi, `[earlydepthstencil]` w `mesh_after_depth.frag.hlsl` i `impostor_after_depth.frag.hlsl`), więc każdy piksel jest oświetlany raz zamiast raz na każdy liść za nim. A instancje są rysowane od najbliższej: od kamery w widoku, od słońca w każdej kaskadzie, więc test głębi odrzuca większość zasłoniętych liści przed cieniowaniem ich pikseli (przy remisie zostaje kolejność instancji, więc wynik jest taki sam przy każdym uruchomieniu).

| | low | medium | high | ultra |
|---|---|---|---|---|
| skala rysowania (z TAA) | 0,67 | 0,8 | 0,9 | 1 |
| obraz cieni na kaskadę | 1024 | 1024 | 1536 | 4096 |
| część dla dalekich kaskad | 0,5 | 1 | 0,5 | 0,5 |
| zasięg cieni | x0,6 | x0,8 | x1 | x1,5 |
| przyciemnianie zagłębień, próbki | wył. | 12 | 20 | 32 |
| mgła objętościowa, kroki | wył. | 20 | 32 | 48 |
| kroki odbić w wodzie | 0 (tylko niebo) | 12 | 24 | 48 |
| kroki chmur, w stronę słońca | 20, 3 | 32, 4 | 48, 5 | 96, 6 |
| obraz chmur (część rysowanego rozmiaru) | 0,25 | 0,25 | 0,25 | 0,5 |
| szczegół roślin, zasięg ich cieni | x0,6, x0,5 | x0,8, x0,75 | x1, x1 | x1,5, x1,5 |
| gęstość i promień pokrywy ziemi | x0,35, x0,6 | x0,65, x0,8 | x1, x1 | x1,5, x1,3 |
| szczegół terenu | x0,6 | x0,8 | x1 | x1,5 |

Zmierzone w `forest_scene` na terenie generowanym z jego jeziorami, wypieczonymi assetami (z kompresją blokową), roślinami sadzonymi regułami, chmurami, ich cieniami, mgiełką i przesłanianiem nieba, na GTX 980 w 1920 x 1080 (`release`, bez okna, 200 ticków), w milisekundach na klatkę, w czterech widokach używanych przy każdym pomiarze: start z widokiem na jezioro `0 0`, w lesie `-60 -40 225 6`, na brzegu jeziora `-6 -230 12 -8`, wstecz nad lasem `0 0 185 -3`:

| | start | w lesie | brzeg | wstecz |
|---|---|---|---|---|
| low | 7,5 | 6,9 | 5,9 | 7,3 |
| medium | 10,8 | 9,9 | 7,8 | 11,7 |
| high | 13,2 | 13,0 | 9,9 | 15,6 |
| ultra | 34,2 | 31,0 | 17,6 | 37,0 |

Medium i high trzymają 60 klatek na sekundę (16,7 ms) wszędzie (widok brzegu stoi teraz przy pasie trzciny, `-6 -230 12 -8`). Skompresowane tekstury zdjęły z klatki od 10 do 35% (karta czyta ćwierć pamięci); chmury i ich cienie kosztują około 1 ms. W lesie resztę rozstrzygają obrazy cieni: na terenie Batorza high z 2048 pikselami na kaskadę zajmowało 30 ms, bez cieni 17 ms. Woda kosztuje od 0,5 do 2 ms tam, gdzie widać jezioro.

Pomiar: `forest_scene X Z OBRÓT POCHYLENIE --headless --ticks 200 --quality high --resolution 1920x1080 --dump-frame out.png`. Testy: część o zestawach w `tests/render_settings_test.cpp`, część o skali rysowania w `tests/temporal_aa_test.cpp`, część o dalekich kaskadach w `tests/effects_render_test.cpp`.

## Światło nieba, liści i oddalenia

- **Cień w kolorze nieba:** światło otoczenia przychodzi z nieba z góry i z ziemi z dołu (`ambient_sky`, `ambient_ground`); przy `sky_light` część nieba to średnia zenitu i horyzontu, więc powierzchnie w cieniu słońca biorą chłodny odcień. Pod chmurami staje się bardziej szare.
- **Przesłanianie w dużej skali:** `Renderer::SetSkyOcclusion(&map)` z `SkyOcclusionMap` (`poligon/world/sky_occlusion.h`): ile nieba widzi każde miejsce terenu, z wysokości horyzontu w ośmiu kierunkach do 160 m (`MakeTerrainSkyOcclusion`) i z koron drzew nad nim (`AddCanopy`). Światło nieba na każdej powierzchni jest przez nie mnożone (światło od ziemi przez 0,6 do 1 tej wartości), więc dna dolin, wąwozy i ściółka pod koronami są ciemniejsze niż otwarty teren, czego przyciemnianie zagłębień w przestrzeni ekranu nie widzi. `forest_scene` robi je na 2,4 km wokół startu co 4 m, z około 25000 koron, w 0,4 s. Ograniczenie: korona dostaje wartość ziemi pod nią.
- **Światło przez liście:** cienkie liście (materiały dwustronne) oświetlone od tyłu świecą cieplejszą, bardziej nasyconą zielenią (światło przechodzi przez kolor liścia dwa razy), najmocniej, gdy kamera patrzy przez nie pod słońce; `Environment::foliage_translucency` mówi, jak mocno (domyślnie 0,35, w `forest_scene` 0,5).
- **Mgiełka oddalenia (perspektywa powietrzna):** `Environment::aerial_density` (na metr; 0 = wyłączona). Każda oświetlona powierzchnia i woda przechodzą z odległością w mgiełkę koloru horyzontu nieba, jasną i ciepłą od strony słońca. Dalekie wzgórza robią się niebieskie bez żadnej mgły przy kamerze. `forest_scene` używa 0,00012 (połowa światła to mgiełka na około 6 km).
- Test: części o przesłanianiu nieba i mgiełce w `tests/effects_render_test.cpp`.

## Materiały i światło

- `Material` to model metaliczność-chropowatość z glTF: kolor bazowy (razy `texture`), `metallic`, `roughness`, opcjonalnie `normal_texture` (drobny relief) i `orm_texture` (zielony chropowatość, niebieski metaliczność, czerwony zacienienie otoczenia przy `occlusion_in_orm`). Tekstury z danymi tworzy się z `TextureOptions::is_color = false`.
- Mapy normalnych są w konwencji OpenGL (zielony wskazuje górę obrazu). Siatki nie potrzebują stycznych; shader wyznacza je dla każdego piksela.
- `DirectionalLight` to słońce: kierunek `direction`, w którym świeci, kolor `color` (może przekraczać 1) i płaskie światło otoczenia `ambient`. Jednostki: biała matowa powierzchnia zwrócona do słońca odbija dokładnie kolor słońca.
- `Environment` (trzeci argument `BeginFrame`): `draw_sky` (gradient od `sky_horizon` do `sky_zenith`, `sky_ground` poniżej, tarcza słońca), `sky_light` (powierzchnie oświetla niebo z góry i ziemia z dołu zamiast płaskiego otoczenia; gładkie powierzchnie odbijają niebo), `shadows` i `shadow_distance`, `fog_density` i `fog_color`. Domyślnie jest skromnie: bez nieba, bez mgły, cienie włączone.
- Materiały dwustronne są traktowane jak cienkie liście: światło z tyłu trochę przez nie prześwieca.
- Słoneczna scena ma wartości światła 2 do 4: włącz `auto_exposure` albo ustaw ekspozycję około 0,5 do 0,7.
- Nowe efekty są domyślnie wyłączone, żeby prosta scena pozostała prosta i dokładna; `examples/environment` włącza wszystkie.
- `tests/scene_render_test.cpp` sprawdza cienie, niebo, mgłę, orientację map normalnych, multisampling i mapowanie tonów po wartościach pikseli na obu backendach; `tests/effects_render_test.cpp` robi to samo dla przyciemniania zagłębień, mgły objętościowej, poświaty i automatycznej ekspozycji.
## Konwencje

- Matematyka to GLM, używane wprost (`glm::vec3`, `glm::mat4`, `glm::quat`, `glm::translate`, `glm::lookAt`...). Zawsze przez `#include "poligon/core/math.h"`, nigdy przez nagłówki GLM, i bez własnych `GLM_FORCE_*`: konfigurację ustawia cel `poligon_engine`.
- Świat prawoskrętny, +Y w górę, kamera patrzy wzdłuż swojego -Z.
- Macierze 4x4 kolumnowe, wektory kolumnowe: `a * b` stosuje najpierw `b`. Typowy zapis przekształcenia obiektu: `translate * rotate * scale`.
- Kąty dla funkcji GLM w radianach: `glm::radians(stopnie)`.
- Głębia w przestrzeni przycinania od 0 do 1 (`GLM_FORCE_DEPTH_ZERO_TO_ONE`). Kamera używa jej **odwróconej**: 1 na płaszczyźnie bliskiej, 0 na dalekiej (bliska i daleka zamienione w `glm::perspective`, głębia czyszczona do 0, test głębi „większe”). Przy zmiennoprzecinkowym obrazie głębi daje to milimetry precyzji kilometry dalej, tam gdzie zwykły kierunek ma metry i daleki teren, drzewa i impostory walczą o piksele. Kaskady cieni zostają przy zwykłym kierunku. Shadery czytające głębię kamery traktują 0 jako niebo.
- Wektor utworzony bez wartości jest zerowy, macierz jednostkowa, kwaternion bez obrotu.
- Wektory mają tylko pola `x`, `y`, `z`, `w` (bez aliasów `.r` czy `.s`).
- Te ustalenia pilnuje `tests/math_test.cpp`.
- Przednie ściany trójkątów: przeciwnie do ruchu wskazówek zegara. Indeksy 32-bitowe.
- Współrzędne tekstury (UV): (0, 0) to lewy górny róg obrazu, (1, 1) prawy dolny. Tak samo jest w glTF.
- Kolory materiałów i światła są liniowe. Obraz wynikowy i tekstury kolorów są przechowywane w kodowaniu sRGB (jak pliki graficzne i monitory), a przeliczenia w obie strony robi karta. Kolor tła podaje się tak, jak ma wyglądać na ekranie.

## Tekstury i materiały

- `Image` (`poligon/core/image.h`): bajty RGBA, wiersze od góry. `LoadImageFromFile` czyta PNG i JPEG, `SaveImageToPng` zapisuje, `MakeCheckerImage` i `MakeSolidImage` tworzą obrazy w kodzie.
- `Renderer::CreateTexture(image, options)` kopiuje obraz na kartę. Domyślnie: obraz kolorów (sRGB) z mipmapami. `is_color = false` dla obrazów z danymi innymi niż kolor.
- **Mipmapy** to pomniejszone kopie tekstury używane, gdy jest rysowana mała lub daleko; bez nich odległe tekstury migoczą.
- `Material` ma kolor, teksturę i `uv_scale`. Kolor mnoży teksturę; materiał bez tekstury ma sam kolor; `uv_scale` powtarza teksturę.
- `alpha_cutoff` powyżej 0 sprawia, że piksele o niższej alfie są niewidoczne (**wycinanie alfą**): tak rysuje się liście, jako płaskie kwadraty ze zdjęciem gałązki. `double_sided` rysuje obie strony każdego trójkąta i oświetla tył tak, jakby normalna była odwrócona. Modele dostają jedno i drugie z pliku (tryb alfa glTF i `doubleSided`). `back_faces_keep_normal` oświetla tył normalną przodu (źdźbła trawy, których normalne wskazują w górę). `casts_ambient_occlusion = false` wyłącza materiał z głębi, z której liczone jest przyciemnianie zagłębień; jest dopisywany do obrazu głębi zaraz potem, więc przebieg sceny, woda, mgła i TAA nadal go widzą (trawa: metoda w przestrzeni ekranu bierze cienkie źdźbło za litą ścianę). `normal_up` (od 0 do 1) obraca normalną o tyle ku pionowi przed oświetleniem, po obu stronach: trawa przy 1 jest oświetlona jak płaska ziemia, na której stoi, niezależnie od tego, w którą stronę zwrócone są jej karty. Żadnego z tych trzech nie ma w plikach modeli; ustawia je kod (dla roślin przez `PlantSettings`, zob. vegetation.md).
- Próbkowanie jest jedno dla wszystkich tekstur: filtrowanie liniowe z mipmapami, anizotropia 8, powtarzanie poza zakresem 0..1.
- Mipmapy są liczone na procesorze (takie same na każdym backendzie). Dla tekstury koloru materiału wycinanego (`TextureOptions::coverage_cutoff`, ustawiane przez `UploadModel` z progu materiału) każda mipmapa ma alfę przeskalowaną tak, żeby udział pikseli powyżej progu został taki jak w pełnym obrazie. Bez tego uśrednianie zdjęcia gałązki spycha alfę poniżej progu, korony rzedną z odległością i znikają ze zgrubnych kaskad cieni, zostawiając same cienie pni i gałęzi.
- Test `tests/texture_render_test.cpp` renderuje bez okna i sprawdza pojedyncze piksele (orientację, kolory, powtarzanie, mipmapy) na obu backendach. To wzór na testy renderowania bez obrazów wzorcowych.

## Modele

Nagłówek: `poligon/assets/model.h`. Przykład: `examples/model`. Narzędzie: `model_viewer`.

- `LoadModelFromFile(path, model, error)` czyta model przez bibliotekę Assimp: glTF/GLB, FBX, OBJ, DAE i około czterdziestu innych formatów. Nie wymaga karty graficznej.
- Wczytany `Model` trzyma się naszych konwencji niezależnie od pliku: układ prawoskrętny, +Y w górę, przednie ściany przeciwnie do ruchu wskazówek zegara, UV (0, 0) w lewym górnym rogu.
- Plik modelu to drzewo węzłów z przekształceniami. Wczytanie spłaszcza je do **instancji**: „narysuj tę siatkę z tym przekształceniem". `Model::bounds_min/max` to pudełko wokół całości.
- Każdy materiał ma kolor bazowy i, jeśli jest, zdekodowaną teksturę koloru (z pliku obok modelu albo osadzoną w nim). Tekstura, której nie da się wczytać, daje wpis w `Model::warnings`, a nie błąd.
- `UploadModel(renderer, model)` kopiuje siatki i tekstury na kartę; `DrawModel(renderer, render_model, transform)` je rysuje.
- `model_viewer` uruchomiony dwuklikiem przegląda modele z `poligon-assets/source` (N i P przełączają na następny i poprzedni); `model_viewer FOLDER` przegląda folder; `model_viewer PLIK` pokazuje jeden model. Kamera: przeciąganie lewym przyciskiem myszy, A/D, W/S, Q/E. `model_viewer PLIK --headless --dump-frame out.png` zapisuje jeden obraz i wypisuje liczbę trójkątów, rozmiar i ostrzeżenia; tak agent ogląda model.
- Za ciężki model (drzewa filmowej jakości mają miliony trójkątów): `tools/assets/simplify_model.py` działa w Blenderze bez okna i zapisuje glTF z zachowaną zadaną częścią trójkątów: `blender -b --factory-startup -P tools/assets/simplify_model.py -- WEJŚCIE WYJŚCIE.gltf 0.1`. Blender to narzędzie zewnętrzne; nic z niego nie jest linkowane.
- Impostory (dalekie karty z wypalonymi zdjęciami modelu) i kropkowane przenikanie między poziomami szczegółowości są opisane w [vegetation.md](vegetation.md).
- Drzewa generowane: `tools/assets/make_tree.mjs` uruchamia bibliotekę EZ-Tree (MIT) w Node.js i zapisuje pliki glTF z poziomami szczegółowości: `node tools/assets/make_tree.mjs NODE_MODULES FOLDER_ASSETÓW PRZEPIS.json`. Przepis (nasze są w `tools/assets/trees/`) podaje preset EZ-Tree ze zmianami, wysokość, tekstury kory i gałązek (kolor, mapa normalnych, zacienienie-szorstkość-metaliczność), prostokąty atlasu gałązek (tekstura z kilkoma gałązkami; każda karta pokazuje jedną, w jej własnych proporcjach) i poziomy. Pełne drzewo trafia do `generated/NAZWA/`, prostsze poziomy do `lods/NAZWA/lodN/`, czyli tam, skąd czyta `forest_scene`. Prostsze poziomy powstają z tego samego drzewa: mniej boków każdej rury, bez cienkich gałęzi, mniej, ale większych kart; skrypt kończy się błędem, jeśli poziom zmieniłby kształt drzewa. Format jest opisany na początku skryptu. EZ-Tree i three.js są zainstalowane poza repozytorium (`poligon-assets/tools/eztree`); FOLDER_ASSETÓW to `poligon-assets/source`.
- `model_viewer PLIK OBRÓT POCHYLENIE ODLEGŁOŚĆ` ustawia widok początkowy, do obrazków z bliska.
- `forest_scene` (w `tools/`) buduje kawałek lasu z modeli i tekstur podłoża z `poligon-assets` i pozwala po nim chodzić (W/S/A/D, Shift to bieg, przeciąganie myszą to rozglądanie). To scena pokazowa renderera i miejsce pomiaru czasu klatki: `forest_scene X Z OBRÓT POCHYLENIE --headless --ticks 60 --dump-frame out.png` wypisuje milisekundy na klatkę i liczbę narysowanych siatek. `--quality NAZWA` wybiera zestaw ustawień grafiki, a `--resolution SxW` rozmiar obrazu; czasy dla zestawów są w części Jakość grafiki. Las jest taki sam przy każdym uruchomieniu (stałe ziarno losowania). `--record PLIK` i `--replay PLIK` zapisują i odtwarzają wejście jak w `toy` ([input.md](input.md)). Brakujące modele są pomijane.
- Pobrane i odchudzone zasoby leżą w `poligon-assets/source/` obok projektu (`polyhaven/`, `lods/`, `generated/` i inne; zob. [assets.md](assets.md)), nigdy w repozytorium.
- Liście z jednolitym tłem dookoła: część paczek (glTF z Poly Haven) ma kolor liści w JPG bez przezroczystości, a maskę w osobnym pliku. Loader ostrzega wtedy, że tekstura materiału wycinanego nie ma przezroczystych pikseli. Naprawa: `image_tool merge-alpha KOLOR ALFA WYJŚCIE.png` łączy oba pliki w jeden PNG (i rozlewa kolory liści na przezroczyste piksele, żeby krawędzie nie miały ciemnej obwódki), potem `node tools/assets/set_gltf_cutout.mjs MODEL.gltf STARY_URI NOWY_URI` podpina go w modelu i oznacza materiał jako wycinany i dwustronny. Nietknięty plik zostaje jako `MODEL.gltf.original`. `image_tool pack-orm ZACIENIENIE SZORSTKOŚĆ WYJŚCIE.png` łączy osobne obrazy zacienienia i szorstkości w jedną teksturę danych, jakiej oczekuje glTF (czerwony zacienienie, zielony szorstkość, niebieski metaliczność = 0).
- Formaty, które nie potrafią oznaczyć materiału jako wycinanego (FBX, OBJ): jeśli co najmniej 1% tekstury materiału jest przezroczysty, materiał jest traktowany jako dwustronny wycinany (sierść, wąsy, liście). Pliki glTF mówią to same i wierzymy im na słowo.
- Szkielety i klipy animacji są wczytywane; patrz [animation.md](animation.md).
- Jeszcze nie wczytujemy: tekstur emisji, kamer i świateł z pliku, animacji części sztywnych. Przekształcenia lustrzane (ujemna skala) odwróciłyby nawinięcie trójkątów.

## Backend

- Domyślny: **Direct3D 12**. Vulkan jest budowany i testowany jako drugi (`--backend vulkan`).
- Powód wyboru: warstwa diagnostyczna D3D12 jest częścią Windows i działa od ręki (`--gpu-validation`). Dla Vulkana warstw walidacji na tej maszynie nie ma (wymagają instalacji Vulkan SDK), więc ten sam błąd przechodzi tam po cichu.
- W trybie bez okna renderer wyłącza „niejawne warstwy” Vulkana (nakładki i nagrywarki innych programów, np. OBS, RivaTuner), które inaczej ładują swój kod do procesu.
- Testy renderują tę samą klatkę na obu backendach i porównują obrazy. Dzięki temu shadery pozostają przenośne.

## Okno i pełny ekran

- `Renderer::SetWindowMode(WindowMode::Windowed, szerokość, wysokość)` pokazuje grę w oknie tego rozmiaru; `WindowMode::Fullscreen` przełącza monitor dokładnie na tę rozdzielczość i pokazuje tylko grę (wyłączny pełny ekran, nie okno bez ramki). Zwraca false i niczego nie zmienia, gdy monitor nie ma takiego trybu. Wywołuje się ją między klatkami; następna klatka jest rysowana w nowym rozmiarze, bo każdy obraz zależny od rozmiaru powstaje od nowa (wygładzanie czasowe zaczyna od zera). Bez okna zmienia się tylko rozmiar obrazu i tak sprawdzają to testy.
- W `forest_scene` F11 przełącza między oknem 1280 × 720 a pełnym ekranem 1920 × 1080. Przykład: `examples/window_mode`; test: część o oknie w `tests/effects_render_test.cpp`.

## Shadery

- Zwykły HLSL, kompilowany przy budowaniu przez DXC do DXIL (D3D12) i SPIR-V (Vulkan), a potem wbudowywany w bibliotekę silnika jako tablice bajtów. W czasie działania nie ma plików shaderów do znalezienia.
- Nowy shader: plik w pasującej grupie `engine/src/render/shaders/` i wpis `poligon_embed_shader(...)` w `engine/src/render/CMakeLists.txt`. Pliki dołączane przez kilka shaderów trafiają do `shaders/common` (jest na ścieżce dołączania każdego shadera).
- Reguły SDL GPU, których trzeba się trzymać w HLSL:
  - wejścia wierzchołka mają semantyki `TEXCOORD0`, `TEXCOORD1`, ... (numer = lokalizacja atrybutu);
  - bufory stałych shadera wierzchołków: `register(bN, space1)`; shadera fragmentów: `register(bN, space3)`;
  - tekstury i samplery shadera wierzchołków: `space0`; shadera fragmentów: `space2`;
  - dla Vulkana tekstura i jej sampler muszą być jednym połączonym wiązaniem: oba dostają `[[vk::combinedImageSampler]][[vk::binding(N, 2)]]` z tym samym N (wzór w `mesh.frag.hlsl`). Kompilacja do DXIL te atrybuty pomija.
- Struktury po stronie C++ (`FrameUniforms`, `ObjectUniforms`, `LightUniforms` w `renderer.cpp`) muszą odpowiadać `cbuffer` w HLSL.

## Weryfikacja obrazu

- `--dump-frame PLIK.png` zapisuje ostatnią klatkę po zakończeniu przebiegu.
- `--gpu-validation` włącza warstwę diagnostyczną; testy z tą flagą padają na każdym komunikacie `D3D12 ERROR` / `D3D12 WARNING`.
- `image_compare A.png B.png [--channel-tolerance N] [--max-different-percent P]` porównuje dwa obrazy z tolerancją (domyślnie różnica do 2 na kanał, do 0,1% pikseli). Kod 0 = zgodne, 1 = niezgodne.
- Na jednym backendzie obrazy z dwóch przebiegów są identyczne, więc porównujemy je domyślną, ciasną tolerancją. Między Direct3D 12 a Vulkanem używamy `--channel-tolerance 12`: sterowniki nieco inaczej liczą filtrowanie anizotropowe tekstur (zmierzone: do 32 na 255 na kilkuset pikselach krawędzi). Bez anizotropii oba backendy dają obraz identyczny co do bitu.
- Mipmapy liczymy sami na procesorze (`MakeHalfSizeImage`), więc dane tekstur są takie same na każdym backendzie i sterowniku.
- Obraz wzorcowy gry-zabawki: `tests/reference/toy_tick_45.png`. Po zamierzonej zmianie wyglądu: obejrzeć nowy zrzut z `build/<preset>/test_output/` i dopiero wtedy skopiować go na wzorzec.

## Znane ograniczenia

- Jedno światło (słońce) plus światło nieba; bez świateł punktowych i reflektorów. Bez prawdziwej przezroczystości (tylko wycinanie). Bez odbić sceny.
- Przyciemnianie zagłębień i mgła widzą tylko to, co jest na ekranie, i tylko najbliższą powierzchnię każdego piksela. Słońce oświetla mgłę tylko w zasięgu `shadow_distance` (dalej liczy się jako oświetlona), a linia wzroku jest przechodzona najwyżej na 1000 m.
- Odrzucanie działa na całe siatki i tylko według widoku: siatka zasłonięta przez inną nadal jest rysowana.
- Geometria sceny jest rysowana jeszcze raz dla przebiegu głębi, gdy włączone jest przyciemnianie zagłębień, mgła objętościowa albo TAA, albo gdy multisampling jest wyłączony; bez multisamplingu ten przebieg oszczędza więcej, niż kosztuje (patrz Jakość grafiki).
- Automatyczna ekspozycja dostosowuje się na klatkę, nie na sekundę, więc jej tempo zależy od liczby klatek na sekundę.
- Normalne są poprawne dla obrotu i skalowania wzdłuż osi siatki; dowolne nierównomierne skalowanie z obrotem wymagałoby macierzy normalnych.
- Tekstury robione z obrazów (`CreateTexture(const Image&)`) są nieskompresowane (4 bajty na piksel plus mipmapy); wypieczone assety mają kompresję blokową (od ćwierci do ósmej części tego, patrz [assets.md](assets.md)).
- Kanał alfa tekstury służy tylko do wycinania (`alpha_cutoff`), nigdy do mieszania kolorów.
- Okno ma stały rozmiar; zmiana rozmiaru nie jest obsługiwana.
