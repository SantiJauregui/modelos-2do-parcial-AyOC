/*
// Ejercicio 1)

    Tendremos las nuevas interrupciones.
 
    IDTENTRY0(41)
    IDTENTRY3(90)
    IDTENTRY3(91)
    IDTENTRY3(92)

    isr90
    isr91
    isr92

    ¿Cómo se puede lograr que las syscalls “lentas”¹ puedan ser interrumpidas?
    La forma de atender una nueva interrupción es salvando el estado de la syscall lenta y activando el bit IF de las interrupciones con STI. De esta manera, cuando llegue una interrupción, puede ser atendida sin perder el estado de syscall lenta.

    ¿Cuáles de las nuevas syscalls deben ser interrumpibles?
    La única syscall que debería ser interrumpible es draw_primitive.

    ¿Qué eventos pueden causar esas interrupciones?
    vblank

    Ejercicio 2)
    Para que el kernel tenga acceso al buffer, se debe mappear la dirección física del backbuffer 0xBE0BE000 como read/write y con permisos de super usuario U/S=0. De esta forma solo el kernel tiene acceso a esa dirección
    a todos les daremos un identity mapping. 0xBE0BE00 será la dirección virtual que apunte a la misma física.



typedef enum {
  TASK_SLOT_FREE,
  TASK_RUNNABLE,
  TASK_PAUSED
  esperando_pantalla
  controlando_pantalla

} task_state_t;

    isr90:
    pushad
    sti
    
    push ecx
    push esi
    push edi
    
    
    call draw_primitive
    add esp, 12
    
    popad
    iret

    void draw_primitive( uint32_t type, uint32_t point, uint32_t data )
        
        if(dueño_pantalla() == current_task_id){
            uint32_t dest = 0xBE0BE00
            draw_primitive_to(dest, type,  point, data)
        }
        return

    isr41:
    pushad

    call picfinish
    call vblank

    popad
    iret

    void vblank(void){
        if(está_pantalla_lista()){
            for(int i = 0; i<16; i++){
                //Se copian las 16 páginas que ocupa el buffer
                copy_page(0xB8000+i*4096, 0xBE0BE000+i*4096);
                
            }
        }else{
            dropped_frame++;
        }
    }

    //dropped_frames debería ir inicializado en 0 dentro del archivo screen.c para mantener coherencia entre los documentos. puesto con extern para poder accederlo sin problema

    isr91:
    
    call save_current_task_context

    call draw_start
    

    call sched_next_task

    jmp switch_to_usermode


    void draw_start(void){
        if(dueño_pantalla() == -1){
            tasks[current_task].state = "controlando_pantalla"
        }else{
            tasks[current_task].state = "esperando_pantalla"
        }
        return;
}


    isr92:
    pushad

    call draw_end

    popad
    iret

    void draw_end(void){
        if(current_task == dueño_pantalla()){
            tasks[current_task].state = "runnable"
            return;
        }
    }

    sched_next_task(){
    ...
        for (i = (current_task + 1); (i % MAX_TASKS) != current_task; i++) {
            
            // Si esta tarea está disponible la ejecutamos
            if (tasks[i % MAX_TASKS].sched_state == TASK_RUNNABLE ||
                (tasks[i % MAX_TASKS].sched_state == "esperando_pantalla" && dueño_pantalla() == -1)) {
                break;
        }
          // Ajustamos i para que esté entre 0 y MAX_TASKS-1
        i = i % MAX_TASKS;

        // Si la tarea que encontramos es ejecutable entonces vamos a correrla.
        if (tasks[i].sched_state == TASK_RUNNABLE || (tasks[i % MAX_TASKS].sched_state == "esperando_pantalla" && dueño_pantalla() == -1)) {
            current_task = i;
            return i;
        }
    ...
  }

    }

    bool está_pantalla_lista(){
        return dueño_pantalla() == -1;
    }

    task_id dueño_pantalla(){
        for (uint8_t i = 0; i < MAX_TASK; i++){
            if (tasks[i].state == controlando_pantalla){
                return i;
            }
        }
        return -1;
    }
*/