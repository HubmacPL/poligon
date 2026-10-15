# Narzędzia w grze

Okna z suwakami i odczytami rysowane na działającej grze, kamera latająca swobodnie i podglądy pojedynczych etapów klatki. Są dla człowieka przy ekranie; wszystko, co zmieniają, można też podać uruchomieniu bez okna przez plik.

Nagłówki: `poligon/debug_ui.h`, `poligon/render_settings.h`, `poligon/free_camera.h`. Przykład: `examples/debug_ui`.

## Okna

- Rysuje je biblioteka Dear ImGui (MIT), którą kod gry woła bezpośrednio: `ImGui::Begin`, `ImGui::Text`, `ImGui::SliderFloat`, `ImGui::End`.
- Włącza je `RendererConfig::debug_ui = true`. Renderer zaczyna klatkę Dear ImGui w `BeginFrame` i rysuje ją na końcu `EndFrame`, na gotowym obrazie. Wywołania Dear ImGui są dozwolone tylko między nimi.
- **W trybie bez okna (headless) okien nie ma.** `Renderer::IsDebugUiActive()` zwraca wtedy fałsz, gotowe okna nic nie robią, a własne wywołania `ImGui::` trzeba pominąć przez `if (renderer.IsDebugUiActive())`. Zrzut klatki z trybu bez okna nigdy więc nie zawiera okien; zrzut z uruchomienia z oknem zawiera.
- Gdy mysz jest nad oknem albo trwa pisanie w polu tekstowym, `Input` zgłasza mysz lub klawiaturę jako nietknięte, więc przeciąganie suwaka nie obraca przy okazji kamery. `ProcessPlatformEvents` przekazuje zdarzenia okna do Dear ImGui.
- Plik ustawień (`imgui.ini`) nie jest zapisywany: okna otwierają się w tym samym miejscu przy każdym starcie.

Gotowe okna:

| Funkcja | Co pokazuje |
|---|---|
| `ShowRenderSettingsWindow(renderer, settings, file_path)` | Suwaki słońca, nieba, cieni, mgły, przyciemniania zagłębień, ekspozycji, poświaty, podglądu diagnostycznego; przyciski Save i Load dla `file_path` |
| `ShowFrameStatsWindow(renderer)` | Klatki na sekundę, milisekundy na klatkę, siatki narysowane ze zgłoszonych, rysowania w cieniach, backend |
| `ShowWorldWindow(renderer, world)` | Każdą encję z komponentami i wartościami, tak jak w zrzucie stanu JSON; filtr po nazwie. Tylko do odczytu |

## Ustawienia obrazu

- `RenderSettings` łączy `DirectionalLight sun`, `Environment environment` i `PostSettings post`: wszystko o wyglądzie, co nie jest samą sceną. Części podaje się co klatkę do `BeginFrame` i `Renderer::SetPostSettings`.
- `PostSettings` (mapowanie tonów, ekspozycja, poświata, automatyczna ekspozycja, podgląd diagnostyczny) startuje jako `RendererConfig::post` i można je zmieniać między klatkami.
- `SaveRenderSettings(path, settings)` i `LoadRenderSettings(path, settings, error)` zapisują i czytają plik tekstowy, jedno ustawienie w wierszu:

```
poligon-render-settings 1

# fog
fog.density 0.0120000001
fog.color 0.800000012 0.779999971 0.699999988
fog.volumetric 1
```

- Liczby mają 9 cyfr znaczących, więc wczytany plik daje wartości identyczne co do bitu. Ustawienia, których w pliku nie ma, zachowują wartość; nieznana nazwa, zła liczba wartości albo słowo zamiast liczby to błąd, który nazywa wiersz, i wtedy nic się nie zmienia.
- Standardowa opcja `--render-settings PLIK` (w `CommandLineOptions::render_settings_path`) to droga, którą wygląd znaleziony suwakami trafia do uruchomienia bez okna: Save w oknie, potem `forest_scene --headless --render-settings PLIK --dump-frame out.png`. Obsługują ją `toy` i `forest_scene`.
- Część `quality` pliku trzyma zestaw ustawień grafiki i jego wartości (`RenderSettings::quality`, patrz [rendering.md](rendering.md)). `quality.preset NAZWA` jest stosowane przed każdym innym wierszem, więc zapisany plik (nazwa zestawu plus wszystkie wartości) wczytuje się dokładnie, a krótki plik pisany ręcznie, np. `quality.preset low` i jedna zmieniona wartość, znaczy „low, poza tym”.
- Jedna lista ustawień (`VisitRenderSettings`) steruje plikiem i oknem. Nowe ustawienie dopisuje się tam, raz.

## Podglądy diagnostyczne

- `PostSettings::debug_view` pokazuje jeden etap zamiast gotowej klatki: `AmbientOcclusion` (białe = otwarte, ciemne = zasłonięte), `Fog` (światło dodawane przez mgłę objętościową), `Bloom` (rozmyty obraz domieszany przez poświatę). Wyłączony etap jest biały.
- `Environment::show_shadow_cascades` barwi powierzchnie według kaskady cieni, która je cieniuje: czerwona, zielona, niebieska, żółta od bliskiej do dalekiej.
- Oba są w oknie ustawień i w pliku ustawień.

## Swobodna kamera

- `FreeCamera` trzyma pozycję, obrót w poziomie (yaw) i pochylenie (pitch). `Move(forward, right, up)`, `Turn(yaw, pitch)`, `SetFromCamera(camera)`, `ToCamera(base)`. Nie czyta urządzeń; wywołujący mówi, o ile ruszyć.
- `FlyFreeCamera(renderer, camera)` lata nią klawiszami I/J/K/L, U/O (dół, góra), lewy Shift (szybciej) i środkowym przyciskiem myszy (rozglądanie). Te klawisze są czytane przez Dear ImGui, a nie przez `Input`, więc nigdy nie trafiają do nagrania wejścia, a swobodna kamera nie jest częścią stanu gry: nagrany przebieg odtwarza się tak samo z nią i bez niej.
- W `toy`: F przełącza swobodną kamerę, Tab chowa okna. W `forest_scene`: F przełącza chodzenie i latanie, Tab chowa okna.

## Znane ograniczenia

- Kolory okien są przeliczane pod nasz obraz wyjściowy raz, na starcie; kolory wybrane w polu koloru są pokazywane nieco bledsze, niż się renderują.
- Okno świata czyta cały stan od nowa co klatkę; wystarcza dla setek encji, jest wolne dla wielu tysięcy.
- Okno świata polega na układzie wierszy zrzutu stanu; zmieniać jedno z drugim.
