#define _GNU_SOURCE
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/resource.h>
#include <time.h>
#include <signal.h>


//timeout para pipelines por grupo de procesos, esto sirve para cerrar toda la cola de procesos restantes de los pipes luego de que termine el max time
static volatile pid_t g_pgid = -1;

static void tiempo_agotado_killpg(int sig) {
    (void)sig;
    if(g_pgid > 0) {
        killpg(g_pgid, SIGKILL);
    }
}


//ejecuta comando simple y devuelve rusage del hijo, esto nos sirve para obtener los tiempos de ejecucion de los comandos que no tengan pipes
int ejecutar_comando_medido(char **argv, int max_tiempo, struct rusage *out_ru){
    memset(out_ru, 0, sizeof(*out_ru));

    pid_t pid = fork();
    if(pid == 0){
        if(max_tiempo > 0){
            signal(SIGALRM, SIG_DFL);  // por si heredo algo raro
            alarm(max_tiempo);
        }
        execvp(argv[0], argv);
        perror("execvp");
        _exit(1);
    }else if(pid < 0){
        perror("fork");
        return -1;
    }else{
        int status;
        struct rusage ru_child;
        if(wait4(pid, &status, 0, &ru_child) == -1) {
            perror("wait4");
            return -1;
        }
        *out_ru = ru_child;
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
}




//este sirve para obtener los tiempos de ejecucion y ejecutar los comandos con pipes
int ejecutar_comando_con_pipe_medido(char **argv, int argc, int max_tiempo, struct rusage *out_ru) {
    memset(out_ru, 0, sizeof(*out_ru));

    // Contar pipes y poner comandos
    int contador_pipes = 0;
    for(int i = 0; i < argc; i++){
        if(strcmp(argv[i], "|") == 0){
            contador_pipes++;
        } 
    } 
    int num_comandos=contador_pipes+ 1;

    char **comandos[num_comandos];
    int j=0;
    comandos[j]=&argv[0];
    for(int i = 0; i < argc; i++){
        if(strcmp(argv[i], "|")==0){
            argv[i]=NULL;
            j++;
            comandos[j]=&argv[i+1];
        }
    }

    //Crear pipes
    int cantidad_de_fd = contador_pipes;
    int fd[cantidad_de_fd][2];
    for(int i = 0; i < cantidad_de_fd; i++){
        if(pipe(fd[i]) == -1){
            perror("pipe");
            return -1;
        }
    }

    //timeout por grupo, usa helper de timeout
    g_pgid= -1;
    if(max_tiempo > 0){
        signal(SIGALRM, tiempo_agotado_killpg);
        alarm(max_tiempo);
    }

    //fork de cada etapa
    pid_t first_pid=-1;
    for(int i= 0; i < num_comandos; i++){
        pid_t pid = fork();
        if (pid == 0) {
            if(i == 0){
                setpgid(0, 0);
            }
            else{
                setpgid(0, first_pid);
            }        

            if(i > 0){
                if(dup2(fd[i-1][0], STDIN_FILENO)== -1){
                    perror("dup2 stdin"); _exit(1);
                }
            }
            if(i < cantidad_de_fd){
                if (dup2(fd[i][1], STDOUT_FILENO) == -1){
                    perror("dup2 stdout");
                    _exit(1);
                }
            }

            for(int k=0; k < cantidad_de_fd; k++){
                close(fd[k][0]);
                close(fd[k][1]);
            }

            execvp(comandos[i][0], comandos[i]);
            perror("execvp");
            _exit(1);
        } else if (pid < 0) {
            perror("fork");
            for (int k = 0; k < cantidad_de_fd; k++){
                close(fd[k][0]); close(fd[k][1]);
            }
            if(max_tiempo > 0){
                alarm(0);
            } 
            return -1;
        } else{
            if(i == 0){
                first_pid=pid;
                setpgid(pid, pid);
                g_pgid = pid;
            }
            else{
                setpgid(pid, first_pid);
            }
        }
    }

    //cerrar extremos en el padre
    for(int k = 0; k < cantidad_de_fd; k++){
        close(fd[k][0]); close(fd[k][1]);
    }

    
   //esperar cada etapa y acumula rusage de cada comando entre pipes
for(int i = 0; i < num_comandos; i++){
    int status;
    struct rusage ru_step;
    if(wait4(-1, &status, 0, &ru_step) == -1){
        perror("wait4 pipeline");
        if(max_tiempo > 0){
            alarm(0);}
        return -1;
    }

    //suma de tiempos para desplegar en pantalla
    out_ru->ru_utime.tv_sec  += ru_step.ru_utime.tv_sec;
    out_ru->ru_utime.tv_usec += ru_step.ru_utime.tv_usec;
    out_ru->ru_stime.tv_sec  += ru_step.ru_stime.tv_sec;
    out_ru->ru_stime.tv_usec += ru_step.ru_stime.tv_usec;

    // aca se pasan los microsegundos a segundos cuando estos llegan al limite
    if(out_ru->ru_utime.tv_usec >= 1000000){
        out_ru->ru_utime.tv_sec += out_ru->ru_utime.tv_usec / 1000000;
        out_ru->ru_utime.tv_usec %= 1000000;
    }
    if(out_ru->ru_stime.tv_usec >= 1000000){
        out_ru->ru_stime.tv_sec += out_ru->ru_stime.tv_usec / 1000000;
        out_ru->ru_stime.tv_usec %= 1000000;
    }

    //memoria maxima, toma el max entre etapas del pipeline
    if(ru_step.ru_maxrss > out_ru->ru_maxrss){
        out_ru->ru_maxrss = ru_step.ru_maxrss;
    }
}


    if(max_tiempo > 0){
        alarm(0);

    } 
    g_pgid = -1;

    return 0;
}


void leer_linea(char *linea, size_t tamaño_de_la_linea){

    //muestra el
    printf("\033[1;32mmishell:$ \033[0m"); //tiene otro color
    fflush(stdout);
    //prompt


    if (fgets(linea, tamaño_de_la_linea, stdin) == NULL) {
        printf("\nError de lectura\n");
        exit(0);
    }
    linea[strcspn(linea, "\n")] = '\0';// quito el salto de linea

}
int tokenizador(char *linea, char **argv, int max) {
    const char separador[]=" \t";
    const char marcador='\x1F'; //para espacios en comillas
    int argc=0;
    int en_comillas=0;
    char tipo=0;

    //linea caracter por caracter
    for(int i=0; linea[i]!='\0'; i++){
        if(!en_comillas && (linea[i]=='"' || linea[i]=='\'')){
            en_comillas=1;
            tipo=linea[i];
            //elimino la comilla corriebndo todo
            for(int j=i;linea[j]!='\0'; j++){
                linea[j]=linea[j+1];
            }
            i--;
            continue;
            
        }
        if(en_comillas && linea[i]==tipo){
            en_comillas= 0;
            tipo=0;
            for(int j=i; linea[j]!='\0'; j++){
                linea[j]=linea[j+1];
            }
            i--;
            continue;
        }
        if(en_comillas && (linea[i]==' ' || linea[i]=='\t')) {
            linea[i]=marcador; // cambio espacio por el marcador
        }
    }
    char *tok=strtok(linea, separador);
    while (tok!=NULL && argc < max-1) {
        // vuelvo a poner los espcaios donde estaba el marcdador
        for(int k=0; tok[k]!='\0'; k++){
            if(tok[k]==marcador){
                tok[k]=' ';
            } 
        }
        argv[argc++] = tok;
        tok = strtok(NULL, separador);
    }


    argv[argc] = NULL;
    return argc;
}

int ejecutar_comando(char **argv){
    pid_t pid =fork(); //creo a un hijo para que ejecute el comando

    if(pid==0){ // el hijo
        
        
        execvp(argv[0],argv);//ejecuta el comando
        perror("Hubo un error");//solo se ejecuta si falla
        _exit(1);

    }
    else if(pid<0){ // si fallara el fork
        perror("fallo fork");
        return -1;
    }
    else{
        //es el padre, debe esperar a su hijo, para no ser paralelo el proceso
        wait(NULL);
    }
    return -1;
}

int ejecutar_comando_con_pipe(char **argv, int argc){
    //cuento los pipes
    int contador_pipes=0;
    for(int i=0;i<argc;i++){
        if (strcmp(argv[i], "|")==0){
            contador_pipes++;
        }
    }
    int num_comandos=contador_pipes+1;//cantidad de comandos a ejecutar

    //separo los comandos

    char **comandos[num_comandos];
    int j = 0; //para el indice en comandos
    comandos[j]= &argv[0];

    for (int i =0; i< argc;i++){
        if(strcmp(argv[i],"|")==0){
            argv[i]=NULL; //para separarlos
            j++;
            comandos[j]=&argv[i+1];

        }
    }


    //creo los pipes, fd sera file descriptor, numero que asigna el kernel para identificar el recurso abierto
    //inicializo el arreglo, y veo si tiene algun error

    int cantidad_de_fd=contador_pipes;
    int fd[cantidad_de_fd][2];//necesito usar el anterior para saber que se hiso antes

    //aqui veo si algun pipe me dio error
    for(int i=0; i< cantidad_de_fd;i++){
        if(pipe(fd[i])==-1){
            perror("error de pipe");
            return -1;
        }
    }

    //ahora debo ajecutar los procesos
    //se crea un hijo por cada comando
    //los pipes son numero de comandos-1, conectan dos procesos, uno escribe el otro lee
    //fd[0][0] se lee el pipe 1
    //fd[0][1] se escribe el pipe 1
    //fd[1][0] se lee el pipe 2
    //fd[1][1] se escribe el pipe 2
    // ademas hay que cerrar los pipes, si no se hace puede ocasionar errores y bloqueos

    
    for (int i=0;i<num_comandos;i++){
        pid_t pid = fork();
        if(pid == 0){// estoy en el hijo
            //hare uso de dup2, hace que un fd nuevo pueda usar la info de un fd viejo
            //casos
            //redirigir salida,con STDOUT_FILENO
            //redirigir entrada,con STDIN_FILENO
            //ademas esta STDERR_FILENO, que es para los errores
            if(i>0){ // caso de que no es el primer comando, el stdin es el del pipe anterior(lectura)
                int redirigir_stdin=dup2(fd[i-1][0],STDIN_FILENO);
                //esto se ejecuta solo si falla
                if(redirigir_stdin==-1){
                    perror("dup2 error del stdin");
                    _exit(1);
                }
                

            }
            //el stdout va al pipe actual(escritura), si no es el ultimo
            if(i< cantidad_de_fd){
                int redirigir_stdout = dup2(fd[i][1],STDOUT_FILENO);
                //se ejecuta solo si falla
                if (redirigir_stdout==-1){
                    perror("duop 2 error del stdout");
                    _exit(1);
                }
            }

            // cierro los fd, ya use el dup2, para conectar los pipes
            for(int j=0;j<cantidad_de_fd;j++){
                close(fd[j][0]);
                close(fd[j][1]);
            }
            //ejecuto el comando en la iteracion que estamos
            execvp(comandos[i][0], comandos[i]);
            //solo se ejecuta si falla
            perror("error en execvp");
            _exit(1);


        }
        else if(pid<0) {
            perror("error en el fork");
            //cierro todos los fd antes de salir
            for(int j=0;j<cantidad_de_fd;j++){
                close(fd[j][0]);
                close(fd[j][1]);
            }
            return -1;
        }
        //el padre sigue creando sus hijos
    }
// el padre cierra todos los fd
    for(int j=0;j<cantidad_de_fd;j++){
        close(fd[j][0]);
        close(fd[j][1]);
    }

    // se esperan todos los hijos
    for(int i=0;i< num_comandos;i++){
        wait(NULL);
    }




    return 1;
}


void tiempo_agotado(){
    _exit(1);
}


void ejecutar_miprof(char **argv, int argc) {
    char *output_file = NULL;
    char **comando_argv = NULL;
    int comando_argc = 0;
    int max_tiempo = 0;

    // detectamos si tiene pipes
    bool tiene_pipes = false;
    for(int i = 0; i < argc; i++){
        if(strcmp(argv[i], "|") == 0){
            tiene_pipes = true;
            break;
        }
    }
    //aca detectamos si luego del comando miprof se ingresa "ejec", "ejecsave", "ejecutar" o " "(el cual funciona como ejec), ademas asignamos los siguientes parametros
    // dependiendo de cual miprof estamos utilizando, por ejemplo, con ejecsave, el siguiente parametro se guarda como el archivo de texto donde se guardaran los resultados(sobreescribiendo de existir)
    int inicio_comando = 1;
    if(argc>=2 && strcmp(argv[1], "ejec")== 0){
        comando_argv = &argv[2];
        comando_argc = argc - 2;
        inicio_comando = 2;
    }
    else if(argc> 3 && strcmp(argv[1], "ejecsave")==0){
        output_file = argv[2]; comando_argv = &argv[3];
        comando_argc = argc-3;
        inicio_comando = 3;
    }
    else if(argc>= 3 && strcmp(argv[1], "ejecutar")== 0){
        max_tiempo = atoi(argv[2]);
        comando_argv = &argv[3];
        comando_argc = argc-3;
        inicio_comando = 3;
    }
    else{
        comando_argv = &argv[1]; comando_argc = argc - 1;
    }
    // se imprime el formato correcto en caso de ingresar mal el comando
    if(comando_argc < 1){
        printf("Error: formato correcto: miprof [ |ejec|ejecsave archivo|ejecutar seg] <comando> [args...]\n");
        return;
    }
    if(output_file){
        printf("\nResultados se guardarán en: %s\n\n", output_file);
    }
    //aca se inicializan los structs para tomar los tiempos, y ademas se inicia el contador de tiempo para calcular el tiempo real
    struct timespec inicio, fin;
    clock_gettime(CLOCK_MONOTONIC, &inicio);

    struct rusage ru_total;
    memset(&ru_total, 0, sizeof(ru_total));
    int rc;
    // de tener pipes el comando se utiliza ejecutar_comando_con_pipe_medido
    if(tiene_pipes){
        char **comando_con_pipes=&argv[inicio_comando];
        int argc_comando = argc-inicio_comando;
        rc=ejecutar_comando_con_pipe_medido(comando_con_pipes, argc_comando, max_tiempo, &ru_total);
    }
    // de no tener pipes se ejecuta ejecutar_comando_medido
    else{
        rc=ejecutar_comando_medido(comando_argv, max_tiempo, &ru_total);
    }
    if(rc!=0){
        perror("Error de rc");
    }
    //aca se finaliza el contador de tiempo final para poder realizar los calculos e imprimir en pantalla
    clock_gettime(CLOCK_MONOTONIC, &fin);

    double real_time=(fin.tv_sec - inicio.tv_sec) + (fin.tv_nsec - inicio.tv_nsec) / 1e9;
    double user_time =ru_total.ru_utime.tv_sec + ru_total.ru_utime.tv_usec / 1e6;
    double system_time=ru_total.ru_stime.tv_sec + ru_total.ru_stime.tv_usec / 1e6;

    //salida por pantalla
    printf("\nRESULTADOS MIPROF\n");
    printf("Comando ingresado: ");
    for(int i = inicio_comando; i<argc;i++){
        if(argv[i]==NULL){
            printf("| ");
        } 
        else{
            printf("%s ", argv[i]);
        }                
    }
    printf("\n");
    printf("Tiempo real: %.6f s\n", real_time);
    printf("Tiempo usuario: %.6f s\n", user_time);
    printf("Tiempo sistema: %.6f s\n", system_time);
    printf("Memoria máxima (ru_maxrss): %ld KB\n", ru_total.ru_maxrss);

    //salida a archivo 
    if (output_file) {
        FILE *archivo = fopen(output_file, "a");
        if (archivo) {
            fprintf(archivo, "RESULTADOS MIPROF\n");
            fprintf(archivo, "Comando: ");
            for(int i=inicio_comando; i < argc; i++){
                if(argv[i]==NULL){
                    fprintf(archivo, "| ");
                } 
                else{
                    fprintf(archivo, "%s ", argv[i]);
                }                 
            }
            fprintf(archivo, "\n");
            fprintf(archivo, "Tiempo real: %.6f s\n", real_time);
            fprintf(archivo, "Tiempo usuario: %.6f s\n", user_time);
            fprintf(archivo, "Tiempo sistema: %.6f s\n", system_time);
            fprintf(archivo, "Memoria máxima: %ld KB\n\n", ru_total.ru_maxrss);
            fclose(archivo);
            printf("Resultados guardados en: %s\n", output_file);
        }
        else {
            printf("Error abriendo archivo: %s\n", output_file);
        }
    }
}



int main (void){


    char linea[1024];// buffer para la linea

    char *argv[256];//espacio asignado para argumentos

    while(true){
        leer_linea(linea, sizeof(linea));
        if (linea[0] == '\0'){//si se puso enter continua
            continue; 
        }         
        if (strcmp(linea, "exit") == 0){ //sale de la shell
            break;
        } 

        int argc=  tokenizador(linea, argv, 256);
        if( argc==0){
            continue; // si no hay argumentos, pido otra linea
        }

        bool pipe=false; // detecto si hay o no hay pipe
        for  (int i=0; i<argc; i++){
            if(strcmp(argv[i],"|")==0){
                pipe =true;
                break;
            }
        }
        // si se utiliza comando miprof
        if(strcmp(argv[0], "miprof") == 0){
            ejecutar_miprof(argv,argc);
            continue;
        }// si se utiliza comando con pipes sin miprof
        else if (pipe){
            ejecutar_comando_con_pipe(argv,argc);

        } // si se utiliza comando sin pipes ni miprof
        else{
            ejecutar_comando(argv);
        }
       
           
        

    }



    return 0;
}