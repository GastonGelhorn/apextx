# ApexTX: árbol completo de menús para revisar con otra IA

Instantánea del código local: 15 de septiembre de 2026. Revisión de menú estable.

## Contexto

- Firmware de la emisora FlySky Noble NB4 para coches RC.
- Repositorio: https://github.com/GastonGelhorn/apextx
- Proyecto local: /Users/gaston/code/noble_firmware.
- Pantalla táctil vertical 320 × 480 y horizontal 480 × 320; navegación física.
- Inglés predeterminado y español completo en los flujos de ApexTX.
- Este documento describe el código local, no necesariamente la versión publicada ni instalada.
- Incluye destinos, pestañas y acciones relevantes; no enumera cada valor numérico, archivo, sensor, widget o campo de los editores avanzados.
- Los nombres internos de algunos controles se resumen descriptivamente.
- [mismo editor] no significa configuración duplicada: comparte ruta, implementación y datos.
- [condicional] depende de hardware, funciones activadas, enlace o archivos.
- [confirmación] precede a una operación que cambia o borra datos.

## Árbol completo

El Menú permanece completo aunque se personalice Acceso rápido.

```text
INICIO — ApexTX Racing
│
├── ACCESO RÁPIDO — hasta ocho atajos globales
│   ├── 1. Dirección                    → Menú / Dirección
│   ├── 2. Gas y freno                  → Menú / Gas y freno
│   ├── 3. Historial de mangas          → Menú / Carrera / Historial de mangas
│   ├── 4. Trims                        → Menú / Mandos / Trims
│   ├── 5. Crono y vueltas              → Menú / Carrera / Crono y vueltas
│   ├── 6. Vista de telemetría          → Menú / Telemetría / Vista de telemetría
│   ├── 7. Boxes                        → Menú / Carrera / Boxes
│   ├── 8. Monitor de entradas/salidas   → Menú / Mandos / Monitor de entradas/salidas
│   └── Configurar acceso rápido        → Menú / Pantalla e interfaz / mismo editor
│
└── MENÚ — ubicación canónica y permanente
    │
    ├── DIRECCIÓN [un editor con pestañas; sin menú intermedio]
    │   ├── Recorrido
    │   ├── Curva
    │   │   └── Elegir / crear / editar curva [contextual]
    │   ├── Centro
    │   └── Velocidad
    │
    ├── GAS Y FRENO [un editor con pestañas; sin menú intermedio]
    │   ├── Recorrido
    │   ├── Curva
    │   │   └── Elegir / crear / editar curvas de gas y freno [contextual]
    │   ├── Freno — límite, arrastre y ABS
    │   └── Motor
    │       ├── Tipo de vehículo
    │       └── Ralentí elevado y corte de motor [condicional]
    │
    ├── COCHE ACTUAL
    │   ├── Datos del coche
    │   │   ├── Nombre
    │   │   └── Etiquetas
    │   ├── Chequeos de arranque
    │   │   ├── Mostrar lista de comprobación
    │   │   ├── Lista de comprobación interactiva
    │   │   ├── Aviso de acelerador
    │   │   ├── Posición de arranque personalizada [condicional]
    │   │   ├── Posiciones esperadas de interruptores [condicional]
    │   │   └── Posiciones de mandos analógicos [condicional]
    │   │       └── Apagado / manual / automático y mandos incluidos
    │   ├── Preajustes del coche
    │   │   ├── Explicación del preajuste eléctrico
    │   │   ├── Aplicar preajuste eléctrico [confirmación]
    │   │   ├── Explicación del preajuste nitro
    │   │   └── Aplicar preajuste nitro [confirmación]
    │   ├── Notas [requiere archivo de notas del coche]
    │   └── Configuración avanzada
    │       ├── Funciones habilitadas
    │       ├── Filtro y avisos de centro
    │       ├── Entradas
    │       │   └── Elegir / crear / editar curva [contextual]
    │       ├── Mezclas
    │       │   └── Elegir / crear / editar curva [contextual]
    │       ├── Salidas — límites, centro, sentido y otros ajustes por canal
    │       ├── Lógica [requiere función habilitada]
    │       ├── Funciones especiales del modelo [requiere función habilitada]
    │       ├── Variables del modelo (GVAR)
    │       │   ├── Activar para este coche
    │       │   └── Abrir editor de variables
    │       └── Scripts [requiere función habilitada y archivos adecuados]
    │
    ├── MANDOS
    │   ├── Trims — paso y límites de las correcciones de conducción
    │   ├── Funciones de botones
    │   │   ├── Elegir familia: original/desactivado, navegación, crono/vueltas, trims
    │   │   ├── Elegir acción y gesto [según función]
    │   │   ├── Asignar pulsando un mando físico
    │   │   └── Ver por mando
    │   │       ├── Botones de empuñadura, volante y trims
    │   │       ├── Editar función de un mando / guardar
    │   │       ├── Pareja SW2/SW3: navegación, trim o restaurar
    │   │       └── Otros controles
    │   │           ├── Volante y gatillo [nombres/tipos de hardware]
    │   │           ├── VR1-L / VR1-R [hardware]
    │   │           ├── Asignación de canales [mismo editor]
    │   │           ├── Mezclas [mismo editor avanzado]
    │   │           └── Interruptores [hardware]
    │   ├── Asignación de canales
    │   │   ├── Canal del receptor para dirección + invertir
    │   │   └── Canal del receptor para gas + invertir
    │   ├── Respuesta de interruptores
    │   │   └── Retardo de interruptores [global; encoder en hardware compatible]
    │   └── Monitor de entradas/salidas [consulta en directo]
    │
    ├── RECEPTOR Y RF [un editor]
    │   ├── Modo RF
    │   └── Ajustes del módulo/receptor [según modo y enlace]
    │       ├── Estado y tipo de módulo
    │       ├── Opciones de salida del módulo
    │       ├── Sensores y canales
    │       ├── Failsafe
    │       ├── Datos del receptor
    │       ├── Enlazar receptor
    │       └── Prueba de alcance
    │
    ├── CARRERA
    │   ├── Crono y vueltas
    │   │   ├── Iniciar / finalizar / nueva manga [según estado]
    │   │   ├── Marcar vuelta
    │   │   ├── Deshacer vuelta
    │   │   ├── Ajustes → Ajustes de carrera [mismo editor]
    │   │   └── Resultado / historial [según estado]
    │   │       ├── Ver resultado → detalle exacto de esta manga en Historial
    │   │       ├── Guardando / resultado pendiente
    │   │       ├── Reintentar guardado fallido
    │   │       └── Ver registro de mangas
    │   ├── Boxes
    │   │   ├── Duración del depósito / pack — usa Temporizador 2
    │   │   └── Repostado / pack nuevo
    │   ├── Historial de mangas
    │   │   ├── Lista de mangas guardadas
    │   │   ├── Abrir manga → detalle y tiempos por vuelta
    │   │   ├── Más antiguas / más recientes [condicional]
    │   │   └── Actualizar / reintentar [condicional]
    │   ├── Estadísticas
    │   │   ├── Tiempo de sesión y de uso de batería de la emisora
    │   │   ├── Tiempo de gas activo y ponderado
    │   │   ├── Tres temporizadores del coche
    │   │   ├── Gráfica de gas
    │   │   └── Reiniciar contadores de uso y gráfica [confirmación; no borra historial]
    │   ├── Ajustes de carrera
    │   │   ├── Mando para marcar vuelta
    │   │   ├── Anunciar vuelta
    │   │   └── Objetivo de vueltas — 0 significa sin límite
    │   ├── Temporizadores
    │   │   ├── Temporizador 1 → editor; Volver recupera esta lista
    │   │   ├── Temporizador 2 → editor
    │   │   ├── Temporizador 3 → editor
    │   │   └── Seguimiento del acelerador
    │   ├── ── separador visual, no submenú ──
    │   └── Reinicios de sesión
    │       ├── Reiniciar temporizador 1 [confirmación]
    │       ├── Reiniciar temporizador 2 [confirmación]
    │       ├── Reiniciar temporizador 3 [confirmación]
    │       └── Reiniciar telemetría [confirmación]
    │
    ├── TELEMETRÍA
    │   ├── Vista de telemetría
    │   │   ├── Seleccionar sensor y consultar gráfica/lectura
    │   │   ├── Más sensores / primeros sensores [condicional]
    │   │   ├── Sensores → editor de sensores [mismo editor]
    │   │   └── Archivos → explorador [mismo editor]
    │   ├── Sensores [requiere telemetría habilitada]
    │   └── Alertas de telemetría [requiere telemetría habilitada]
    │
    ├── MIS COCHES
    │   ├── Lista de coches
    │   │   ├── Coches, etiquetas y ordenación
    │   │   ├── Crear coche / crear etiqueta
    │   │   └── Acciones sobre un coche [contextual]
    │   │       ├── Seleccionar [según coche/configuración]
    │   │       ├── Duplicar
    │   │       ├── Editar etiquetas
    │   │       ├── Guardar como plantilla [solo coche activo]
    │   │       └── Borrar [solo coche no activo; confirmación]
    │   └── Plantillas
    │       ├── Listar plantillas y consultar información
    │       ├── Crear coche desde plantilla [confirmación]
    │       ├── Guardar coche actual como plantilla personal
    │       │   └── Confirmar sobrescritura si existe
    │       └── Borrar plantilla personal [confirmación]
    │
    ├── PANTALLA E INTERFAZ
    │   ├── Brillo [toda la emisora]
    │   ├── Apariencia [toda la emisora]
    │   │   ├── Paleta
    │   │   ├── Acento
    │   │   ├── Orientación — vertical/horizontal
    │   │   └── Temas externos [sin repetir paletas integradas]
    │   ├── Diseño de pantallas [coche actual]
    │   │   ├── Inicio → su editor
    │   │   ├── Pantallas adicionales → editor de la elegida
    │   │   └── Añadir pantalla → editor de la nueva
    │   │
    │   │   Editor común de cada pantalla:
    │   │   ├── Seleccionar layout
    │   │   ├── Configurar widgets por zona
    │   │   │   ├── Elegir / sustituir widget
    │   │   │   ├── Configurar widget
    │   │   │   └── Vaciar zona
    │   │   ├── Opciones específicas del layout
    │   │   ├── Restaurar diseño ApexTX [confirmación; solo esta pantalla]
    │   │   └── Eliminar pantalla [solo adicionales]
    │   │
    │   │   ApexTX Racing: cuatro zonas
    │   │   ├── Dirección
    │   │   ├── Gas y freno
    │   │   ├── Cronómetro
    │   │   └── Estadísticas
    │   │
    │   ├── Barra superior → configurar widgets [coche actual]
    │   ├── Teclas y navegación [coche actual]
    │   │   ├── Anterior, Siguiente, Seleccionar, Volver, Menú o Acceso rápido
    │   │   ├── Asignar pulsando un botón físico
    │   │   └── Ver por mando [acceso contextual al editor completo de asignaciones]
    │   ├── Configurar acceso rápido [toda la emisora]
    │   │   ├── Elegir una de las ocho posiciones
    │   │   ├── Elegir/sustituir destino — sin duplicados dentro de Acceso rápido
    │   │   ├── Mover arriba/abajo
    │   │   ├── Quitar — permite huecos vacíos
    │   │   └── Restaurar ocho predeterminados [confirmación]
    │   └── Luces [toda la emisora]
    │       ├── Apagado / color fijo / respiración / estado de batería
    │       ├── Color [fijo o respiración]
    │       └── Explicación del indicador de carga que sustituye temporalmente el color
    │
    ├── SONIDO Y AVISOS [toda la emisora]
    │   ├── Alertas de la emisora
    │   ├── Sonido
    │   └── Vibración
    │
    └── SISTEMA
        ├── USB
        ├── Bluetooth [solo compilaciones compatibles; ausente en esta NB4]
        ├── Copia manual
        │   ├── Instrucciones de copia y restauración por USB Almacenamiento
        │   └── Abrir archivos [mismo explorador; no crea una copia automáticamente]
        ├── Restablecer ajustes
        │   ├── Ajustes de la emisora [confirmación]
        │   ├── Este coche [confirmación]
        │   └── Emisora y este coche [confirmación]
        ├── Preferencias generales
        ├── Energía
        ├── Hardware
        ├── Calibración
        ├── Almacenamiento
        │   ├── Explorador de archivos
        │   └── Crear sistema de archivos [solo si falta; confirmación]
        ├── Actualizar [confirmación → modo Update]
        ├── Ubicación [esta NB4 sin RTC]
        ├── Diagnósticos
        ├── Acerca de
        └── Ayuda
            ├── Categorías
            ├── Opciones y explicación
            └── Abrir ajuste correspondiente [mismo editor]
```

## Reglas que deben mantenerse

1. Acceso rápido es un atajo, no una mudanza. Nunca oculta funciones ni categorías del Menú. El retorno respeta la entrada y conserva posición/foco.
2. Dirección y Gas/freno abren el mismo editor desde Menú, Acceso rápido o un acceso contextual. Las pestañas no crean menús intermedios.
3. Los editores tienen ? en la cabecera. Las cuadrículas de Menú, Acceso rápido y categorías no muestran ese botón: solo sirven para elegir un destino. El índice sigue en Sistema → Ayuda. Cerrar ayuda vuelve al editor existente. Abrir un ajuste desde el índice elimina antes el recorrido de ayuda para no dejarlo en el historial de Volver.
4. Los iconos decorativos y el fondo de los diálogos no son botones ocultos de Volver.
5. La cabecera muestra el alcance: Toda la emisora, Coche: nombre, Colección de coches o Datos de emisora y coche. Los subeditores de layouts/widgets heredan el alcance del coche.
6. Teclas y navegación es una vista limitada a acciones de interfaz del mismo editor de botones. Sus asignaciones se guardan por coche y sustituyen la función previa del botón elegido; no son los atajos globales de Acceso rápido.
7. Canales y Salidas no se fusionaron: Asignación de canales escoge qué salida mueve cada eje; Salidas amplía los ajustes del servo. La inversión utiliza los mismos datos en ambas vistas.
8. Luces controla aspecto de LED y batería/carga, no el editor de avisos sonoros o telemetría. Por eso está en Pantalla e interfaz.
9. Estadísticas no es un resumen de manga. Explica tiempos de uso globales y de sesión, gas, temporizadores y gráfica. Ver resultado utiliza el registro exacto de Historial, sin pantalla Resumen paralela.
10. Preajustes modifica determinados ajustes de conducción; Plantillas crea un coche; Restaurar diseño sustituye solo layout/widgets de la pantalla elegida. No deben confundirse con Restablecer ajustes.
11. Copia manual no promete un asistente automático. Restablecer ajustes está separado y conserva otros coches y calibración.
12. Curvas pertenece al coche/modelo; entradas y mezclas pueden compartirlas. No existe Biblioteca de curvas como menú independiente ni atajo; el editor contextual muestra usos compartidos.
13. Se conservan los ocho atajos del usuario. Boxes sustituye a Receptor únicamente en una configuración nueva o al restaurar predeterminados. Receptor sigue siendo elegible.
14. USB, navegación, configuración de atajos y Luces conservan sus IDs de ruta aunque cambien de categoría visual. El YAML de atajos sigue en versión 2, sin eliminar campos heredados.
15. Una función no soportada se omite; una función habitual desactivada (por ejemplo Sensores con telemetría desactivada) permanece identificable y explica su indisponibilidad.
16. Con un modelo incompatible siguen accesibles ayuda, actualización, recuperación y preferencias de radio; se bloquean los editores de datos del modelo.
17. La migración de Inicio respalda el modelo antes de sustituir la pantalla oculta y conserva las adicionales. Si falla el respaldo se aplaza. Esta revisión no cambia ese esquema.
18. El menú principal y Acceso rápido mantienen una cuadrícula compacta; los submenús usan opciones más anchas para evitar palabras partidas. Carrera separa visualmente los reinicios, sin añadir un nivel.

## Qué pedir a otra IA

> Revisa la arquitectura de información y la UX de este árbol real de ApexTX. Distingue el estado implementado de tus propuestas. Indica ruta, problema concreto, motivo, impacto y alternativa. No confundas un atajo al mismo editor con datos duplicados. No elimines funciones avanzadas sin revisar su contenido. Si el árbol no permite evaluar un campo o un aspecto visual, dilo. Comprueba especialmente claridad de nombres, alcance radio/coche, retorno, ayuda y separación entre uso en pista y configuración. No implementes cambios: entrega primero tus observaciones priorizadas.

## Fuentes y verificación

Fuentes locales: nb4_routes.cpp (rutas/categorías/alcance), nb4_menu_pages.cpp (atajos/diseños/ayuda/temporizadores), nb4_assignments.cpp, model_nb4_axis.cpp, model_nb4_racing.cpp, model_setup.cpp, preflight_checks.cpp, module_setup.cpp, nb4_pages.cpp, view_statistics.cpp, screen_setup.cpp y traducciones en.h/es.h.

Las instrucciones y resultados exactos de pruebas están en [VALIDATION.md](VALIDATION.md); las capturas nativas, en [GALLERY.md](GALLERY.md); las reglas de persistencia, en [MENUS.md](MENUS.md).

El simulador comprueba rutas, ayuda/retorno, idiomas, orientaciones y casos de almacenamiento. No acredita por sí solo el uso físico: quedan el recorrido táctil en la emisora, pulsadores, reinicio, cambio real de coche y comportamiento RF. Esta revisión no se ha instalado en la emisora durante su implementación.
