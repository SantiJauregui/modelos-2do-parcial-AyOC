# 2do Parcial de Arquitectura y Organización del Computador - 2c2026

## Normas generales

- El parcial es en los laboratorios, usando las compus de los labos o sus propias compus.
- Es a libro abierto, pueden tener todo lo que se les ocurra a disposición. Esta totalmente prohibido el uso del celular, internet y chatbots.
- El internet de los laboratorios está limitado a la página del campus de la materia. 
- Quienes rindan con su computadora personal deberán conectarse al internet del labo mediante cable ethernet.
- Las resoluciones que incluyan código, pueden usar assembly o C. No es necesario que el código compile correctamente, pero debe tener un nivel de detalle adecuado para lo pedido por el ejercicio.
- Vamos a usar herramientas de detección de plagio para asegurarnos de que su entrega sea original. La entrega debe ser original para estar aprobada. El plagio en instancias de evaluación incurre sanciones que pueden incluir la suspensión académica.

> [!NOTE]
> Durante el parcial estaremos disponibles para resolver consultas de enunciado y para destrabarles si están dando vueltas mucho tiempo en algo que no forma parte de lo evaluado. NO responderemos preguntas teóricas.

## Régimen de Aprobación

En este parcial se evalúa el manejo de los mecanismos de:
- Excepciones, interrupciones de hardware y syscalls mediante Interrupciones al procesador
- Manejo de memoria mediante Segmentación y Paginación
- Permisos y cambios de nivel de privilegio
- Administración del tiempo de procesamiento y del espacio de memoria para la ejecución de tareas.

Para la Arquitectura Intel x86.
La entrega debe demostrar, en la medida exigida por el enunciado, comprensión clara de la mayoría estos mecanismos, e incomprensión de ninguno.

El parcial se aprueba con un puntaje numérico de **60 o más**.

**NOTA: Lea el enunciado del parcial hasta el final antes de comenzar a resolverlo.**

## Modalidad de Entrega

> [!WARNING]
> Se estará circulando por el aula una planilla escrita donde **deben** anotarse para que tengamos constancia de que su presencia en los laboratorios al momento del parcial.
> De no anotarse no podremos considerar su entrega como válida. La planilla, luego de circular, se encontrará en los escritorios al frente de cada laboratorio. **No se vayan sin anotarse!**

Deberán completar sus datos y desarrollar su solución en el archivo `Resolucion.md`, en formato Markdown (en el campus, pestaña [recursos](https://campus.exactas.uba.ar/course/view.php?id=1383&section=8) tienen un machete de cómo dar formato en markdown). Es importante que no utilicen otro formato ya que dificultaría la corrección. 

Si su solución utiliza código modificado del Trabajo Práctico, es importante que lo incluyan además de explicar en palabras las modificaciones (pueden copiar y pegar cualquier cosa de sus repositorios de TP). También pueden incluir imágenes si así lo desean. De tener dudas sobre qué funciones se pueden o no asumir implementadas, preguntar a los docentes durante el examen.

Una vez finalizado, completarán la entrega subiendo el archivo `Resolucion.md` **y cualquier imagen incrustada o archivo complementario** necesario al campus de la materia, en la tarea correspondiente al parcial. Pueden acercarse a nosotros para que confirmemos su entrega haya quedado registrada en el campus. 

No se olviden de anotarse en la planilla de presente también.

# Enunciado

Durante el desarrollo del anticipado videojuego Half-Fantastruco 3, hemos decidido aprovechar uno de los features más innovadores de la consola de videojuegos OrgaStation: su TPU (Tensor Processing Unit), una unidad optimizada para realizar multiplicaciones de matrices. Utilizaremos la TPU para mejorar la eficiencia de los NPCs (personajes) del juego, cuyo accionar está regido por redes neuronales. **Cada NPC** está implementado con una **tarea propia**, y ya tenemos resuelto cómo van a interactuar con la TPU.

La TPU **ya está configurada** para que, a partir del contexto del NPC, devuelva la acción a realizar. Para utilizarla, los NPCs utilizan el siguiente protocolo:

1. El NPC escribe en el **buffer de entrada** de la TPU su contexto, que es aquello que percibe: sonidos, obstáculos, la posición del jugador, etc. El buffer de entrada está en la dirección física `0xFAC000` y ocupa 64 KiB. Los NPCs escriben al buffer de entrada mediante la variable `char* virt_buf_entrada`.  
2. Cuando el NPC termina de escribir, indica mediante una syscall que queda a la espera de que la TPU termine de procesar \- esto puede tomar **varios ciclos** de clock. Esa misma syscall indica a la TPU que comience a procesar escribiendo 0xAA en el puerto de E/S `0x1E0`.  
3. La TPU procesa los datos del buffer de entrada y, al terminar, genera una **interrupción en IRQ3 del PIC2.** Mientras la TPU está procesando, el resto del sistema debe continuar su funcionamiento usual.  
4. El NPC busca el resultado de la operación en el **buffer de salida** de la TPU, que se encuentra en la dirección física `0xFAC10000` y ocupa 32bits.  
5. Finalmente el NPC traduce la salida de la TPU a una acción efectiva. La salida de la TPU es un número de 32 bits, que es traducido a una acción consultando un **mapa de decisiones** propio del NPC, que se encuentra en la dirección virtual `0x0C0FE000` y ocupa 4KiB. Cada tarea tiene su propio mapa de decisiones distinto, el cual puede **leer pero no escribir** (no queremos que los NPCs puedan modificar su comportamiento pre-programado).

Para simplificar su código, **los NPCs no tienen conciencia de que la TPU es un recurso compartido** y hacen uso de sus buffers como si fueran usuarios exclusivos de ellos. Esto es problemático para el buffer de entrada, ya que sin algún control los NPCs podrían sobrescribir o corromper el contexto ya escrito mientras la TPU lo está leyendo. Queremos construir un sistema que restrinja el acceso al recurso y la ejecución del código de los NPCs de forma inteligente para que **nuestro juego funcione sin cambiar el comportamiento de los NPCs**, que ya fue programado.

Para adaptarnos al comportamiento de los NPCs (tareas) antes descrito, hemos decidido lo siguiente: en cualquier momento dado, la **única tarea que tendrá mapeado el buffer de entrada** (y por ende la única que puede escribirlo) es aquella que la TPU **está atendiendo** o va a atender. Decimos que la tarea que tiene mapeado el buffer de entrada es **dueña** de la TPU.  
Una vez que la TPU indica que **terminó de procesar, se libera el buffer de entrada** para que otro NPC pueda comenzar a escribirlo y volverse dueño de ella.

Contamos con una variable global `task_id idx_TPU_owner` que tiene valor 0 cuando nadie está utilizando la TPU, o un identificador correspondiente a la tarea cuando está en uso (con qué valores se identifican las tareas queda a su criterio). Esta variable **solo puede ser accedida desde nivel 0**, no por los NPCs directamente.

Con estas decisiones de diseño ya tomadas, se pide **diseñar el resto del funcionamiento del sistema para que las tareas puedan hacer uso ordenado y exclusivo de la TPU.**

**Nota:** se puede asumir que las respuestas de la TPU son suficientemente lentas para que una tarea llegue a leer el buffer de salida aunque ya se haya comenzado a atender un siguiente pedido de procesamiento.

## Ejercicios

Partiendo de la versión final del sistema operativo construido durante la realización del taller de system programming, pensar qué modificaciones serían necesarias para que el sistema cumpla con la funcionalidad pedida y resolver los siguientes ejercicios:

### Ejercicio 1 (10 puntos)

> [!WARNING]
> Este ejercicio pide **dos** respuestas.

**Dar el código** de una función `void make_task_TPU_owner(task_id new_owner)` la cual, dado un identificador de tarea/NPC, vuelva a esta tarea la nueva dueña de la TPU y le conceda acceso **exclusivo** al buffer de entrada. 

* Esta función exige como precondición que la TPU no tenga dueño actual al momento de ser llamada (es decir, está garantizado que al momento de ejecutar su código la TPU no tiene un dueño previo).   
* La función **debe** modificar la variable `task_id idx_TPU_owner`, que solo puede ser accedida por el kernel.

La dirección `char* virt_buf_entrada` tiene un valor definido por el sistema y conocido por las tareas.**Proponer un valor adecuado** para `char* virt_buf_entrada`.

### Ejercicio 2 (20 puntos)

Responder las siguientes preguntas **para el momento en que la TPU nunca tuvo dueño aún**:

1. Explicar: ¿en qué momento de la ejecución del sistema se llama a la función `make_task_TPU_owner` por primera vez? Mostrar el código completo de la rutina donde se llama esta función.   
   * Recordar que `make_task_TPU_owner` requiere que la TPU no tenga dueño al momento de llamarla. En este ejercicio **es válido** dar una solución que sólo funcione **cuando la TPU no está en uso** - se evaluará modificar la solución de este ejercicio para manejar el caso en que la TPU actualmente tiene dueño durante un ejercicio posterior.  
2. Indicar quién realiza el llamado a la rutina de la cual se mostró el código

### Ejercicio 3  (20 puntos)

Programar una `syscall` a ser llamada por la tarea dueña de la TPU cuando ya terminó de escribir el buffer de entrada. Indicar en detalle todas las modificaciones a estructuras del sistema necesarias para dar soporte a dicha syscall. La syscall debe:

* Indicar a la TPU que comience el procesamiento de los valores del buffer de entrada (mediante el puerto de E/S)  
* No devolver el control al proceso de usuario que la llamó hasta que la TPU no haya indicado la finalización del procesamiento mediante una interrupción

### Ejercicio 4 (15 puntos)

Dar el código de una función `void free_TPU()` que libere el uso de la TPU y del buffer de entrada. Tener en cuenta que esta función debería ser llamada en la respuesta de algún otro ejercicio del parcial.

### Ejercicio 5 (10 puntos)

Mostrar el código de la rutina de atención que atiende la interrupción de la TPU. ¿Qué rol cumple dentro de su solución?  
Recordar que se debe liberar el buffer de entrada cuando la TPU indica que finalizó de procesar los valores de entrada actuales.

### Ejercicio 6 (15 puntos)

Explicar cómo maneja el sistema la situación en que una tarea no dueña de la TPU intenta escribir el buffer de entrada mientras la TPU está en uso. Si es necesario, explicar cómo modificaría el código propuesto en los puntos anteriores para soportar este caso. La explicación debe:

* Responder cómo se comporta una tarea no dueña mientras espera que se libere la TPU  
* Responder cómo se realiza el traspaso de propiedad de la TPU entre tareas  
* Responder la siguiente pregunta: si había una tarea esperando que se liberara la TPU y se vuelve dueña de la TPU \- comienza a escribir el buffer de entrada inmediatamente?

### Ejercicio 7 (10 puntos)

* Explicar cómo se logra que el acceso al buffer de entrada efectivamente sea exclusivo.   
* Explicar cómo se garantiza que las tareas tengan acceso al buffer de salida cuando lo necesiten. **Nota:** no hay restricciones de exclusividad sobre el buffer de salida.  
* Explicar qué modificaciones hay que hacer al sistema para garantizar que todas las tareas tengan su propio mapa de decisiones en la dirección virtual `0x0C0FE000` , cómo se logra que las tareas puedan leerlo pero no escribirlo, y cómo se logra que los mapas de decisiones de cada tarea sean independientes entre sí.