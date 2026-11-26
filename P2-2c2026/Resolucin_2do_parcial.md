# Resolución

# Ejercicio 1

## Consideraciones

- Como la función tiene precondición que la TPU no tenga dueño actual al momento de ser llamada, no es necesario que implemente un lógica de chequeo de si la TPU tiene dueño o no.

- Como la función debe modificar la variable `idx_TPU_owner` que solo ve el kernel, esta misma funcion será llamada por código de Ring 0.

- Respecto de conceder acceso al buffer de entrada:
  la función se debe encargar de mappear, en el esquema de paginación de la tarea TPU_owner, es decir, con el CR3 de la tarea TPU_owner, la dirección VIRTUAL en `char* virt_buf_entrada` a la dirección FISICA `0xFAC000` + 64KiB, es decir, se deben mapear 64/4 = 16 páginas de 4KiB (desde `0xFAC000` hasta `0xFAC000` + `0xFA00` - 1 = `0XFBB9FF` deben poder ser accesibles)

- Eventualmente, cuando se deje de ser propietario, se debe desmapear para que no tenga más acceso.

- Respecto del valor `virt_buf_entrada`:
  Como es un valor usado por todas las tarea (es una dirección virtual), el criterio de selección tiene que ser un rango en el que no haya nada mapeado según el mapa de memoria del TP.
  Ademas si bien es una posición a demanda, la pagina ON-DEMAND del TP es de solo 4 KiB y no me entra los 64KiB que necesito.
  Un valor apropiado para `virt_buf_entrada` es `0x400000`.

## Código

```c
#define DIREC_FIS_BUFF_ENTRADA 0xFAC000

// virt_buf_entrada = 0x400000
void make_task_TPU_owner(task_id new_owner) {
    idx_TPU_owner = new_owner;    // nuevo dueño de TPU
 
    //como no sé si el CR3 actual es necesarimente el dueño de la TPU
    uint32_t rc3_owner = tasks[new_owner].cr3;
    //son 16 paginas
    for(int i = 0; i < 16; i++) {
        //mapeo pagina una a una
        mmu_map_page(rc3_owner, virt_buf_entrada + (4096*i), DIREC_FIS_BUFF_ENTRADA + (4096*i), MMU_W | MMU_U);
    }
}
```

# Ejercicio 2

## Consdieraciones

- Voy a saber que están queriendo usar la TPU cuando intenten escribir en alguno de bytes de la direccion virtual entre `0x400000` y `0x400000` + 64KiB.

- Cuando ocurra esto, saltará la excepcion Page Fault.

- Voy a tener que manejar dos situaciones:

  - Si la TPU no tiene dueño: escenario maravilloso, pues llamo directamente a `make_task_TPU_owner` y listo.

  - Si la TPU tiene dueño: lamentablemente voy a hacer esperar a la tarea, para esto la voy a poner en modo `ESPERANDO_TPU` y cambio a la siguiente tarea disponible a ejecutar. Eventualmente, este modo me va a servir para rastrear las tareas que estaban esperando el uso de la TPU para irles dando lugar. (linkeado a ejercicio 6)

## Punto 1

EL sistema llama a `make_task_TPU_owner` por primera vez en el contexto de un `page_fault_handler` llamada por la interrupción que maneja estas excepciones que es la `_isr14`.

Puesto que es cuando un NPC está intentando escribir en el buffer de entrada, es decir, de empezar a hacer uso de la TPU.

Además aprovecho justamente que esas paginas esten inicialmente SIN mapear para poder darme cuenta de esto.

```c
bool page_fault_handler(vaddr_t virt) {
    print("Atendiendo page fault...", 0, 0, C_FG_WHITE | C_BG_BLACK);

    // Chequeamos si el acceso fue dentro del rango admitido ON DEMAND
    if (virt >= ON_DEMAND_MEM_START_VIRTUAL && virt <= ON_DEMAND_MEM_END_VIRTUAL) {
        // Mapeamos la página física de on-demand (0x03000000) en el CR3 actual
        mmu_map_page(rcr3(), ON_DEMAND_MEM_START_VIRTUAL, ON_DEMAND_MEM_START_PHYSICAL, MMU_P | MMU_W | MMU_U);
        
        return true; // Se pudo atender el Page Fault exitosamente
    }
    
    // Chequeamos si el acceso fue dentro del rango admitido BUFFER ENTRADA
    if (virt >= virt_buf_entrada && virt <= virt_buf_entrada + 64000 - 1) {
        //Si NO tiene dueño
        if(idx_TPU_owner == 0) {
            make_task_TPU_owner(current_task);
        }
        else {
            tasks[current_task].sched_state = ESPERANDO_TPU;
        }
        
        return true; // Se pudo atender el Page Fault exitosamente
    }

    // Si fue fuera del área on-demand, no se puede atender
    return false;
}
```

Ademas agrego estos dos nuevos estados a `sched_state`:

- `ESPERANDO_TPU`: para cuando una tarea quiere hacer uso de la TPU pero está siendo usada.

- `USANDO_TPU`: para la tarea que termino de escribir el buffer de entrada y  le dijo a la TPU que empiece a procesar (se usa en otro ejercicio).

```c
typedef enum {
  TASK_SLOT_FREE,
  TASK_RUNNABLE,
  TASK_PAUSED,
  ESPERANDO_TPU,
  USANDO_TPU
} task_state_t;
```

## Punto 2

Quien realiza el llamado a `page_fault_handler` es la `_isr14` que es la encargada de manejar la expeciones de Page Fault.

Pero como eventualmente podemos estar en una tarea que cambio el estado a ESPERANDO_TPU, entonces tenemos que cambiar la tarea que estamos ejecutando a la siguiente disponible a ejecutar.

Es decision intencional de este diseño que, para atajar los casos antes mencionados, si justo la tarea en cuestion es la que tiene que ser la dueña, igual se va a cambiar a la siguiente tarea a ejecutar, esto es aceptable puesto que la tarea dueña va a ejecutarse igual porque sigue en estado `TASK_RUNNABLE`.

```nasm
;; Rutina de atención de Page Fault ISRE 14 ; Descomentar esta rutina en la parte 3 (paginación)
;; -------------------------------------------------------------------------- ;;
global _isr14

_isr14:
    ; 1. Guardamos el estado de los registros generales
    pushad

    ; 2. Leemos CR2 (dirección virtual del fallo) y lo pasamos como argumento a C
    mov eax, cr2
    push eax
    call page_fault_handler
    add esp, 4                  ; Limpiamos el parámetro de la pila

    ; 3. Verificamos el valor de retorno en EAX (true = 1, false = 0)
    cmp eax, 0
    je .no_atendido             ; Si no se pudo atender, saltamos a manejar el error

    ; 4. Si se atendió correctamente:
    
    ; CAMBIO DE TAREA!!!
    ; Copiar el contexto completo a tasks[current_task]
    call save_current_task_context ;Esta implementación toma los valores de los registros de la pila, que están ahí gracias al pushad del comienzo
    popad                  ; Desapila los 32 bytes del pushad (EAX..EDI)
    call sched_next_task
    call switch_to_usermode

.no_atendido:
    popad                       ; Restauramos registros
    add esp, 4                  ; Descartamos el código de error
    jmp $                       ; Colgamos el sistema si fue un acceso no permitido
```

# Ejercicio 3

## Consideraciones

- Debo modificar la IDT (en `idt.c`) para agregar una nueva ENTRY que pueda ser llamada desde nivel 3 (usuario).
- Declarar e implementar la rutina en `isr.asm`, que se va a encargar de llamar a la funcion en C y de hacer el cambio de tarea.
- Implemento la funcion en C que se va a encargar de actualizar la estructura de tareas para que pase a modo `USANDO_TPU` y ademas va a enviar a la TPU que inicie su procesamiento.
- El cambio de tarea es porque la tarea que invoco la syscall tiene que esperar hasta que la TPU termine de procesar.

## Código

Agrego a la IDT:

```c
void idt_init() {
    //...
    IDT_ENTRY3(90); //Importante que sea de nivel 3 puesto que quien la va a llamar sera de nivel usuario
    //...
}
```

En isr.asm:

```nasm
global _isr90
extern procesa_TPU

_isr90:
   pushad
   
   call procesa_TPU
   
    ; CAMBIO DE TAREA!!!
    ; Copiar el contexto completo a tasks[current_task]
    call save_current_task_context ;Esta implementación toma los valores de los registros de la pila, que están ahí gracias al pushad del comienzo
    popad                  ; Desapila los 32 bytes del pushad (EAX..EDI)
    add esp, 20            ; Desapila los 20 bytes del hardware (SS, ESP, EFLAGS, CS, EIP)    
    call sched_next_task
    call switch_to_usermode
```

En isr.h:

```c
void _isr90();
```

Luego, procesa_TPU es la función en C encargada de avisar a la TPU que inicie el procesamiento e cambiar el estado de la tarea.

```c
#define TPU_PORT 0x1E0

void procesa_TPU(void) {
    tasks[current_task].sched_state = USANDO_TPU;
    outb(TPU_PORT, 0xAA);
}
```

# Ejercicio 4

## Consideraciones

- Para liberar la TPU debo desmapear el buffer de entrada de la tarea que es dueña de la TPU y luego poner en 0 la variable `idx_TPU_owner`

## Código

```c
void free_TPU(void) {
    uint32_t rc3_owner = tasks[idx_TPU_owner].cr3;
    //son 16 paginas
    for(int i = 0; i < 16; i++) {
        //desmapeo pagina una a una
        mmu_unmap_page(rc3_owner, virt_buf_entrada + (4096*i));
    }
    idx_TPU_owner = 0;
}
```

# Ejercicio 5

## Consideraciones

- Como la TPU me avisa por el IRQ3 del PIC2 esto se traduce al IRQ11 de mi bloque de PICS.

- El IRQ11 se traduce a la _isr43 (base 32 + 12 IRQs (la 0,1,...,11) ).

- Como esta a conectada a PIC2 que es el escalvo tengo que hacer finish a los dos.

- Invoca a mi funcion en C que va a liberar la TPU y va a buscar quien es la siguiente que está esperando usarla para hacerla dueña.

## Código

Agrego a la IDT:

```c
void idt_init() {
    //...
    IDT_ENTRY0(43); //Importante que sea de nivel 0 pues quien la llama es una interrupcion por hardware
    //...
}
```

En isr.h:

```c
void _isr43();
```

En isr.asm:

```nasm
global _isr43
extern termino_procesamiento_TPU

_isr43:
    pushad
    ; 1. Le decimos al PIC que vamos a atender la interrupción
    call pic_finish2

    call termino_procesamiento_TPU

    ;no hace falta hacer un cambio de tarea puesto que solo se van a estar ejecutando tareas RUNNABLE entonces, con la politica del scheluder van a volverse a ejecutar las tareas que les cambie el sched_state

    popad
    iret
```

En C la funcion `termino_procesamiento_TPU` se encarga de liberar la TPU y de asignarla a otro tarea esperando:

```c
void termino_procesamiento_TPU(void) {
    task[idx_TPU_owner].sched_state = RUNNABLE //la volvemos a habilitar puesto que estaba en USANDO_TPU
    free_TPU();
    for(i = (current_task + 1); (i % MAX_TASKS) != current_task; i++) {
        // busco la siguiente que esta esperando usar la TPU
        if(tasks[i % MAX_TASKS].sched_state == ESPERANDO_TPU){
            make_task_TPU_owner(i % MAX_TASKS); //se llama despues de free_TPU, se garantiza precondicion de make_task_TPU_owner
            tasks[i % MAX_TASKS].sched_state = RUNNABLE; //como estaba esperando el acceso y ahora ya lo tiene, entonces puede seguir corriendo
            return;
        }
    }
}
```

# Ejercicio 6

## Situación en que una tarea no dueña de la TPU intenta escribir el buffer de entrada mientras la TPU está en uso

Cuando se da esta situación, lo que va a ocurrir es un page fault pero por lo que definimos en el ejercicio 2 (y también está explicado ahí), el page fault va a consultar, antes de mapear el buffer de entrada de la tarea que lo esta solicitando, si la TPU está en uso.

En caso de que esté en uso, va a poner la tarea en estado de ESPERANDO_TPU para que no siga ejecutando, y luego va a proceder con un cambio de tarea (guardando todos los recursos de la tarea, consultando la siguiente a ejecutar y efectivamente cambiando)

## Cómo se comporta una tarea no dueña mientras espera que se libere la TPU

Como la tarea no queda en estado RUNNABLE queda sin ejecutarse y con su contexto guardado hasta que vuelva a tener estado RUNNABLE

## Cómo se realiza el traspaso de propiedad de la TPU entre tareas

Cuando la TPU avisa por interrupción de hardware que finalizó de procesar, hace en orden:

- Poner a la tarea que estaba haciendo uso de la TPU en estado RUNNABLE puesto que ya tiene la info necesaria en el buffer de salida para seguir ejecutandose

- Se llama a la funcion del ejercicio 4 `free_TPU` para liberar el uso de la TPU, esto hace que se desmapeen las direcciones del buffer de entrada de la tarea que estaba haciendo uso y se ponga en 0 la variable que indica el dueño de la tarea

- Luego se busca, partiendo desde current_task, quién es la siguiente que estaba esperando hacer uso de la TPU, se la hace dueña con `make_task_TPU_owner` y se la pone en estado RUNNABLE para que pueda seguir ejecutandose y, eventualmente, hacer syscall para avisar que terminó de llenar el buffer de entrada

## Si había una tarea esperando que se liberara la TPU y se vuelve dueña de la TPU \- comienza a escribir el buffer de entrada inmediatamente?

Con este diseño, NO comienza a escribir inmediatamente.

Puesto que la que produce el cambio de dueño de TPU (ahora la dueña es tarea Y) es la interrupción de finalización de TPU, cuando se produce esa interrupcion se puede estar ejecutando la tarea X (siendo la tarea X no esta haciendo ningun uso de la TPU), cuando termina la interrupción vuelve a seguir ejecutando la tarea X, es decir, NO se pone a escribir la tarea Y.

La tarea Y va a ponerse a escribir recien cuando el schedluer le vuelva a asignar su tiempo de ejecución, puesto la tarea Y tiene estado `RUNNABLE`.

# Ejercicio 7

## Cómo se logra que el acceso al buffer de entrada efectivamente sea exclusivo

Se logra porque cuando ya se está haciendo uso del buffer, es decir, cuando una tarea es dueña de la TPU, no hay manera que alguna otra tarea que tambien lo requiera pueda acceder.

Si alguna otra tarea que se está ejecutando quiere acceder, la pasamos a un estado distitnto de RUNNABLE que va a hacer que se quede esperando sin ejecutarse, y por ende, que no esté pudiendo acceder al buffer exclusivo de entrada

Ademas cuando la tarea dejó de usar la TPU (porque la TPU finalizó de procesar), es el sistema el que se encarga de transmitir a una (y solo una) tarea que estuviera en espera de uso de la TPU.

## Cómo se garantiza que las tareas tengan acceso al buffer de salida cuando lo necesiten

Al iniciar nuestro sistema, puesto que todas las tareas con NPCs, todas las tareas van a tener mapeada en la direccion virtual 0x500000 el buffer de salida.

Solo mapeo una pagina pues para un datos de 32 bits es lo minimo que necesito y puedo dar. La pagina está Presente y es de Usuario y solo escritura (pues no tiene sentido que pueda escribir ahi, si seria necesario que sea R/W se le agrega | `MMU_W`).

Con esto garantizo que esté mapeado en su mapa de memoria y que todas las tareas puedan acceder al buffer de salida.

Se modifica la funcion `mmu_init_task_dir` con:

```c
#define VIRTUAL_BUFF_SALIDA 0x500000 //lugar no mapeado de las direcciones virtuales y que no choca con el usado en 0x400000
#define FISICA_BUFF_SALIDA 0xFAC1000

paddr_t mmu_init_task_dir(paddr_t phy_start) {
    //...
    mmu_map_page(task_cr3, VIRTUAL_BUFF_SALIDA, FISICA_BUFF_SALIDA, MMU_P | MMU_U);
    //...
}
```

## Qué modificaciones hay que hacer al sistema para garantizar que todas las tareas tengan su propio mapa de decisiones

Decidio resolverlo modifcando la funcion `mmu_init_task_dir` que inicializa el esquema de paginación de todas las tareas, y por ende, de todos los NPCs.

Cuando inicializo los esquemas de paginación de las tareas voy pidiendo paginas nuevas de usuario para que esa dirección virtual vaya a una dirección fisica diferente para cada NPC (es similar al sub punto anterior, pero pidiendo nuevas paginas de usuario). Como para pedir estas paginas nuevas utilizo `next_free_user_page`, necesito modificarlo para que comience a generar direcciones desde `0x501000`, y no desde `0x400000` como esta en el TP.

La pagina está Presente y es de Usuario y solo escritura `(MMU_P | MMU_U)`.

Como el mapa es de 4KiB, con una sola pagina alcanza.

Con esto garantizo que esté mapeado en su mapa de memoria y que cada tarea tengo un mapa diferente.

```c
#define VIRTUAL_MAPA_DECISIONES 0x0C0FE000

paddr_t mmu_init_task_dir(paddr_t phy_start) {
    //...
    paddr_t direc_fisica_mapa = mmu_next_free_user_page();
    mmu_map_page(task_cr3, VIRTUAL_MAPA_DECISIONES, direc_fisica_mapa, MMU_P | MMU_U);
    //...
}
```