# Fizyka

Bryły, które spadają, zderzają się i zatrzymują, postać, która po nich chodzi, i promienie do pytania, co leży w danym kierunku.

Nagłówek: `poligon/physics/physics.h`. Przykład: `examples/physics/physics`. Test: `tests/physics/physics_test.cpp`.

## Jak to działa

- Pracę wykonuje biblioteka Jolt Physics (MIT). Nic z niej nie wystaje poza `physics.cpp`; cały interfejs to `PhysicsWorld`.
- Jednostki: metry, kilogramy, sekundy. +Y to góra. Grawitacja to `PhysicsConfig::gravity`.
- Pozycje są `double` (`glm::dvec3`): pozycje brył i postaci, początki i trafienia promieni, początki pól wysokości, granice. Jolt jest budowany z `JPH_DOUBLE_PRECISION` z naszego portu nakładkowego `ports/joltphysics` (port vcpkg tego samego Jolta 5.6.0 z `DOUBLE_PRECISION=ON`; kod Jolta bez zmian). Prędkości, rozmiary i obroty zostają `float`. `GetTransform` zwraca `poligon::Transform` (patrz „Pozycje w świecie i rysowanie względem kamery” w [rendering.md](rendering.md)).
- Daleko od początku układu działa tak samo jak blisko: `physics_test` stawia ląd, leżącą skrzynię i idącą postać 16 km dalej (22,6 km od początku układu) i wymaga, żeby skrzynia stała w miejscu z dokładnością 0,1 mm przez 600 ticków, a marsz kończył się tam, gdzie blisko początku, z dokładnością 1 mm (zmierzone: 0 m i 2e-12 m). Jolt trzyma pozycje w double, a zderzenia liczy we float względem pobliskiego punktu, więc precyzja nie spada z odległością.
- Renderer nic nie wie o fizyce. Gra pyta świat, gdzie jest bryła (`GetTransform`), i rysuje tam siatkę.
- `Step(delta_seconds)` przesuwa świat o jeden tick. Wołać raz na tick z długością ticku, nigdy ze zmierzonym czasem.

## Bryły

- Bryła **statyczna** nigdy się nie rusza (grunt, drzewa, ściany). Bryła **dynamiczna** (`BodySettings::dynamic = true`) ma masę (`mass`), spada i daje się popychać.
- Kształty: `AddBox(half_size)`, `AddSphere(radius)`, `AddCylinder(half_height, radius)` (pionowy), `AddCapsule(half_height, radius)` oraz `AddMesh(vertices, indices)` dla dowolnych trójkątów, tylko statycznie (grunt). Rozmiary to połowy wymiarów; `BodySettings::position` to środek kształtu.
- `friction` (tarcie): 0 to lód, 1 to guma. `bounciness` (sprężystość): 0 nie odbija się, 1 odbija się tak szybko, jak przyleciało; z dwóch stykających się brył decyduje bardziej sprężysta.
- Każde `Add...` zwraca `BodyHandle`. Uchwyty mają numery 1, 2, 3... w kolejności dodawania brył, a numer nigdy nie jest używany ponownie. `GetLastBodyId()` i `IsBody(handle)` pozwalają narzędziu odwiedzić każdą bryłę.
- Odczyt: `GetPosition`, `GetRotation`, `GetVelocity`, `GetAngularVelocity`, `GetTransform`, `GetBounds`, `IsAsleep`, `IsDynamic`. Bryła, która się zatrzymała, zasypia i nic nie kosztuje, dopóki coś jej nie dotknie.
- Zmiana: `SetPosition` (od razu, przez wszystko), `SetVelocity`, `AddImpulse` (impuls = masa razy pożądana zmiana prędkości), `RemoveBody`.

## Postacie

- Postać to gracz albo cokolwiek innego, co chodzi: pionowa kapsuła, której nie popycha się jak bryłę, tylko mówi się jej, dokąd chce iść.
- `AddCharacter(settings)`: `position` to miejsce stóp; `height`, `radius`; `max_slope_degrees` (na bardziej strome zbocza nie da się wejść); `step_height` (na progi tej wysokości wchodzi się bez skoku); `jump_speed`.
- Co tick: `SetCharacterInput(character, walk_velocity, jump)`, potem `Step`. Świat trzyma postać na ziemi, poza ścianami, i każe jej spadać, gdy pod spodem nic nie ma.
- `GetCharacterPosition` (stopy), `GetCharacterVelocity`, `IsCharacterOnGround`, `SetCharacterPosition`.
- Postać stojąca na zboczu nie zsuwa się z niego. Bryły nie czują postaci: nie umie ona jeszcze popchnąć skrzyni.

## Promienie

- `CastRay(origin, direction, max_distance)` zwraca pierwszą bryłę na drodze promienia: `hit`, `distance`, `position`, `normal` (w którą stronę zwrócona jest powierzchnia) i `body`. Postacie nie są trafiane.
- Zastosowania: w co trafia strzał, jak wysoko jest grunt pod punktem, czy coś stoi między dwoma punktami.

## Determinizm

- `PhysicsWorld`, który dostaje te same wywołania w tej samej kolejności z tą samą długością kroku, daje wyniki identyczne co do bitu, na tej samej maszynie i tym samym programie. Jolt obiecuje to pod tymi warunkami, a my to sprawdzamy: `physics_test` wykonuje skrypt dwa razy i porównuje `HashState()` w każdym ticku, a testy powtórek gry-zabawki porównują cały zrzut stanu.
- Co to utrzymuje: stały tick; jeden wątek (`JobSystemSingleThreaded`); bryły dodawane i usuwane w kolejności zależnej wyłącznie od stanu gry. W systemie ECS bryłę tworzy się od razu, a nie z odroczonego wywołania, którego kolejność mogłaby się zmieniać.
- `HashState()` to skrót pozycji, obrotu i prędkości każdej bryły i postaci.
- Bez obietnicy: te same wyniki z budowy Debug i Release, z innego kompilatora albo na innej rodzinie procesorów. Jolt ma do tego opcję (około 8% wolniej), której nasza budowa nie włącza.
- Pozycje w double zmieniły rachunki Jolta: nagranie zrobione przed tą zmianą (2026-10-25) odtwarza się po niej do nieco innych pozycji.

## W grze z flecs

Wzorcem jest `game/src/toy_world.cpp`:

- `PhysicsWorld` nie jest komponentem. Systemy sięgają po niego przez wskaźnik kontekstu świata flecs (`world.set_ctx(&physics)`).
- Encja z bryłą ma komponent `Body { handle }`.
- Jeden system robi krok świata (`flecs::OnValidate`, po własnych systemach gry z `OnUpdate`); następny kopiuje pozycję, obrót i prędkość ruchomych brył do komponentów. Zrzut stanu świata flecs pokazuje wtedy cały stan gry, razem z fizyką.
- System, który usuwa encję, najpierw usuwa jej bryłę.

## Znane ograniczenia

- Jeszcze bez połączeń (zawiasy, liny), bezwładnych ciał, pojazdów i ciał miękkich; Jolt ma je wszystkie.
- `PhysicsConfig::max_bodies` (domyślnie 16 384) jest ustalane przy tworzeniu świata; pamięć robocza kroku rośnie razem z nim (1 KB na ciało, co najmniej 16 MB). Kolizje strumieniowanego świata (poligon/world/streaming.h) mogą potrzebować więcej: `forest_scene` używa 65 536.
- Jeszcze bez zdarzeń zderzeń („to dotknęło tamtego”); gra może pytać tylko promieniami i o pozycje.
- Bryła dynamiczna nie może mieć kształtu z siatki trójkątów.
- Kształty kolizyjne nie są rysowane drucianą siatką. `forest_scene` (klawisz B) rysuje zamiast tego pudełko otaczające każdej bryły jako pełne kolorowe pudło.
- Renderer nie ma jeszcze siatek kuli ani walca, więc okrągłe bryły są w przykładach rysowane jako pudełka.
