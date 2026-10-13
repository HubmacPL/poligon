# Renderowanie

Nagłówki: `poligon/renderer.h`, `poligon/math.h`, `poligon/platform.h`.
Przykład: `examples/renderer`. Shadery: `engine/shaders/*.hlsl`.

Dodatkowo `poligon/image.h` (obrazy w pamięci, PNG i JPEG) i przykład `examples/texture`.

## Jak to działa

- `Renderer` rysuje każdą klatkę do obrazu poza ekranem (1280x720 domyślnie). Z oknem ten obraz jest potem kopiowany do okna; bez okna można go tylko zapisać. Piksele są w obu trybach te same.
- Kolejność w klatce: `BeginFrame(camera, light)`, dowolnie wiele `DrawMesh(mesh, transform, material)` (albo z samym kolorem), `EndFrame()`. Po `EndFrame` można wywołać `SaveFrameToPng(path)` albo `CaptureFrame(image)`.
- Siatki i tekstury żyją do zniszczenia renderera; tworzymy je poza parą `BeginFrame`/`EndFrame`.
- Kolor tła (`RendererConfig::clear_color`) ustala się raz przy tworzeniu renderera.
- Błędy nie do naprawienia (brak karty, zła kolejność wywołań) kończą proces przez `FatalError`.
- Nagłówki silnika nie pokazują typów SDL.

## Konwencje

- Matematyka to GLM, używane wprost (`glm::vec3`, `glm::mat4`, `glm::quat`, `glm::translate`, `glm::lookAt`...). Zawsze przez `#include "poligon/math.h"`, nigdy przez nagłówki GLM, i bez własnych `GLM_FORCE_*`: konfigurację ustawia cel `poligon_engine`.
- Świat prawoskrętny, +Y w górę, kamera patrzy wzdłuż swojego -Z.
- Macierze 4x4 kolumnowe, wektory kolumnowe: `a * b` stosuje najpierw `b`. Typowy zapis przekształcenia obiektu: `translate * rotate * scale`.
- Kąty dla funkcji GLM w radianach: `glm::radians(stopnie)`.
- Głębia w przestrzeni przycinania od 0 (blisko) do 1 (daleko).
- Wektor utworzony bez wartości jest zerowy, macierz jednostkowa, kwaternion bez obrotu.
- Wektory mają tylko pola `x`, `y`, `z`, `w` (bez aliasów `.r` czy `.s`).
- Te ustalenia pilnuje `tests/math_test.cpp`.
- Przednie ściany trójkątów: przeciwnie do ruchu wskazówek zegara. Indeksy 32-bitowe.
- Współrzędne tekstury (UV): (0, 0) to lewy górny róg obrazu, (1, 1) prawy dolny. Tak samo jest w glTF.
- Kolory materiałów i światła są liniowe. Obraz wynikowy i tekstury kolorów są przechowywane w kodowaniu sRGB (jak pliki graficzne i monitory), a przeliczenia w obie strony robi karta. Kolor tła podaje się tak, jak ma wyglądać na ekranie.

## Tekstury i materiały

- `Image` (`poligon/image.h`): bajty RGBA, wiersze od góry. `LoadImageFromFile` czyta PNG i JPEG, `SaveImageToPng` zapisuje, `MakeCheckerImage` i `MakeSolidImage` tworzą obrazy w kodzie.
- `Renderer::CreateTexture(image, options)` kopiuje obraz na kartę. Domyślnie: obraz kolorów (sRGB) z mipmapami. `is_color = false` dla obrazów z danymi innymi niż kolor.
- **Mipmapy** to pomniejszone kopie tekstury używane, gdy jest rysowana mała lub daleko; bez nich odległe tekstury migoczą.
- `Material` ma kolor, teksturę i `uv_scale`. Kolor mnoży teksturę; materiał bez tekstury ma sam kolor; `uv_scale` powtarza teksturę.
- Próbkowanie jest jedno dla wszystkich tekstur: filtrowanie liniowe z mipmapami, anizotropia 8, powtarzanie poza zakresem 0..1.
- Test `tests/texture_render_test.cpp` renderuje bez okna i sprawdza pojedyncze piksele (orientację, kolory, powtarzanie, mipmapy) na obu backendach. To wzór na testy renderowania bez obrazów wzorcowych.

## Modele

Nagłówek: `poligon/model.h`. Przykład: `examples/model`. Narzędzie: `model_viewer`.

- `LoadModelFromFile(path, model, error)` czyta model przez bibliotekę Assimp: glTF/GLB, FBX, OBJ, DAE i około czterdziestu innych formatów. Nie wymaga karty graficznej.
- Wczytany `Model` trzyma się naszych konwencji niezależnie od pliku: układ prawoskrętny, +Y w górę, przednie ściany przeciwnie do ruchu wskazówek zegara, UV (0, 0) w lewym górnym rogu.
- Plik modelu to drzewo węzłów z przekształceniami. Wczytanie spłaszcza je do **instancji**: „narysuj tę siatkę z tym przekształceniem". `Model::bounds_min/max` to pudełko wokół całości.
- Każdy materiał ma kolor bazowy i, jeśli jest, zdekodowaną teksturę koloru (z pliku obok modelu albo osadzoną w nim). Tekstura, której nie da się wczytać, daje wpis w `Model::warnings`, a nie błąd.
- `UploadModel(renderer, model)` kopiuje siatki i tekstury na kartę; `DrawModel(renderer, render_model, transform)` je rysuje.
- `model_viewer PLIK` pokazuje model na podłodze w szachownicę z kamerą krążącą wokół (przeciąganie lewym przyciskiem myszy, A/D, W/S, Q/E). `model_viewer PLIK --headless --dump-frame out.png` zapisuje jeden obraz i wypisuje liczbę trójkątów, rozmiar i ostrzeżenia; tak agent ogląda model.
- Jeszcze nie wczytujemy: szkieletów, animacji, map normalnych i pozostałych tekstur materiału glTF, kamer i świateł z pliku. Przekształcenia lustrzane (ujemna skala) odwróciłyby nawinięcie trójkątów.

## Backend

- Domyślny: **Direct3D 12**. Vulkan jest budowany i testowany jako drugi (`--backend vulkan`).
- Powód wyboru: warstwa diagnostyczna D3D12 jest częścią Windows i działa od ręki (`--gpu-validation`). Dla Vulkana warstw walidacji na tej maszynie nie ma (wymagają instalacji Vulkan SDK), więc ten sam błąd przechodzi tam po cichu.
- W trybie bez okna renderer wyłącza „niejawne warstwy” Vulkana (nakładki i nagrywarki innych programów, np. OBS, RivaTuner), które inaczej ładują swój kod do procesu.
- Testy renderują tę samą klatkę na obu backendach i porównują obrazy. Dzięki temu shadery pozostają przenośne.

## Shadery

- Zwykły HLSL, kompilowany przy budowaniu przez DXC do DXIL (D3D12) i SPIR-V (Vulkan), a potem wbudowywany w bibliotekę silnika jako tablice bajtów. W czasie działania nie ma plików shaderów do znalezienia.
- Nowy shader: plik w `engine/shaders/` i wpis `poligon_embed_shader(...)` w `engine/CMakeLists.txt`.
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

- Jeden rodzaj materiału: kolor razy tekstura, jedno światło kierunkowe plus otoczenie. Bez cieni, przezroczystości, map normalnych.
- Normalne są poprawne dla obrotu i skalowania wzdłuż osi siatki; dowolne nierównomierne skalowanie z obrotem wymagałoby macierzy normalnych.
- Siatek i tekstur nie da się zwolnić przed zniszczeniem renderera.
- Tekstury są nieskompresowane (4 bajty na piksel plus mipmapy), co przy 4 GB pamięci karty wystarczy na testy, nie na duży świat.
- Kanał alfa tekstury jest ignorowany.
- Okno ma stały rozmiar; zmiana rozmiaru nie jest obsługiwana.
