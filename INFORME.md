**Nombres: Carolina Hong, Andres Pirela**

ARCHIVOS CAMBIADOS:
tip: poner en el buscador "shared memory" y salen los cambios realizados
1. vm.c
    desde linea 501 // shared memory
2. usys.S
    linea 32: SYSCALL(shmget)
3. sysproc.c
    linea 116: implementacion de sys_shmget
4. syscall.h
    linea 24: #define SYS_shmget 22
5. syscall.c
    linea 108: extern int sys_shmget(void);
    linea 135: [SYS_shmget]  sys_shmget
6. proc.c
    desde linea 234 a 237
    desde linea 592 a 656
7. proc.h
    desde linea 72 a 76
8. user.h
    linea 29: int shmget(uint, char*, uint);
9. Makefile
    linea 32: $K/vm.o\
    linea 189: $U/_shmemtest\
    linea 261: shmemtest.c

Implementacion de system call en archivos: 
5, 3, 6, 8, 2

Implementación de funciones del requisito:
1: Función en que construya la región de memoria compartida llamando kalloc y mappages. Esta función puede quedar implementada en kernel/vm.c.
-> create_shared_memory_region

Función que permita copiar entradas de tabla de página desde un proceso a otro. Debe recibir punteros a los directorios de página de los procesos de origen y destino, la dirección lógica en el espacio de origen, la dirección lógica en el espacio de destino, y el tamaño en bytes de la región cuyas entradas se deben copiar. La función mappages en kernel/vm.c puede ser una excelente inspiración para esto.
-> copy_pte_range

6: Función que permita búsqueda de un proceso (su estructura proc) dado un token identificador de memoria compartida. Esta función puede quedar implementada en kernel/proc.c.
-> shmem_procpid
