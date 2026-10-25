# Determinizm i jego test

## Co gwarantujemy

To samo nagranie wejścia odtworzone tym samym plikiem wykonywalnym na tej samej maszynie daje identyczny stan gry po każdym ticku.

Nie gwarantujemy tego między różnymi budowami ani maszynami. Sprawdzone: budowa `dev` (Debug) i `asan` (zoptymalizowana) dają te same wartości wszystkich komponentów, ale inne numery obiektów, bo wersje debug i release biblioteki flecs tworzą różną liczbę własnych obiektów.

## Wymogi

- **Twardy:** identyczny zrzut stanu (JSON bajt w bajt) i identyczny skrót stanu po każdym ticku.
- **Z tolerancją:** końcowa klatka (PNG) zgodna według `image_compare` (do 2 na kanał, do 0,1% pikseli). Identyczności bit w bit nie wymagamy, choć dziś ją obserwujemy.

## Jak to sprawdzić

Dwa przebiegi z tym samym nagraniem:

```
toy --headless --replay NAGRANIE --dump-state a.json --hash-log a.hashes --hash-interval 1 --dump-frame a.png
toy --headless --replay NAGRANIE --dump-state b.json --hash-log b.hashes --hash-interval 1 --dump-frame b.png
state_compare a.json b.json --hash-logs a.hashes b.hashes
image_compare a.png b.png
```

- `--hash-log PLIK` zapisuje skrót stanu co `--hash-interval` ticków (domyślnie 60), po jednym wierszu: liczba wykonanych ticków i skrót.
- `state_compare` kończy się kodem 0 i słowem `IDENTICAL` albo kodem 1 i opisem: po którym ticku stany były jeszcze równe, po którym już nie, oraz pierwszą różniącą się linią zrzutu z nagłówkiem obiektu.
- Przy szukaniu przyczyny rozjazdu: `--hash-interval 1` wskazuje dokładny tick; potem `--ticks N --dump-state` dla tego ticku w obu przebiegach pokaże, który komponent się różni.

## Testy w `check.ps1`

W `game/CMakeLists.txt`:

- `toy_replay_run_a` (Direct3D 12) i `toy_replay_run_b` (Vulkan) odtwarzają `game/tests/recordings/walk_jump_look.txt` w osobnych procesach. Backend graficzny nie może wpływać na symulację.
- `toy_replay_state_is_identical`: wymóg twardy na tych dwóch przebiegach.
- `toy_replay_frame_matches`: wymóg z tolerancją.
- `toy_replay_difference_is_detected`: przebieg z nagraniem, w którym drugi skok jest o jeden tick później, musi zostać uznany za inny, a narzędzie musi wskazać tick 151. To dowód, że test widzi różnicę i dobrze ją lokalizuje.

Powiązane: `tests/ecs/ecs_order_probe.cpp` (powtarzalność flecs), `game/tests/toy_world_test.cpp` (dwa światy w jednym procesie), `tests/physics/physics_test.cpp` (sam świat fizyki).

Daleko od początku układu: pozycje w świecie są w double (patrz [rendering.md](rendering.md) i [physics.md](physics.md)), więc scena przesunięta daleko przebiega tak samo jak blisko początku, tylko przesunięta. `toy_world_test` przesuwa scenę gry-zabawki o (20000, 50, -16000) i wymaga po 300 tickach tych samych miejsc plus przesunięcie, z dokładnością 1e-6 m; `physics_test` robi to samo dla skrzyni i idącej postaci 16 km dalej. To sprawdzenie precyzji, nie obietnica identyczności co do bitu: przesunięta scena daje nieco inne bity (zmierzone: 1e-11 m).

Nagrania zrobione, zanim pozycje stały się double (2026-10-25), odtwarzają się po tej zmianie do nieco innej fizyki: w próbnym marszu `forest_scene` gracz kończy w tym samym miejscu z dokładnością do milimetra (tyle wypisywała stara wersja), ale rzucone skrzynie lądują nieco inaczej.

Grę-zabawkę symuluje silnik fizyki, więc wszystkie powyższe testy obejmują także jego; co utrzymuje powtarzalność fizyki, opisuje [physics.md](physics.md).

## Co psuje determinizm

Reguły z [ecs.md](ecs.md), [input.md](input.md) i [game_loop.md](game_loop.md) w jednym miejscu:

- czas inny niż `delta` ticku (zegar systemowy, czas klatki);
- wejście czytane z pominięciem `Input`;
- `rand()` albo generator, którego stan nie jest w komponencie;
- stan symulacji trzymany poza ECS (zmienne globalne, pola klasy gry);
- zmiana stanu w `Render`;
- wiele wątków we flecs;
- systemy tworzone po usunięciu jakiegokolwiek obiektu;
- niezainicjalizowane pola komponentów;
- funkcje matematyczne zależne od biblioteki (np. `sin`, `cos`) są powtarzalne na tym samym pliku wykonywalnym, ale mogą różnić się między budowami.

## Renderowanie z efektami czasowymi

Obrazy porównuje się z tolerancją, ale uruchomienie bez okna na jednej maszynie daje za każdym razem te same piksele, także z czasowym wygładzaniem krawędzi, które miesza każdą klatkę z poprzednimi: przesunięcie kamery i wzory szumu idą za licznikiem klatek renderera, nie za zegarem, a historia mieszka w rendererze. Zob. „Powtarzalne klatki przy efektach czasowych” w [rendering.md](rendering.md).
