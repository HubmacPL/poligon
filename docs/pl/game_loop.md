# Pętla gry i tryb bez okna

Nagłówki: `poligon/core/game_loop.h`, `poligon/core/fixed_step_clock.h`, `poligon/core/command_line.h`, `poligon/core/fatal_error.h`.
Przykłady: `examples/core/game_loop`, `examples/core/fixed_step_clock`, `examples/core/command_line`.

## Pojęcia

- **Tick**: jeden krok symulacji o stałej długości `1 / ticks_per_second` sekundy (domyślnie 60 na sekundę).
- **Klatka**: jedno wywołanie `Game::Render`.

## Dwa tryby

| | Bez okna (`headless = true`) | Czas rzeczywisty (`headless = false`) |
|---|---|---|
| Zegar ścienny | nigdy nie czytany | wyznacza liczbę ticków |
| Ticki na klatkę | dokładnie 1 | 0 do `max_ticks_per_frame` |
| Szybkość | tak szybko, jak pozwala maszyna | zgodna z czasem rzeczywistym |
| Powtarzalność | pełna | liczba klatek zależy od maszyny |

Weryfikacja zmian odbywa się w trybie bez okna.

## Reguły

- Cały stan symulacji zmienia się wyłącznie w `Game::FixedUpdate`. `Render` niczego w stanie nie zmienia.
- Czas w symulacji bierze się tylko z `TickInfo` (`delta_seconds`, `time_seconds`). Nie wolno czytać zegara systemowego w kodzie gry.
- `time_seconds` jest liczone jako `tick_index / ticks_per_second`, nie sumowane, więc nie narasta błąd zaokrągleń.
- `FrameInfo::delta_seconds` (w `Render`) to czas gry, który objęły ticki klatki: 1/60 s w trybie bez okna, od 0 do kilku ticków w czasie rzeczywistym. Efekty renderera zmieniające się w czasie (automatyczna ekspozycja, `Environment::frame_seconds`) biorą go stąd, więc odtworzenia się zgadzają.
- Błąd programisty (np. zła konfiguracja) kończy proces przez `FatalError`: linia `FATAL: ...` ze stosem wywołań na stderr i kod wyjścia 3, bez okna dialogowego. Szczegóły: [crashes.md](crashes.md).

## Wiersz poleceń

- `--headless`: tryb bez okna.
- `--ticks N`: zakończ po N tickach (N >= 1). Bez tej opcji gra z oknem działa do zamknięcia okna lub Escape.
- `--backend d3d12|vulkan`, `--gpu-validation`, `--dump-frame PLIK.png`: patrz [rendering.md](rendering.md).
- `--dump-state PLIK.json`: patrz [ecs.md](ecs.md). `--record`, `--replay`: patrz [input.md](input.md). `--hash-log`, `--hash-interval`: patrz [determinism.md](determinism.md). `--render-settings`: patrz [debug_tools.md](debug_tools.md).
- `--quality low|medium|high|ultra` (zestaw ustawień grafiki) i `--resolution SxW` (rozmiar obrazu, od 16 do 8192 w każdą stronę): patrz [rendering.md](rendering.md). Obie są tu tylko odczytywane i sprawdzane; stosuje je gra.
- Nieznana opcja jest błędem (kod wyjścia 2), żeby literówka nie przeszła po cichu.

Przykład: `build\dev\bin\toy.exe --headless --ticks 600 --dump-frame klatka.png`.

## Okno i tempo pętli

- Z oknem `Renderer::EndFrame` czeka na odświeżenie ekranu (vsync). Gra ustawia wtedy `GameLoopConfig::render_every_iteration = true`, żeby pętla rysowała w każdym obiegu, także gdy nie przypadł żaden tick.
- `ProcessPlatformEvents()` wywołuje się raz na klatkę w `Game::BeginFrame` (przed tickami klatki); zwraca `false`, gdy użytkownik zamknął okno lub nacisnął Escape.
- Kolejność w jednej klatce: `BeginFrame`, potem dla każdego ticku `Input::AdvanceTick` (jeśli ustawiono `GameLoopConfig::input`) i `FixedUpdate`, na końcu `Render`.

## Test powtarzalności

`cmake/CompareTwoRuns.cmake` uruchamia program dwa razy i wymaga identycznego wyjścia. Test `toy_headless_is_repeatable` używa go na grze-zabawce.
