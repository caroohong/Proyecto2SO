# Tarea 2 - Memoria Compartida

En esta tarea implementarás soporte de memoria compartida para xv6. 
La memoria compartida se implementa mediante páginas de memoria que dos o
más procesos pueden acceder. Es decir, las respectivas tablas de página
en los procesos hacen referencia a marcos físicos comunes.

Una región de memoria compartida es vista por cada proceso que la accede
como una región contigua de memoria, que puede extenderse por una o más
páginas de memoria lógica. Cada proceso puede mapear la memoria compartida 
en su espacio de memoria a partir de una dirección de memoria lógica 
completamente arbitraria, pero dentro de los límites de memoria que son
permitidos en xv6.

La interfaz de memoria compartida a implementar se basará en una única _system
call_ que será expuesta a los procesos de usuario con la siguiente firma,
la cual debe ser añadida a `user.h`:

```c
int shmget(uint token, char* addr, uint size);
```

El primer parámetro `token` sirve para identificar la región de memoria
compartida. El primer proceso en llamar a `shmget` con un determinado
token causará que la región de memoria compartida sea creada y mapeada
a partir de la dirección `addr`. Dicha dirección debe estar alineada con 
el tamaño de página (0x1000 ó 4096). Finalmente, el parámetro `size` dice
el tamaño de la región de memoria compartida en bytes. En realidad, el
tamaño efectivo de la región de memoria compartida será múltiplo del 
tamaño de página, por lo que debe utilizarse y mapearse la cantidad mínima 
de páginas que permitan contener el tamaño `size`.

Los valores de `addr` deben encontrarse en el rango `0x60000000 -> 0x7FFFFFFF`.

Si una región de memoria compartida con un cierto token ya se encuentra
creada, el proceso que invoque a `shmget` **debe especificar valor `size` 0** 
para incorporar la región existente en su propio espacio de memoria lógica
a partir de la dirección `addr` especificada.

## Valores de retorno de shmget

La _system call_ `shmget` retorna -1 cuando ocurre un error, y 0 cuando
termina exitosamente. Las posibles causas de error son las siguientes:

* Si al invocar `shmget` con valor `size` mayor que cero ya existe una 
región de memoria compartida asociada al mismo token especificado.
* Si el valor `addr + size` es mayor o igual a `KERNBASE` (dirección lógica
a partir de la cual se mapea la memoria del kernel).
* Si alguna de las funciones de las cuales `shmget` depende falla.

## Consejos para comenzar

El procesamiento de los parámetros de `shmget` se debe realizar en
`kernel/sysproc.c`, y la implementación de `shmget` se debe realizar
en `kernel/proc.c`. 

El análisis de las funciones de paginación de xv6 en `kernel/vm.c` 
es fundamental para esta tarea. Las funciones más importantes son las 
siguientes:

* `static pte_t * walkpgdir(pde_t *pgdir, const void *va, int alloc)`: 
Dado el directorio de páginas de un proceso, una dirección
de memoria lógica (va), y un flag (alloc), la función retorna el _Page Table Entry_ 
(PTE) asociado a la página en donde su ubica la dirección lógica dada.
El flag indica si se debe crear la tabla de páginas en caso que
no exista. Si el flag tiene valor 0 y la dirección lógica dada no está
mapeada, el valor de retorno es 0. También la función retorna 0 si
no es posible crear la tabla de página por falla en llamada a `kalloc`. 
* `static int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)`: Crea las PTEs en la estructura de tablas de página del proceso 
necesarias para mapear memoria lógica a partir de una dirección dada y por 
una cantidad de bytes. Llama a `walkpgdir` para crear las PTEs.
* `char* kalloc(void)`: Retorna un puntero a una página de memoria obtenida desde
la estructura de marcos libres (esta función está en `kernel/kalloc.c`).
La dirección de la página retornada por la función es lógica, no física 
(ver consejo para convertir a física al final del siguiente párrafo). 
Esta función se debe llamar al momento de crear la región de memoria 
compartida, por tantas páginas como se requieran.

Aparte de lo anterior, hay una serie de constantes y macros de administración
de memoria definidas en `kernel/mmu.h` y `kernel/memlayout.h`. En particular,
existen constantes para los bits de protección de los PTE, como `PTE_P` 
(entrada presente), `PTE_W` (página escribible), `PTE_U` (página accesible
por proceso de usuario). Además, hay un par de macros que permiten convertir
una dirección de memoria lógica a memoria física (`V2P`), y viceversa (`P2V`).
Un puntero a una página de memoria retornado por `kalloc` se puede convertir
a su dirección física respectiva llamando a `V2P`. Se puede ver un ejemplo
al respecto en la función `inituvm` en `kernel/vm.c`. La función `memset`
se puede usar para limpiar una página retornada por `kalloc` (llenar de ceros
todos sus bytes - esto se debe hacer siempre).

## Requisitos de la implementación

Se debe modificar la estructura `proc` en `kernel/proc.h` para añadir
los siguientes miembros de tipo `uint`: valor de puntero a región de memoria 
compartida, tamaño de región de memoria compartida, y token identificador 
de la región de memoria compartida.

Además, se debe desarrollar la siguiente funcionalidad:

1. [1.0 punto] Función que permita búsqueda de un proceso (su estructura `proc`) dado
un `token` identificador de memoria compartida. Esta función puede quedar
implementada en `kernel/proc.c`. Se debe cuidar la sincronización al
acceder a la estructura `ptable`.
2. [1.0 punto] Función en que construya la región de memoria compartida 
llamando `kalloc` y `mappages`. Esta función puede quedar implementada en `kernel/vm.c`.
3. [1.5 punto] Función que permita copiar entradas de tabla de página desde un proceso
a otro. Debe recibir punteros a los directorios de página de los procesos
de origen y destino, la dirección lógica en el espacio de origen, la
dirección lógica en el espacio de destino, y el tamaño en bytes de la región 
cuyas entradas se deben copiar. La función `mappages` en `kernel/vm.c` puede
ser una excelente inspiración para esto.
4. [1.0 punto] La función `shmget` que debe decidir si se crea o no la región
de memoria compartida y realizar el mapeo de memoria si corresponde. 
La región no debe crearse si ya existe un  proceso que tenga su token. Si la 
región ya está creada, se debe llamar a la función (3). Si la región no está
creada, entonces, sdebe llamar a la función (2).
5. [1.0 punto] Prueba funcional de la solución utilizando programa de pruebas 
`shmemtest` (ver sección siguiente).

## Depuración y Pruebas

Se recomienda partir creando un programa de prueba en espacio de usuario que
llame a `shmget`, con un cierto token e inicialmente requiriendo una región
del tamaño (`size`) de una página o menos. Luego, verificar que sea posible 
escribir desde el programa de pruebas directamente en la región de memoria 
compartida.

Una vez que `shmget` funciona correctamente invocada desde un programa de
usuario, es posible probar dos procesos concurrentes padre e hijo (`fork`), 
haciendo que el padre escriba un patrón en la memoria compartida, y luego 
el hijo verifique la existencia del patrón en la memoria compartida que está 
mapeada a su propio espacio. El profesor pone a disposición el programa de 
pruebas [`shmemtest`](https://gist.github.com/claudio-alvarez/7789178c5efc4b7fb843a0f39aadd8a4)  
(seguir el enlace para ver gist) para realizar la prueba completa de 
la implementación.

## Modalidad de trabajo

Esta tarea deberá ser resuelta en parejas. La totalidad del trabajo debe
realizarse en el repositorio clonado después de aceptar la invitación en GitHub
classroom. Para la entrega, se considerará la última revisión en el repositorio
anterior a la fecha y hora de entrega. La entrega es para el 24 de octubre a las
23:59 hrs.

Dado que en esta tarea el trabajo es algo menos estructurado que en las anteriores
y hay algo más de espacio para la creatividad, es muy importante que editen el 
archivo `INFORME.md` en el directorio raíz del repositorio para indicar todo aquello
relevante que hayan modificado o añadido a xv6 en el desarrollo de la tarea, y así
facilitar la evaluación. Es muy importante que describan cómo quedaron implementados
los requisitos 1-4 nombrado arriba en las sección "Requisitos de la implementación".

Recuerden poner los nombres de los integrantes de su grupo al inicio del archivo
`INFORME.md`.

## Compilación, Ejecución y Depuración

Para compilar e iniciar xv6, en el directorio en donde se encuentra el código
de la tarea, se debe ejecutar el siguiente comando:

```sh 
prompt> make qemu-nox 
```

Para cerrar qemu, se debe presionar la combinación de teclas `Ctrl-a-x`.

Si se desea depurar el kernel con gdb, se debe compilar y ejecutar con 

```sh 
prompt> make qemu-nox-gdb
```

En un terminal paralelo, en el directorio del código base de la tarea se debe
ejecutar gdb:

```sh
prompt> gdb kernel
```

Algunas operaciones comunes con gdb son las siguientes:

* `c` para continuar la depuración. **Siempre se debe ingresar este comando
  cuando se inicia gdb**
* `b archivo:linea` para fijar un _breakpoint_ en cierto `archivo` y `linea`
  del mismo.
* `backtrace` (o `bt`) para mostrar un resumen de cómo ha sido la ejecución
  hasta el momento.
* `info registers` muestra el estado de registros de la CPU.
* `print`, `p` o `inspect` son útiles para evaluar una expresión.
*  Más información aquí: http://web.mit.edu/gnu/doc/html/gdb_10.html

En el kernel, puedes imprimir mensajes de depuración utilizando la función
`cprintf`, la cual admite strings de formato similares a `printf` de la biblioteca
estándar. Puedes ver los detalles de implementación en `kernel/console.c`.

Es posible imprimir valores en formato hexadecimal con las funciones `printf`
usando el especificador de formato `%x`.

Además, existe la función `panic` que permite detener la ejecución del kernel
cuando ocurre una situación de error. Esta función muestra una traza de la ejecución
hasta el momento en que es ejecutada. Los valores que muestra pueden ser
buscados en `kernel/kernel.asm` para comprender cómo pudo haberse
ejecutado la función.

