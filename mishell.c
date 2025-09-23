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


void leer_linea(char *linea, size_t tamaño_de_la_linea){

    //muestra el
    printf("mishell:$ ");
    fflush(stdout);
    //prompt

    fgets(linea,tamaño_de_la_linea, stdin);
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

        if (pipe){
            ejecutar_comando_con_pipe(argv,argc);

        }
        else{
            ejecutar_comando(argv);
        }
       
           
        

    }



    return 0;
}