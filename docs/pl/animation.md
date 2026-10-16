# Animacja szkieletowa

Jak model się wygina: szkielet ze stawów, siatka, której wierzchołki za nimi podążają, i klipy mówiące, jak stawy ruszają się w czasie.

Nagłówki: `poligon/animation/animation.h`, `poligon/assets/model.h`, `poligon/render/renderer.h`. Przykład: `examples/animation/animation`. Test: `tests/animation/skinning_render_test.cpp`.

## Pojęcia

- **Staw** (kość, ang. joint): punkt szkieletu z położeniem względem stawu nadrzędnego. Ruch stawu porusza wszystkie stawy pod nim.
- **Skinning**: każdy wierzchołek siatki podąża za najwyżej czterema stawami, za każdym z pewną wagą; wagi sumują się do 1. Gdy stawy się ruszają, wierzchołek trafia w ważoną mieszankę miejsc, w które zaniósłby go każdy ze stawów.
- **Poza spoczynkowa** (rest pose): gdzie są stawy, gdy nie gra żadna animacja. **Poza wiązania** (bind pose): gdzie były, gdy przypinano do nich siatkę. Macierz `inverse_bind` stawu przenosi wierzchołek z siatki do przestrzeni tego stawu w pozie wiązania.
- **Poza**: jeden `JointTransform` (przesunięcie, obrót, skala względem rodzica) na staw.
- **Macierze stawów**: jedna macierz na staw, przenosząca wierzchołek z miejsca, w którym go wymodelowano, tam, gdzie stawia go staw w pozie. Tego potrzebuje karta graficzna.

## Dane

- `Skeleton` to lista stawów (`Joint`); rodzic zawsze stoi przed dziećmi. `Skeleton::FindJoint(name)` znajduje staw po nazwie.
- `AnimationClip` ma nazwę, czas trwania w sekundach i po jednym `AnimationChannel` na ruszający się staw, każdy z osobnymi listami kluczy przesunięcia, obrotu i skali (czas w sekundach, wartość).
- `LoadModelFromFile` wypełnia `Model::skeleton` i `Model::animations`, gdy plik ma siatkę ze skinningiem. Stawy to kości z pliku plus każdy węzeł nad nimi w drzewie pliku. `ModelMesh::skin` trzyma po jednym `SkinVertex` (cztery numery stawów, cztery wagi) na wierzchołek; dla sztywnej siatki jest pusty.
- Ograniczenia: 256 stawów na szkielet (model z większą liczbą wczytuje się bez szkieletu, z ostrzeżeniem), cztery stawy na wierzchołek (zostają cztery najsilniejsze). Animacje części sztywnych (drzwi, koło) nie są wczytywane.
- `model_viewer PLIK --headless` wypisuje liczbę stawów i klipy z czasami trwania.

## Odtwarzanie klipów

```cpp
poligon::AnimationSet animations(model.skeleton, model.animations);   // raz na model
poligon::AnimationPlayer player(animations);                          // jeden na postać
player.Play(animations.FindClip("Walk"));
player.Update(tick.delta_seconds);                                    // co tick
poligon::DrawModel(renderer, render_model, transform, player.GetJointMatrices());   // co klatkę
```

- `AnimationSet` przygotowuje szkielet i jego klipy do odtwarzania; buduje się go raz na model i współdzieli. `AnimationPlayer` trzyma czas jednej postaci. Zestaw musi żyć dłużej niż jego odtwarzacze.
- Wypieczony model (poligon/assets/assets.h) przynosi zestaw gotowy: `LoadedModel::animations`, wczytany z archiwum ozz-animation zapisanego przez wypiekacz, bez budowania. `AnimationSet::SaveArchive()` daje to archiwum (własny format ozz dla zbudowanych szkieletów i klipów), a `AnimationSet::LoadArchive(szkielet, bajty, błąd)` wczytuje je z powrotem dla szkieletu, z którego powstało; wczytany zestaw gra dokładnie tak jak zapisany (`tests/animation/animation_archive_test.cpp`). Budowanie z klipów zostaje dla modeli czytanych prosto z plików (przeglądarka, przykłady). Gry nadal linkują więc część ozz do budowania; zostawienie jej tylko wypiekaczowi wymagałoby podziału biblioteki animacji na dwie.
- `Play(clip, fade_seconds, loop)` zaczyna klip od początku. To, co grało, wygasa przez `fade_seconds`, a nowy klip w tym czasie narasta, więc chód przechodzi w bieg bez szarpnięcia. Klip zapętlony zaczyna się od nowa po końcu; niezapętlony zatrzymuje się na ostatniej chwili (`IsFinished()`).
- `SetSpeed(2.0f)` gra dwa razy szybciej; `GetClip()`, `GetTime()` mówią, gdzie jest odtwarzacz.
- Czas rusza się tylko w `Update`, o podaną wartość. Przy stałym ticku odtwarzanie jest przez to powtarzalne: te same kroki dają macierze stawów identyczne co do bitu (sprawdzane testem).
- Pracę wykonuje biblioteka ozz-animation (MIT); nic z niej nie wystaje poza `animation.cpp`. Klipy są przechowywane skompresowane, więc poza wraca z błędem ułamka milimetra na metr. Między dwoma kluczami obroty są mieszane po prostej i normalizowane, co wystarcza przy kluczach tak gęstych, jak w plikach animacji.
- Klip, który nie wymienia stawu albo jednej z jego trzech części, zostawia tę część w pozie spoczynkowej.
- Przeglądarka: `model_viewer PLIK OBRÓT POCHYLENIE ODLEGŁOŚĆ KLIP` gra wskazany klip; C przełącza na następny. Z `--headless --ticks N` obraz powstaje N sześćdziesiątych sekundy od początku klipu.
- `forest_scene` ma na polanie cztery lisy (dwa krążące, jeden stojący, jeden wyjący). Używa realistycznego lisa z `poligon-assets/source/sketchfab/fox_realistic_demo` (FBX, 103 stawy), jeśli tam jest, a w przeciwnym razie próbki Khronosa z `poligon-assets/source/khronos/fox`.

## Od pozy do obrazu

Bez odtwarzacza pozę można ustawić ręcznie:

```cpp
std::vector<poligon::JointTransform> pose = poligon::GetRestPose(model.skeleton);
pose[joint].rotation = ...;                       // albo, później, poza próbkowana z klipu
const std::vector<glm::mat4> matrices = poligon::ComputeJointMatrices(model.skeleton, pose);
poligon::DrawModel(renderer, render_model, transform, matrices);
```

- `DrawModel(renderer, render_model, transform)` bez macierzy rysuje model ze skinningiem w pozie spoczynkowej.
- Pod `DrawModel`: `Renderer::CreateSkinnedMesh(vertices, skin, ...)` i `Renderer::DrawSkinnedMesh(mesh, transform, material, joint_matrices, joint_count)`. Macierze są kopiowane w chwili wywołania.
- Siatkę ze skinningiem ustawiają jej stawy i `transform`; węzeł, na którym wisi w pliku, jest pomijany, jak nakazuje glTF.

## W rendererze

- Siatka ze skinningiem ma drugi bufor wierzchołków ze stawami i wagami i rysuje ją `mesh_skinned.vert.hlsl`, tak samo w przebiegu sceny, głębi i cieni, więc cienie, przyciemnianie zagłębień i mgła podążają za pozą. Shadery fragmentów są te same co dla siatek sztywnych.
- Macierze stawów wszystkich takich siatek z klatki trafiają do jednego bufora na karcie, wysyłanego raz w `EndFrame`; każde rysowanie podaje tylko, gdzie zaczynają się jego macierze.
- Shader czyta ten bufor jako zwykłe bajty (`ByteAddressBuffer`). Przy `StructuredBuffer` Direct3D 12 przez SDL GPU po cichu czyta zera i siatka znika, bez żadnego komunikatu warstwy diagnostycznej.
- Odrzucanie używa pudełka, które obejmuje siatkę w bieżącej pozie: własne pudełko siatki przeniesione przez każdy staw.

## Znane ograniczenia

- Normalne siatki ze skinningiem są poprawne dla stawów, które się obracają i skalują równomiernie; nierównomierna skala stawu wymagałaby dodatkowej matematyki.
- Pudełko do odrzucania jest bezpieczne, ale hojne dla modeli, których stawy leżą daleko od siebie.
- Przejście miesza dwa klipy; przełączanie klipów szybciej, niż trwa przejście, porzuca najstarszy z małym przeskokiem.
- Jeszcze nieużywane z ozz: mieszanie częściami ciała (chód plus obrót głowy), klipy addytywne, stawianie nóg na nierównym terenie. Stan odtwarzacza nie jest jeszcze częścią świata ECS ani zrzutu stanu; dojdzie z pierwszym obiektem gry, który go posiada.
- Zwierzęta w `forest_scene` lekko się ślizgają: ich prędkość po ziemi jest ustawiona ręcznie, a nie wzięta z klipu.
