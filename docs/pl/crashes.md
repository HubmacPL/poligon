# Awarie i błędy krytyczne

Nagłówki: `poligon/core/crash_handler.h`, `poligon/core/fatal_error.h`. Przykład: `examples/crash_handler`. Testy: `tests/crash_test.cpp`.

## Co robi

`poligon::InstallCrashHandler()` w pierwszej linii `main` sprawia, że program, który pada:

- nigdy nie otwiera okna dialogowego (przebieg bez okna nie może zawisnąć);
- wypisuje na stderr raport: co się stało oraz stos wywołań z nazwami funkcji, plikami i numerami linii;
- zapisuje ten sam raport do pliku `<program>_crash.txt` obok pliku wykonywalnego (albo pod ścieżką z `CrashHandlerConfig::report_path`);
- kończy się kodem 4.

`poligon::FatalError(komunikat)` robi to samo z nagłówkiem `FATAL:` i kodem 3; stos wypisuje także bez zainstalowanej obsługi awarii.

## Kody wyjścia programów Poligonu

| Kod | Znaczenie |
|---|---|
| 0 | sukces |
| 1 | błąd działania zgłoszony przez program (np. nie udało się zapisać pliku) |
| 2 | złe argumenty wiersza poleceń |
| 3 | `FatalError`: błąd programisty wykryty przez silnik |
| 4 | awaria |

## Jak czytać raport

```
CRASH: access violation writing address 0x0000000000000000
Program: toy.exe
Stack:
  #0 toy::MoveSystem (...\toy_world.cpp:123)
  #1 ...
```

- Pierwsza linia mówi, co się stało. `#0` to miejsce awarii, niżej kolejni wywołujący.
- Dla nieprzechwyconego wyjątku C++ pierwsza linia zawiera jego `what()`, a stos zaczyna się w miejscu `throw`.
- Dla `abort()` i sprawdzeń biblioteki debugowej pierwsze ramki należą do biblioteki; kod gry jest tuż pod nimi.

## Co jest łapane

Błędny dostęp do pamięci, przepełnienie stosu, dzielenie całkowite przez zero i inne wyjątki procesora, `abort()`, nieprzechwycone wyjątki C++, wywołanie czysto wirtualne, błędne argumenty funkcji biblioteki C, a w budowie Debug także sprawdzenia biblioteki debugowej (np. indeks wektora poza zakresem).

## Ograniczenia

- Nazwy funkcji wymagają pliku `.pdb` obok programu. Wszystkie obecne presety go tworzą.
- W budowie `asan` błędy pamięci raportuje AddressSanitizer we własnym formacie, zanim zobaczy je nasza obsługa.
- Awarie kończone przez Windows z pominięciem obsługi wyjątków (`__fastfail`, np. wykryte nadpisanie stosu przy `/GS`) nie dają raportu, tylko kod wyjścia `0xC0000409`.
- Obsługa dotyczy wątku głównego w zakresie rezerwy stosu na przepełnienie; silnik jest dziś jednowątkowy.
- Raport jest tekstowy. Zrzutu pamięci (`.dmp`) dla debugera nie zapisujemy.
- Ścieżki plików w raporcie są bezwzględne (z maszyny, na której budowano).
