# Renderowanie: co jeszcze zostało

Prace nad rendererem uzgodnione, ale jeszcze niezrobione. Punkty od 1 do 6 porównania z
Godotem 4.7 (2026-10-17) oraz, z paczki 5 (2026-10-26), AgX (7), XeGTAO (8) i pomijanie
tego, co zasłaniają wzgórza (9), są zrobione i opisane w [rendering.md](rendering.md) i
[terrain.md](terrain.md). Punkt zrobiony usuwa się stąd.

## 10. Skalowanie FSR 2 (wstrzymane 2026-10-26: w obecnej postaci nie pasuje do wiązań SDL GPU)

- Co: FidelityFX Super Resolution 2 AMD w miejsce skalowania w naszym TAA przy skali
  rysowania poniżej 1: ostrzejsze szczegóły i mniej smug przy tym samym koszcie na piksel,
  co pozwala zestawom low i medium rysować mniej pikseli.
- Licencja: MIT, sprawdzona u źródła ponownie 2026-10-26
  (github.com/GPUOpen-Effects/FidelityFX-FSR2, `LICENSE.txt`, Copyright (c) 2022-2023 Advanced
  Micro Devices).
- Sprawdzone 2026-10-26: jego przebiegi budują się DXC do DXIL i SPIR-V bez 16-bitowych liczb
  (`FFX_HALF` 0) i bez operacji na falach (`SPD_NO_WAVE_OPERATIONS`), więc GTX 980 nie jest
  przeszkodą. Przeszkodą są wiązania SDL GPU: jego warstwa integracji
  (`ffx_fsr2_callbacks_hlsl.h`) próbkuje kilkanaście tekstur dwoma wspólnymi samplerami
  (`s_PointClamp`, `s_LinearClamp`), a backend Vulkan SDL GPU wiąże każdą próbkowaną teksturę
  z jej własnym samplerem, i DXC nie umie zrobić SPIR-V, w którym taki sampler jest wspólny
  (widać to było przy rozmyciu XeGTAO, `rendering.md`); jego rejestry (`t`, `s`, `u`, `b` w
  jednej przestrzeni) trzeba by też przenieść do zestawów SDL GPU. Same jego przebiegi by się
  nie zmieniły.
- Co by to kosztowało (godziny naszych sesji): własna warstwa integracji w miejsce
  `ffx_fsr2_callbacks_hlsl.h` (sampler dla każdej próbkowanej tekstury, rejestry i zestawy
  SDL GPU dla każdego przebiegu), 3 do 4; 7 przebiegów jako potoki compute z ich obrazami,
  historiami, stałymi i dwiema tablicami, które robi jego strona C++ (też MIT), 4 do 6; maska
  reaktywna pisana przez cząsteczki i wodę, 1 do 2; wektory ruchu i przesunięcia w jego
  konwencji, 1; testy (testy TAA z FSR 2 na jego miejscu), obrazy czterech widoków w medium i
  pomiary, 2 do 3. Razem około 11 do 16. Koszt na klatkę, z pamięci i niesprawdzony: około 1
  do 2 ms w 1080p na kartach tej klasy; musi pokonać nasze TAA ze skalowaniem przy tym samym
  koszcie, co na tej karcie nie jest pewne.
