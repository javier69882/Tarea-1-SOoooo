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


void leer_linea(char *linea, size_t tamaño_de_la_linea){

    //muestra el
    printf("mishell:$ ");
    fflush(stdout);
    //prompt


    if (fgets(linea, tamaño_de_la_linea, stdin) == NULL) {
        printf("\nError de lectura\n");
        exit(0);
    }
    linea[strcspn(linea, "\n")] = '\0';// quito el salto de linea

}
int tokenizador(char *linea, char **argv, int max){//argv es el vector de los argumentos, argc es el contador
    int argc = 0;
    char *token = strtok(linea, " \t");
    while(token && argc<max-1){
        char *token_actual=token;
        argv[argc]=token_actual; // lo guardo en el vector
        argc++;
        token = strtok(NULL, " \t");// pido el token que vien

    }
    argv[argc] = NULL;
    return argc;
}

int ejecutar_comando(char **argv){
    pid_t pid =fork(); //creo a un hijo para que ejecute el comando

    if(pid==0){ // el hijo
        printf("Resultado obtenido del comando: \n\n");
        
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

    printf("Resultado obtenido del comando: \n\n");
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
    struct timespec inicio, fin;
    struct rusage uso;
    int max_tiempo = 0;

    //comenzamos el contador total aca, desde que se reconoce la linea de comandos
    clock_gettime(CLOCK_MONOTONIC, &inicio);

    //revisamos si hay pipes en la linea de comandos
    bool tiene_pipes = false;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "|") == 0) {
            tiene_pipes = true;
            break;
        }
    }
    

    int inicio_comando = 1;
    // aca detectamos el modo ejec, sus comandos y argumentos
    if (argc >= 2 && strcmp(argv[1], "ejec") == 0) {
        comando_argv = &argv[2];
        comando_argc = argc - 2;
        inicio_comando = 2;
    }
    //en el caso que sea ejecsave se detecta aca, y se analiza el nombre del archivo localizado en el indice 2 del arreglo de tokens
    else if (argc >= 3 && strcmp(argv[1], "ejecsave") == 0) {
        output_file = argv[2];
        comando_argv = &argv[3];
        comando_argc = argc - 3;
        inicio_comando = 3;
    }
    
    else if (argc >= 3 && strcmp(argv[1], "ejecutar") == 0) {
        max_tiempo = atoi(argv[2]);
        comando_argv = &argv[3];
        comando_argc = argc - 3;
        inicio_comando = 3;
    }
    
    else {
        //aca, si no se escribe ejec o ejecsave despues de miprof, el comando funcionara como si llevara ejec
        comando_argv = &argv[1];
        comando_argc = argc - 1;
    }
    
    // primero verificamos si se ingresaron comandos, de no ser asi, lanzamos codigo de error y volvemos a estado de ingresar linea en consola para la lectura
    if (comando_argc < 1) {
        printf("Error: formato correcto: miprof [ |ejec|ejecsave archivo] <comando> [argumentos...]\n");
        printf("Ejemplos:\n");
        printf("  miprof ls\n");
        printf("  miprof ejec ls\n");
        printf("  miprof ejecsave resultado.txt ls\n");
        return;
    }
    

    
    if (output_file) {
        printf("\nResultados se guardarán en: %s\n\n", output_file);
    }
    
    
    
    pid_t pid = fork();
    
    if (pid == 0) {
        // proceso hijo ejecuta el comando
        if (max_tiempo > 0) {
            signal(SIGALRM, tiempo_agotado);
            alarm(max_tiempo);
        }
        
        

        if (tiene_pipes) {
            char **comando_con_pipes = &argv[inicio_comando];
            int argc_comando = argc - inicio_comando;
            
            ejecutar_comando_con_pipe(comando_con_pipes, argc_comando);
            exit(0);
        }else {
            // Comando simple
            execvp(comando_argv[0], comando_argv);
            exit(1);
        }
    }
    else if (pid > 0) {
        // proceso padre espera a que el proceso hijo termine y toma los datos de tiempo y uso
        int status;
        waitpid(pid, &status, 0);
        
        // detectar timeout en el padre
        if (WIFSIGNALED(status)) {
            int signal_num = WTERMSIG(status);
            if (signal_num == SIGALRM) {
                printf("Se agoto el tiempo, finalizando el proceso.\n");
            }
        }


        //finalizamos el contador de tiempo total
        clock_gettime(CLOCK_MONOTONIC, &fin);
        getrusage(RUSAGE_CHILDREN, &uso);
        
        //calculamos tiempo real, tiempo de usuario y tiempo de sistema
        double real_time = (fin.tv_sec - inicio.tv_sec) + 
                          (fin.tv_nsec - inicio.tv_nsec) / 1000000000.0;
        
        double user_time = uso.ru_utime.tv_sec + uso.ru_utime.tv_usec / 1000000.0;
        double system_time = uso.ru_stime.tv_sec + uso.ru_stime.tv_usec / 1000000.0;
        
        //  mostramos los resultados en pantalla
        printf("\nRESULTADOS MIPROF\n");
        printf("Comando ingresado: ");
        for (int i = 0; i < comando_argc; i++) {
            printf("%s ", comando_argv[i]);
        }
        printf("\n");
        printf("Tiempo real: %.6f segundos\n", real_time);
        printf("Tiempo usuario: %.6f segundos\n", user_time);
        printf("Tiempo sistema: %.6f segundos\n", system_time);
        printf("Memoria máxima: %ld KB\n", uso.ru_maxrss);
        
        //  en caso de haber outputfile al usar ejecsave, guardamos los resultados concatenandolos a los anteriores
        if (output_file) {
            FILE *archivo = fopen(output_file, "a");
            if (archivo) {
                fprintf(archivo, "RESULTADOS MIPROF\n");
                fprintf(archivo, "Comando: ");
                for (int i = 0; i < comando_argc; i++) {
                    fprintf(archivo, "%s ", comando_argv[i]);
                }
                fprintf(archivo, "\n");
                fprintf(archivo, "Tiempo real: %.6f segundos\n", real_time);
                fprintf(archivo, "Tiempo usuario: %.6f segundos\n", user_time);
                fprintf(archivo, "Tiempo sistema: %.6f segundos\n", system_time);
                fprintf(archivo, "Memoria máxima: %ld KB\n", uso.ru_maxrss);
                fprintf(archivo, "\n");
                fclose(archivo);
                printf("Resultados guardados en: %s\n", output_file);
            } else {
                printf("Error abriendo archivo: %s\n", output_file);
            }
        }
    }
    else {
        perror("Error en fork");
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