# Wejście, nagrywanie i odtwarzanie

Nagłówek: `poligon/core/input.h`. Przykład: `examples/core/input`. Wzorzec w grze: `SetUpToyInput` i `UpdatePlayerInput` w `game/src/toy_world.cpp`.

## Model

- Gra nie czyta klawiatury ani myszy. Czyta **akcje**: nazwane liczby, po jednej wartości na tick (przycisk to 0 lub 1, oś to dowolna liczba).
- **Wiązania** zamieniają urządzenia na akcje: `BindKey(akcja, klawisz, wartość)` (klawisze: litery, cyfry, strzałki, Space, Enter, Tab, Backspace, lewe Shift, Control i Alt, F1 do F12, Escape), `BindMouseButton`, `BindMouseMotion`. Dwa klawisze z +1 i -1 na tej samej akcji dają oś. Wartości wiązań się sumują, więc grę obowiązuje ewentualne ograniczenie zakresu.
- Źródło wartości wybiera się raz na przebieg: prawdziwe urządzenia (`UseLiveDevices`), stan podany ręcznie (`UseManualDevices` + `SetDeviceState`, dla testów), nagranie (`StartReplay`) albo nic (wszystko 0).
- `GameLoopConfig::input = &input` sprawia, że pętla woła `AdvanceTick` przed każdym `FixedUpdate`.
- Odczyt: `GetValue`, `IsDown` (powyżej 0,5), `WasPressed` i `WasReleased` (tylko w ticku zmiany).

## Reguły

1. Wszystkie akcje dodajemy przed pierwszym tickiem i przed startem nagrywania lub odtwarzania.
2. Systemy ECS nie sięgają do `Input`. Gra raz na tick przepisuje akcje do komponentu-singletonu (w zabawce `PlayerInput`), a systemy czytają ten komponent. Dzięki temu wejście danego ticku jest w zrzucie stanu.
3. Zdarzenia okna przetwarzamy w `Game::BeginFrame` (`ProcessPlatformEvents`), czyli przed tickami klatki.
4. Wszystko, co ma się odtwarzać z nagrania, musi przejść przez akcje. Odczyt urządzenia z pominięciem `Input` nie trafi do nagrania.
5. Escape: domyślnie kończy `GameLoop` (`GameLoopConfig::escape_quits`, zob. [game_loop.md](game_loop.md)). Gra, która go używa (menu pauzy), ustawia `escape_quits = false` i wiąże `Key::Escape` z własną akcją, która wtedy jest nagrywana i odtwarzana jak każdy inny klawisz.

## Flagi programu

- `--record PLIK`: zapisz wejście tego przebiegu.
- `--replay PLIK`: weź wejście z nagrania zamiast z urządzeń. Bez `--ticks` przebieg trwa tyle ticków, ile ma nagranie.
- Obie naraz są dozwolone (odtwórz i nagraj ponownie).

Przykład: `build\dev\bin\toy.exe --headless --replay game\tests\recordings\walk_jump_look.txt --dump-state stan.json --dump-frame klatka.png`

## Format nagrania

Zwykły tekst, który można napisać ręcznie. `#` zaczyna komentarz.

```
poligon-input 1
ticks_per_second 60
action move_x
action jump
tick 30 move_x=1
tick 60 jump=1
tick 62 jump=0
end 480
```

- `tick N nazwa=wartość ...` podaje wartości, które zmieniły się w ticku N; wartość obowiązuje do następnej zmiany. Numery ticków muszą rosnąć.
- `end N` to liczba ticków nagrania; po niej wszystkie akcje wracają do 0. Plik bez `end` (ucięty przez awarię) też się wczytuje.
- Plik może deklarować podzbiór akcji gry; pozostałe mają wartość 0. Akcja, której gra nie zna, jest błędem, podobnie jak inna liczba ticków na sekundę.
- Liczby zapisujemy z 9 cyframi znaczącymi, więc `float` wraca dokładnie.
- Plik jest zapisywany na bieżąco i opróżniany po każdej zmianie, więc przetrwa awarię gry.

## Ograniczenia

- Klawiatura i mysz, bez pada.
- Stan klawiszy jest próbkowany raz na tick; naciśnięcie i puszczenie w obrębie jednej klatki może zostać pominięte.
- Ruch myszy jest liczony bez przechwytywania kursora.
- Nagranie zawiera tylko wejście. Zakłada ten sam stan początkowy gry; nie ma w nim ziarna ani wersji gry.
