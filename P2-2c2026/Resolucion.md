### Estudiante: Santiago Jauregui
### LU: 1204/23

# Resolución

### Ejercicio 1



```c
vaddr_t buffer_entry = mmu_next_free_user_page()
typedef struct {
    eax,
    ...,
    ...,
    task_state sched_state,
    
    uint32_t solicito_TPU // 1 si solicito la TPU y esta esperando que se libere, 0 sino
} task_t
//  en create_task se inicializa con 0

task_id idx_TPU_owner; //variable global

// Mappeo todo el buffer de la TPU a la memoria virtualde la tarea dueña y le doy permisos de escritura
void make_task_TPU_owner(task_id new_owner){
    idx_TPU_owner= new_owner;
    uint32_t cr3_owner= tasks[new_owner].cr3;
    for(int i=0; i <16; i++){
        mmu_map_page( cr3_owner, buffer_entry + i*4096, 0xFAC000+ i*4096, (MMU_P | MMU_U | MMU_W));
        zero_page(buffer_entry + i*4096);
    }
    return
}
```

### Ejercicio 2
1- Creamos la syscall isr90 la cual va ser la que hace dueña de la TPU a la tarea que llamo. La agrego a la idt
Va a ser lamada cuando se este ejecutando una tarea la cual requiere la TPU y llama para ser dueña, con la precondicion de que no se esta utilizando, es decir la tarea tendria que chequear eso y despues llamar en caso de que este desocupada.

```c 
void idt_init(){
    ... ;
    ... ;
    IDT_ENTRY3(90);

}
``` 

```asm
isr90:
pushad
mov eax, current_task
push eax
call make_task_TPU_OWNER
add esp, 4
popad
iret
``` 
2- A la rutina solo las van a poder llamar las tareas es decir cada NPC

### Ejercicio 3
Agrego la rutina de atencion a la IDT, va a ser la isr91
```c
void idt_init(){
    ... ;
    ... ;
    IDT_ENTRY3(90);
    IDT_ENTRY3(91);

}
``` 

```asm
isr91:
call save_current_task_context

call procesar_TPU
xor eax, eax
mov al, AA
in 0x1E0, al

call sched_next_task
jmp switch_to_usermode
```  

```c
void procesar_TPU(){
    tasks[current_task].sched_state= TASK_PAUSED;
    return

} 
``` 

### Ejercicio 4
```c
void free_TPU(){
    int8_t id= idx_TPU_owner;
    idx_TPU_owner= 0;
    uint32_t cr3_owner = tasks[id].cr3
    for (int i=0; i<16; i++){
        mmu_unmap_page(cr3_owner, 0xFAC000 + i*4096);
        
    }
    return
}
```  

### Ejercicio 5

Esa rutina va a ser la isr42 ya que es la atendida por el PIC2 en IRQ3
```c
void idt_init(){
    ... ;
    ... ;
    IDT_ENTRY3(90);
    IDT_ENTRY3(91);
    IDT_ENTRY0(42);

}
```  

```asm

isr42:
call save_current_task_context
call pic_finish1
call pic_finish2

call TPU_procesada
call free_TPU
call asignar_owner
jmp switch_to_usermode

``` 

```c 
void TPU_procesada(){
    int8_t id= idx_TPU_owner;
    tasks[id].sched_state= TASK_RUNNABLE ;

    return
}
``` 
```c 
void asignar_owner(){
    for (i=0; i< MAX_TASKS; i++){
        if(tasks[i].sched_state== TASK_PAUSED && task[i].solicito_TPU==1){
            tasks[i].sched_state=TASK_RUNNABLE;
            task[i].solicito_TPU=0;
            idx_TPU_owner= i ;
        }
    }
    
    return
}
``` 
El rol que cumple es el de volver a poder ejecutar la tarea dueña que puso en pausa porque la TPU estaba procesando lo que puso la tarea en el buffer de entrada, y como ya termino de procesar la TPU, la tarea dueña puede seguir ejecutando. Y ademas libera el uso de la TPU, es decir la tarea dueña ya no es mas dueña por lo que desmapeamos, limpeamos y le sacamos el permiso de escritura a la tarea exdueña.
Y llamando a asignar_owner podemos asignarle a la TPU una nueva dueña en caso de que algun NPC la haya solicitado

### Ejercicio 6
Para este punto voy a tener que modificar la syscall make_task_TPU_owner, ya que tengo que agregar el caso de que le TPU este ocupada

```c
void make_task_TPU_owner(task_id new_owner){
    idx_TPU_owner= new_owner;
    uint32_t cr3_owner= tasks[new_owner].cr3;
    for(int i=0; i <16; i++){
        mmu_map_page( cr3_owner, buffer_entry + i*4096, 0xFAC000+ i*4096, (MMU_P | MMU_U | MMU_W));
        zero_page(buffer_entry + i*4096);
    }
    return
}
```
Ahora la rutina de atencion 90 no va cumplir solo con  lo pedido del 1, sino que agregamos una caso separado que puede suceder

```asm
isr90:
call save_current_task_context
xor eax,eax
mov eax, current_task
push eax
call solicitando_TPU
pop eax
cmp eax,1
je .continuo

call sched_next_task

.continuo:
jmp switch_to_user_mode
``` 
```c
uint32_t solicitando_TPU(task_id id){
    if(idx_TPU_owner!= 0){
       make_task_TPU_owner(id) ;
       return 1;
    }
    else{
        tasks[id].sched_state= TASK_PAUSED;
        tasks[id].solicito_TPU=1;
        return 0;
    }
}
```
En vez de llamar a make_task_TPU_owner cree una funcion superior que es solicitando_TPU que recibe el id de la tarea que quiere ser dueña de la TPU, esta lo que hace es mirar los 2 casos, Si la TPU no tiene dueña llama a make_task_TPU_owner y devuelve 1 (significa que se pudo hacer dueña a la tarea), y si la TPU tiene dueña lo que hace es pausar la tarea que queria y poner en su definicion que solicito_TPU.
En caso que devuelva 1 continuo con la tarea que llamo a la syscall, pero si devuelve 0 cambio de tarea porque la actual queda pausada.

- Una tarea no dueña que pidio la TPU pero estaba ocupada se mantiene pausada y con solicito_TPU en 1 
- El intercambio se realiza cuando la TPU termina de procesar y ejecuta la rutina 42, lo que libera la TPU de la dueña y la deja en RUNNABLE, para que despues se llame a asignar_owner antes de volver al usermode, esta funcion asigna una nueva dueña, y esta misma cumple con que su estado era PAUSED y que habia solicitado la CPU, si cumple con esto se cambia el idx_TPU_owner a esta misma y su estado cambia a RUNNABLE y salta al usermode para seguir con la tarea que estaba que interrumpio la TPU.
- No comienza a escribir inmediatamente porque la TPU interrumpe una tarea que se estaba ejecutando que no es la que es la dueña ahora porque esta estaba pausada, por lo tanto continua con la tarea que interrumpio

### Ejercicio 7 
- Al mappear todo el buffer de la TPU a la memoria virtual de la tarea dueña y le doy permisos de escritura, como la direccion virtual esta solamente en la memoria virtual de la tarea dueña hace que ninguna tarea que no sea dueña tenga el buffer de entrada disponible , esto hace que efectivamente sea exclusivo

- Para esto tendria que agregar en el sistema un mappeo para todas las tareas que mappee la dir fisica 0xFAC10000 (el buffer de salida) una dir virtual de solo lectura. Lo que podria ser realizar un identity mapping, ya que esta seccion de la memoria virtual es solo de lectura
```c
void map_buffer_salida(){
    for (i=0; i< MAX_TASKS; i++){
        uint32_t cr3= tasks[i].cr3;
        mmu_map_page(cr3, 0xFAC10000,0xFAC10000,(MMU_P | MMU_U );
    }
}
``` 
- Para esto se tendria que mappear para cada tarea la pagina fisica del mapa suyo a esa direccion virtual con estos atributos: (MMU_P | MMU_U )






