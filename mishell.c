#define _GNU_SOURCE

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


        ejecutar_comando(argv);
                  

    }


    return 0;
}