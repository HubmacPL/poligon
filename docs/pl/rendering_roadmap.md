# Renderowanie: co jeszcze zostało

Prace nad rendererem uzgodnione, ale jeszcze niezrobione. Pochodzą z porównania z Godotem
4.7 (2026-10-17); punkty od 1 do 6 tamtego porównania (cienie chmur, mapa światła nieba,
miękkie cienie, chmury budowane przez kilka klatek, wektory ruchu, mgła w kolorze nieba) są
zrobione i opisane w [rendering.md](rendering.md). Punkt zrobiony usuwa się stąd.

## 7. Mapowanie tonów AgX jako opcja

- Co: trzeci wybór `PostSettings::tone_mapping` obok ACES i braku. AgX (Troy Sobotka,
  w Blenderze od wersji 4.0, w Godocie od 4.2) nie pozwala bardzo jasnym kolorom zamienić się
  w płaski żółty, cyjan albo magentę: zachód słońca czy ogień zachowuje odcień, blednąc do
  bieli. ACES zostaje domyślne.
- Jak: krzywa dopasowana do AgX w shaderze mapowania tonów (kilka linii HLSL), z wyglądem
  „punchy” jako opcją dla większego kontrastu. Przed napisaniem sprawdzić, czy istnieje
  dopasowana wersja na liberalnej licencji (zasada 1), i sprawdzić licencję u źródła.
- Koszt: dużo poniżej 0,1 ms. Test: jasny nasycony kolor zachowuje odcień przy AgX, a test
  obrazu dla pozostałych mapowań się nie zmienia.

## 8. XeGTAO zamiast naszego przyciemniania zagłębień

- Co: XeGTAO Intela (ground-truth ambient occlusion, Jimenez 2016, w wersji Intela) zamiast
  obecnego przyciemniania zagłębień z głębi. Daje przyciemnienie dużo bliższe wzorcowi (bez
  ciemnych aureoli wokół pni, właściwa siła w narożnikach) i „wygiętą normalną”, która może
  też kierować światłem nieba.
- Licencja: MIT, sprawdzona u źródła 2026-10-17 (github.com/GameTechDev/XeGTAO, `LICENSE`:
  „MIT License, Copyright (C) 2016-2021, Intel Corporation”). Jego HLSL brać bez zmian,
  z zachowaną informacją o licencji.
- Jak: jego shadery obliczeniowe (łańcuch mipmap głębi, przebieg główny, odszumianie) jako
  potoki obliczeniowe SDL GPU; poziomy jakości przypisane do zestawów grafiki w miejsce
  `ambient_occlusion_samples`. Sprawdzić, że nie potrzebuje niczego, czego brak GTX 980
  (celuje w shader model 5 i domyślnie nie potrzebuje operacji na falach).
- Koszt: około 0,5 do 1 ms w 1080p na kartach tej klasy (z pamięci, niesprawdzone); do zmierzenia
  w czterech widokach. Test: obecne testy przyciemniania (narożnik ciemniejszy niż otwarta
  podłoga) i jednorazowe porównanie na oko obrazu widoku w lesie.

## 9. Rośliny schowane za wzgórzami pomijane (odrzucanie zasłoniętego przez teren)

- Co: przed rysowaniem pominąć komórki roślin i impostory, które wzgórze zasłania przed
  kamerą. Na pofałdowanym terenie spora część lasu za najbliższym grzbietem jest dziś
  rysowana, a potem zakryta.
- Jak: kod własny (nie ma biblioteki do problemu tego kształtu): horyzont terenu widziany
  z kamery, liczony z zgrubnej mapy wysokości na procesorze (jeden kąt wzniesienia na kolumnę
  kierunków, idąc na zewnątrz), a komórka jest pomijana, gdy jej szczyt leży poniżej tego
  horyzontu we wszystkich kierunkach, które obejmuje. Deterministyczne, bez odczytu z karty
  graficznej. Przebiegi cieni bez zmian (wzgórza nie zasłaniają rzucających cień przed
  słońcem).
- Koszt: dużo poniżej 0,5 ms na procesorze; zysk zależy od widoku (największy na brzegu
  i we wstecznym). Test: komórka za ścianą terenu nie jest rysowana (`FrameStats`), ta sama
  po bliższej stronie jest.

## 10. Skalowanie FSR 2 (później)

- Co: FidelityFX Super Resolution 2 AMD w miejsce skalowania w naszym TAA przy skali
  rysowania poniżej 1: ostrzejsze szczegóły i mniej smug przy tym samym koszcie na piksel,
  co pozwala zestawom low i medium rysować mniej pikseli.
- Licencja: MIT, sprawdzona u źródła 2026-10-17 (github.com/GPUOpen-Effects/FidelityFX-FSR2,
  `LICENSE.txt`). Użyte byłyby tylko jego shadery HLSL, jako potoki obliczeniowe SDL GPU, nie
  jego warstwy Direct3D 12 czy Vulkan.
- Potrzebuje: wektorów ruchu (zrobione, punkt 5), obrazu głębi, maski reaktywnej dla cząstek
  i wody (do napisania), przesunięć kamery o ułamek piksela (zrobione). Przed startem
  sprawdzić, że jego shadery budują się bez 16-bitowych liczb i operacji na falach, które
  GTX 980 obsługuje słabo albo wcale, i jak pasują do zasad wiązania zasobów SDL GPU.
- Koszt: około 1 do 2 ms w 1080p na kartach tej klasy (z pamięci, niesprawdzone); musi pokonać nasze TAA ze
  skalowaniem przy tej samej jakości obrazu, zmierzone w czterech widokach. Test: testy TAA
  uruchomione z FSR 2 na jego miejscu.
