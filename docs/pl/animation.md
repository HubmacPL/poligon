# Animacja szkieletowa

Jak model się wygina: szkielet ze stawów, siatka, której wierzchołki za nimi podążają, i klipy mówiące, jak stawy ruszają się w czasie.

Nagłówki: `poligon/animation.h`, `poligon/model.h`, `poligon/renderer.h`. Test: `tests/skinning_render_test.cpp`.

**Stan: w toku (kamień 5).** Wczytywanie i rysowanie są gotowe. Odtwarzanie klipu i mieszanie klipów nie są jeszcze napisane.

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

## Od pozy do obrazu

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
