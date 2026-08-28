# SanHands

Mod ASI para GTA San Andreas 1.0 US que sustituye visualmente las manos rígidas
de los peds por las manos articuladas usadas por las animaciones de pandillas.

## Qué hace

- Usa los modelos nativos `shandl`, `shandr`, `fhandl` y `fhandr`.
- Conserva intacta la geometría de las manos y clona la geometría del ped por
  instancia. Localiza el último anillo visible de cada antebrazo y mueve sólo
  esos vértices del ped hasta el contorno proximal de la mano. Mantiene los
  triángulos, materiales, UV, pesos y huesos originales del ped: no añade un
  manguito, una tira intermedia ni un tercer objeto. La geometría original se
  restaura al retirar las manos o cambiar el modelo del ped.
- Carga `GangHands.txd` como un diccionario propio y aplica `hands_black` a los
  modelos marcados por el juego como raza negra; para las demás razas emplea
  `hands_white`. Así la mano ya no toma zonas incorrectas del atlas de ropa y
  piel de cada ped.
- Cada mano conserva sus 17 huesos originales: antebrazo, palma y 15 huesos de
  dedos.
- Construye `handpose.ifp` con una pose anatómica medida sobre las mallas
  originales: el índice se flexiona junto al dedo medio y el pulgar cruza por
  encima del índice y el medio. Mantiene rotaciones específicas para cada lado,
  porque reflejar directamente sus cuaterniones no produce una mano simétrica.
- La pose de reposo queda cerca de la clave abierta y transiciona a puño al
  detectar golpes o combate.
- Al detectar la asociación `FUCKU` de `ped.ifp`, mantiene la mano derecha en
  puño, extiende únicamente el dedo medio y copia la matriz animada de la muñeca
  original para conservar exactamente su supinación. La detección cubre el ID,
  el nombre de la asociación y `TASK_SIMPLE_SHAKE_FIST`. La entrada y salida
  interpolan los dedos, la posición y la rotación de la muñeca con una curva
  suave, evitando cambios instantáneos de pose.
- Replica el flujo de `CTaskSimplePlayHandSignalAnim` reconstruido en
  `gta-reversed`: crea cada `CHandObject`, lo añade a `CWorld` y asocia la
  animación de dedos con `CAnimManager::AddAnimation`.
- Deja que los métodos nativos `CHandObject::ProcessControl` y
  `CHandObject::PreRender` sigan el antebrazo animado y sustituyan el subárbol de
  la mano rígida. No aplica correcciones externas a las matrices de la muñeca.
- Normaliza también la inclinación de la palma dentro de las secuencias nativas
  `LHGsign`/`RHGsign` y sustituye su meseta por el mismo puño completo. Así, las
  pistas cortas que originalmente dejan el índice o el pulgar extendidos no
  vuelven a abrir la mano.
- Cede temporalmente el render a los objetos nativos cuando un ped ejecuta un
  gesto de pandilla, evitando manos duplicadas.
- Conserva el desplazamiento nativo entre muñeca y palma y vuelve a anclar el
  modelo del arma a la mano articulada justo antes de renderizarlo. Esto evita
  que el arma herede la matriz de escala cero usada para ocultar la mano rígida;
  también cubre la segunda arma de las pistolas duales y respeta el anclaje
  especial del paracaídas.
- Detecta `InertiaBox.asi` sin requerir una dependencia de enlace. Durante un
  ragdoll captura las matrices de muñeca que Inertia3D publica en el render del
  ped y dibuja las manos articuladas dentro de ese mismo pase y cuadro. Los
  objetos de mano dejan de renderizarse por separado mientras dura el ragdoll,
  por lo que ya no arrastran una muestra anterior de la muñeca durante los
  movimientos rápidos. Los dedos conservan su pose relativa. La captura se hace
  en la llamada final a `RpClumpRender`, después de los hooks de ambos ASI: allí
  también se vuelven a anular las ramas de las manos rígidas que Inertia3D acaba
  de reconstruir, evitando la superposición durante el ragdoll.
- Mantiene un límite configurable de peds para no agotar el pool de objetos.

Los DFF y `ghands.ifp` se extraen y validan en la instalación local con
`rwfury`; los cuatro DFF se copian sin alterar sus vértices originales. El
repositorio no redistribuye activos del juego.

## Compilar e instalar

Desde PowerShell:

```powershell
.\build.ps1
```

El script usa:

- MinGW de `C:\msys64\mingw32` (el juego es de 32 bits).
- plugin-sdk de `C:\Users\Digon\Documents\Fuentes\plugin-sdk-master`.
- rwfury de `C:\Users\Digon\Documents\Fuentes\rwfury-master`.
- instalación en `C:\juegos\Grand Theft Auto San Andreas\modloader\hands`.

## Configuración

`SanHands.ini` permite activar jugador/NPC, limitar distancia y cantidad de peds,
y ajustar por separado las transiciones de reposo/puño y `FUCKU`. Los cambios se
leen al iniciar una partida.

## Compatibilidad

El plugin apunta a GTA San Andreas 1.0 US, igual que la configuración
`PLUGIN_SGV_10US` de plugin-sdk. Necesita un ASI Loader y Mod Loader funcionales.
