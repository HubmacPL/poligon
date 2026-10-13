# Renderowanie

Nagłówki: `poligon/renderer.h`, `poligon/math.h`, `poligon/platform.h`.
Przykład: `examples/renderer`. Shadery: `engine/shaders/*.hlsl`.

## Jak to działa

- `Renderer` rysuje każdą klatkę do obrazu poza ekranem (1280x720 domyślnie). Z oknem ten obraz jest potem kopiowany do okna; bez okna można go tylko zapisać. Piksele są w obu trybach te same.
- Kolejność w klatce: `BeginFrame(camera, light)`, dowolnie wiele `DrawMesh(mesh, transform, color)`, `EndFrame()`. Po `EndFrame` można wywołać `SaveFrameToPng(path)`.
- Kolor tła (`RendererConfig::clear_color`) ustala się raz przy tworzeniu renderera.
- Błędy nie do naprawienia (brak karty, zła kolejność wywołań) kończą proces przez `FatalError`.
- Nagłówki silnika nie pokazują typów SDL.

## Konwencje

- Świat prawoskrętny, +Y w górę, kamera patrzy wzdłuż swojego -Z.
- Macierze 4x4 kolumnowe, wektory kolumnowe: `Multiply(a, b)` stosuje najpierw `b`.
- Głębia w przestrzeni przycinania od 0 (blisko) do 1 (daleko).
- Przednie ściany trójkątów: przeciwnie do ruchu wskazówek zegara. Indeksy 16-bitowe.
- Kolory obiektów i światła są liniowe; shader koduje wynik gammą 2.2. Kolor tła jest używany bez zmian.

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
  - tekstury i samplery shadera wierzchołków: `space0`; shadera fragmentów: `space2`.
- Struktury po stronie C++ (`FrameUniforms`, `ObjectUniforms`, `LightUniforms` w `renderer.cpp`) muszą odpowiadać `cbuffer` w HLSL.

## Weryfikacja obrazu

- `--dump-frame PLIK.png` zapisuje ostatnią klatkę po zakończeniu przebiegu.
- `--gpu-validation` włącza warstwę diagnostyczną; testy z tą flagą padają na każdym komunikacie `D3D12 ERROR` / `D3D12 WARNING`.
- `image_compare A.png B.png [--channel-tolerance N] [--max-different-percent P]` porównuje dwa obrazy z tolerancją (domyślnie różnica do 2 na kanał, do 0,1% pikseli). Kod 0 = zgodne, 1 = niezgodne.
- Obraz wzorcowy gry-zabawki: `tests/reference/toy_tick_45.png`. Po zamierzonej zmianie wyglądu: obejrzeć nowy zrzut z `build/<preset>/test_output/` i dopiero wtedy skopiować go na wzorzec.

## Znane ograniczenia

- Jeden rodzaj materiału: jednolity kolor, jedno światło kierunkowe plus otoczenie. Bez tekstur, cieni, przezroczystości.
- Normalne są poprawne dla obrotu i skalowania wzdłuż osi siatki; dowolne nierównomierne skalowanie z obrotem wymagałoby macierzy normalnych.
- Kodowanie gamma w shaderze zamiast formatu sRGB celu renderowania.
- Okno ma stały rozmiar; zmiana rozmiaru nie jest obsługiwana.
- Własna, minimalna matematyka (`math.h`) zamiast biblioteki.
