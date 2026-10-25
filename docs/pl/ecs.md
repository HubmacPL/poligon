# ECS (flecs) i zrzut stanu

Nagłówki: `poligon/ecs/ecs.h`, `poligon/ecs/state_dump.h`.
Przykład: `examples/ecs/state_dump`. Wzorzec większej gry: `game/src/toy_world.cpp`.

## Pojęcia

- **Obiekt (entity)**: sam identyfikator. **Komponent**: dane doczepione do obiektu. **Znacznik (tag)**: komponent bez danych. **System**: funkcja uruchamiana co tick dla wszystkich obiektów z danym zestawem komponentów. **Singleton**: komponent istniejący raz na cały świat.
- **Faza**: etap ticku (`flecs::OnUpdate`, `flecs::PostUpdate`, ...). Fazy idą w stałej kolejności, systemy w jednej fazie w kolejności utworzenia.

Silnik nie opakowuje flecs: gra używa `flecs::world`, `flecs::entity` i systemów bezpośrednio (wersja 4.1.6).

## Reguły projektu

1. Tylko typowane API C++. Żadnych zapytań w napisach ani skryptów flecs.
2. Każdy komponent rejestruje swoje pola: `world.component<T>().member<float>("x")...`. Bez tego jego wartości nie trafią do zrzutu stanu.
3. Wszystkie systemy tworzymy na starcie, zanim jakikolwiek obiekt zostanie usunięty, każdy z jawną fazą `.kind(...)`.
4. Jeden wątek: nie wywoływać `world.set_threads`.
5. Czas tylko z kroku: `world.progress(delta)` zawsze z podanym krokiem, w systemach `it.delta_time()`.
6. Stan gry żyje w komponentach. Liczby losowe: generator, którego stan jest polem komponentu (patrz `Spawner` w zabawce), nigdy `rand()` ani zegar.
7. Na początku: `poligon::RegisterEngineTypes(world)`, żeby `glm::vec2`, `vec3`, `vec4`, `quat` i `Color` dało się zrzucać. W komponentach używamy ich jako pól: `.member<glm::vec3>("position")`.

## Zmiany w trakcie systemu

Tworzenie i usuwanie obiektów oraz dodawanie i usuwanie komponentów wewnątrz systemu jest kolejkowane i wykonywane po systemie. Zmiana wartości istniejącego komponentu działa od razu.

## Zrzut stanu

- `DumpWorldStateToJson(world)` zwraca cały stan gry jako JSON; `SaveWorldStateToJson(world, path)` zapisuje go do pliku; `HashWorldState(world)` daje 64-bitowy skrót.
- Flaga programu: `--dump-state PLIK.json` (zapis po zakończeniu przebiegu). Gra-zabawka wypisuje też `state_hash=...`.
- W zrzucie są wszystkie żywe obiekty gry (identyfikator, generacja, nazwa, komponenty) i singletony. Nie ma definicji: komponentów, systemów, modułów ani niczego z wnętrza flecs.
- **Generacja** to licznik ponownych użyć numeru obiektu po usunięciu poprzednika.
- Liczby są dokładne: `float` ma 9 cyfr znaczących, `double` 17. Dwa zrzuty są identyczne bajt w bajt dokładnie wtedy, gdy stany są identyczne.
- Pozycje w świecie w komponentach są `glm::dvec3` (`Position` gry-zabawki), cała reszta zostaje float (patrz „Pozycje w świecie i rysowanie względem kamery” w [rendering.md](rendering.md)). `RegisterEngineTypes` rejestruje `vec2`, `vec3`, `dvec2`, `dvec3`, `vec4`, `quat` i `Color`. `toy_world_test` stawia scenę gry-zabawki w początku układu i przesuniętą o (20000, 50, -16000) i wymaga, żeby każda skrzynka po 300 tickach była tam, gdzie nieprzesunięta plus przesunięcie, z dokładnością 1e-6 m (zmierzone: 1e-11 m).
- Komponent bez zarejestrowanych pól pojawia się jako `{"unreflected_bytes": N}` i trafia na listę `unreflected_components`. Testy gry wymagają, żeby ta lista była pusta.
- Obsługiwane typy pól: bool, liczby całkowite, float, double, `flecs::entity_t` (przez `.member(flecs::Entity, "nazwa")`), wyliczenia, zagnieżdżone struktury, tablice o stałym rozmiarze. Inne typy (napisy, wektory) kończą proces błędem `FATAL` z nazwą komponentu; wtedy trzeba rozszerzyć `state_dump.cpp`.

Dlaczego własny zrzut zamiast `world.to_json()` z flecs: wbudowany zapis obcina liczby zmiennoprzecinkowe do 10 cyfr po przecinku, więc dwa różne stany mogą dać ten sam tekst. `tests/ecs/state_dump_test.cpp` wypisuje informacyjnie, czy tak nadal jest.

## Powtarzalność

- Dokumentacja flecs gwarantuje kolejność systemów (pod warunkiem reguły 3), ale nie mówi wprost, że ta sama historia operacji daje to samo ułożenie danych w dwóch uruchomieniach.
- Sprawdza to `tests/ecs/ecs_order_probe.cpp`: stały pseudolosowy scenariusz tworzenia, usuwania ze środka, doczepiania i zdejmowania komponentów, usuwania rodziców z dziećmi oraz systemów zmieniających skład obiektów w trakcie iteracji. Systemy wliczają do skrótu kolejność odwiedzin. Test wymaga identycznego wyniku dwa razy w jednym procesie i między dwoma procesami.
- `game/tests/toy_world_test.cpp` robi to samo dla symulacji zabawki: dwa światy po 600 tickach muszą dać identyczny zrzut.

## Czego jeszcze nie ma

- Wczytywania stanu z JSON.
- Zdalnego podglądu flecs (REST); do włączenia tylko w budowie deweloperskiej, gdy będzie potrzebny.
