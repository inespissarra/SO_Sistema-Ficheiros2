#include "operations.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

#define S 20

static pthread_cond_t cond[S];
static pthread_mutex_t global_lock[S];

typedef struct{
    int status;
    char op_code, name[40], *buffer;
    int fhandle, session_id, flags;
    size_t len;
} Data;

Data datas[S];

int insert_in_sessions(int fcli);
int sessions[S];
int sessions_ids[S];
int shutdown;
pthread_t tid[S];
void server_open(int), server_close(int), server_write(int), server_read(int), server_shutdown_after_all_closed(int), *wait_for_signal(void*);
void read_from_pipe(int, void*, size_t), write_pipe(int, void*, size_t);

int main(int argc, char **argv) {

    if (argc < 2) {
        printf("Please specify the pathname of the server's pipe.\n");
        return 1;
    }

    for(int i = 0; i < S; i++){
        if (pthread_mutex_init(&global_lock[i], 0) != 0)
            exit(1);
        if (pthread_cond_init(&cond[i], 0) != 0)
            exit(1);
        sessions_ids[i] = i;
        if(pthread_create(&tid[i], NULL, wait_for_signal, (void*)&sessions_ids[i]) != 0)
            exit(1);
        datas[i].status = 0;
        sessions[i] = -1;
    }
    if(tfs_init() < 0)
        exit(1);

    char *pipename = argv[1];
    printf("Starting TecnicoFS server with pipe called %s\n", pipename);

    signal(SIGPIPE, SIG_IGN);

    unlink(pipename);

    if (mkfifo(pipename, 0777) < 0)
        exit(1);

    int fserv, fcli, session_id;

    shutdown = 0;

    while(1){
        fserv = open(pipename, O_RDONLY);
        if(fserv >= 0){
            break;
        } else if(errno != EINTR)
            exit(1);
    }

    while(1){
        char op_code = '\0';
        ssize_t ret = read(fserv, &op_code, sizeof(char));

        if(shutdown){
            break;
        }

        if(ret == 0){
            while(1){
                fserv = open(pipename, O_RDONLY);
                if(fserv >= 0){
                    break;
                } else if(errno != EINTR)
                    exit(1);
            }
        } else if(ret != sizeof(char))
            exit(1);

        if(op_code == TFS_OP_CODE_MOUNT){

            char client_pipe[40];

            read_from_pipe(fserv, &client_pipe, 40);

            while(1){
                fcli = open(client_pipe, O_WRONLY);
                if(fcli >= 0){
                    break;
                } else if(errno != EINTR)
                    exit(1);
            }

            if((session_id = insert_in_sessions(fcli)) == -1){
                while(1){
                    int ret2 = close(fcli);
                    if(ret2 >= 0){
                        break;
                    } else if(errno != EINTR)
                        exit(1);
                }
                exit(1);    
            }

            write_pipe(fcli, &session_id, sizeof(int));

        } else if(op_code == TFS_OP_CODE_UNMOUNT) {

            int cur_session;

            read_from_pipe(fserv, &cur_session, sizeof(int));

            while(1){
                int ret2 = close(sessions[cur_session]);
                if(ret2 >= 0){
                    break;
                } else if(errno != EINTR)
                    exit(1);
            }

            sessions[cur_session] = -1;

        } else if(op_code == TFS_OP_CODE_OPEN) {
            int cur_session;

            read_from_pipe(fserv, &cur_session, sizeof(int));

            if (pthread_mutex_lock(&global_lock[cur_session]) != 0)
                exit(1);

            read_from_pipe(fserv, datas[cur_session].name, 40);

            read_from_pipe(fserv, &datas[cur_session].flags, sizeof(int));

            datas[cur_session].op_code = TFS_OP_CODE_OPEN;
            datas[cur_session].status = 1;
            
            if(pthread_cond_signal(&cond[cur_session])!=0)
                exit(1);

            if (pthread_mutex_unlock(&global_lock[cur_session]) != 0)
                exit(1);

        } else if(op_code == TFS_OP_CODE_CLOSE) {

            int cur_session;

            read_from_pipe(fserv, &cur_session, sizeof(int));

            if (pthread_mutex_lock(&global_lock[cur_session]) != 0)
                exit(1);

            read_from_pipe(fserv, &datas[cur_session].fhandle, sizeof(int));

            datas[cur_session].op_code = TFS_OP_CODE_CLOSE;

            datas[cur_session].status = 1;

            if(pthread_cond_signal(&cond[cur_session])!=0)
                exit(1);

            if (pthread_mutex_unlock(&global_lock[cur_session]) != 0)
                exit(1);

        } else if(op_code == TFS_OP_CODE_WRITE) {

            int cur_session;

            read_from_pipe(fserv, &cur_session, sizeof(int));

            if (pthread_mutex_lock(&global_lock[cur_session]) != 0)
                exit(1);

            read_from_pipe(fserv, &datas[cur_session].fhandle, sizeof(int));

            read_from_pipe(fserv, &datas[cur_session].len, sizeof(size_t));
            
            datas[cur_session].buffer = (char*) malloc(datas[cur_session].len * sizeof(char));

            read_from_pipe(fserv, datas[cur_session].buffer, datas[cur_session].len);

            datas[cur_session].op_code = TFS_OP_CODE_WRITE;
            datas[cur_session].status = 1;

            if(pthread_cond_signal(&cond[cur_session])!=0){
                free(datas[cur_session].buffer);
                exit(1);
            }

            if (pthread_mutex_unlock(&global_lock[cur_session]) != 0){
                free(datas[cur_session].buffer);
                exit(1);
            }

        } else if(op_code == TFS_OP_CODE_READ) {

            int cur_session;

            read_from_pipe(fserv, &cur_session, sizeof(int));

            if (pthread_mutex_lock(&global_lock[cur_session]) != 0)
                exit(1);

            read_from_pipe(fserv, &datas[cur_session].fhandle, sizeof(int));

            read_from_pipe(fserv, &datas[cur_session].len, sizeof(size_t));

            datas[cur_session].op_code = TFS_OP_CODE_READ;
            datas[cur_session].status = 1;

            if(pthread_cond_signal(&cond[cur_session])!=0)
                exit(1);

            if (pthread_mutex_unlock(&global_lock[cur_session]) != 0)
                exit(1);

        } else if(op_code == TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED) {

            int cur_session;

            read_from_pipe(fserv, &cur_session, sizeof(int));

            if (pthread_mutex_lock(&global_lock[cur_session]) != 0){
                exit(1);
            }

            datas[cur_session].op_code = TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED;
            datas[cur_session].status = 1;

            if(pthread_cond_signal(&cond[cur_session])!=0){
                exit(1);
            }

            if (pthread_mutex_unlock(&global_lock[cur_session]) != 0)
                exit(1);

        }
    }

    if(tfs_destroy() < 0){
        exit(1);
    }


    for(int i = 0; i < S; i++){
        if (pthread_mutex_destroy(&global_lock[i]) != 0)
            exit(1);
        if (pthread_cond_destroy(&cond[i]) != 0)
            exit(1);
        if(sessions[i]!=-1){
            while(1){
                int ret2 = close(sessions[i]);
                if(ret2 >= 0){
                    break;
                } else if(errno != EINTR)
                    exit(1);
            }
        }
    }

    while(1){
        int ret2 = close(fserv);
        if(ret2 >= 0){
            break;
        } else if(errno != EINTR)
            exit(1);
    }

    unlink(pipename);

    return 0;

}

int insert_in_sessions(int fcli) {

    for(int i = 0; i < S; i++)

        if(sessions[i] == -1){

            sessions[i] = fcli;

            return i;

        }

    return -1;
}

void server_open(int session){ 
    int ret = tfs_open(datas[session].name, datas[session].flags);

    write_pipe(sessions[session], &ret, sizeof(int));

}

void server_close(int session){
    int ret = tfs_close(datas[session].fhandle);

    write_pipe(sessions[session], &ret, sizeof(int));
}

void server_write(int session){
    ssize_t ret = tfs_write(datas[session].fhandle, datas[session].buffer, datas[session].len);

    while(1){
        ssize_t ret2 = write(sessions[session], &ret, sizeof(ssize_t));
        if(ret2 == sizeof(ssize_t)){
            break;
        } else if(ret2 < 0 && errno != EINTR){
            free(datas[session].buffer);
            exit(1);
        }
    }
    
    free(datas[session].buffer);
}

void server_read(int session){
    datas[session].buffer = (char*) malloc(datas[session].len * sizeof(char));

    ssize_t ret = tfs_read(datas[session].fhandle, datas[session].buffer, datas[session].len);

    while(1){
        ssize_t ret2 = write(sessions[session], &ret, sizeof(ssize_t));
        if(ret2 == sizeof(ssize_t)){
            break;
        } else if(ret2 < 0 && errno != EINTR){
            free(datas[session].buffer);
            exit(1);
        }
    }

    while(1){
        ssize_t ret2 = write(sessions[session], datas[session].buffer, (size_t)ret);
        if(ret2 == ret){
            break;
        } else if(ret2 < 0 && errno != EINTR){
            free(datas[session].buffer);
            exit(1);
        }
    }

    free(datas[session].buffer);
}

void server_shutdown_after_all_closed(int session){
    int ret = tfs_destroy_after_all_closed();

    shutdown = 1;

    write_pipe(sessions[session], &ret, sizeof(int));
}

void *wait_for_signal(void* arg){
    
    int session_id = *((int*)arg);
    if (pthread_mutex_lock(&global_lock[session_id]) != 0){
        exit(1);
    }

    while(datas[session_id].status == 0)
        if(pthread_cond_wait(&cond[session_id], &global_lock[session_id])!=0){
            exit(1);
        }

    if (datas[session_id].op_code == TFS_OP_CODE_OPEN) {
        server_open(session_id);
    } else if (datas[session_id].op_code == TFS_OP_CODE_CLOSE) {
        server_close(session_id);
    } else if (datas[session_id].op_code == TFS_OP_CODE_WRITE) {
        server_write(session_id);
    } else if (datas[session_id].op_code == TFS_OP_CODE_READ) {
        server_read(session_id);
    } else if (datas[session_id].op_code == TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED){
        server_shutdown_after_all_closed(session_id);
    }

    datas[session_id].status = 0;
    if (pthread_mutex_unlock(&global_lock[session_id]) != 0){
        exit(1);
    }

    wait_for_signal(arg);
    
    return NULL;
}

void read_from_pipe(int pipe, void* buffer, size_t len){
    ssize_t offset = 0;
    while(1){
        offset += read(pipe, buffer, len);
        if(offset == len){
            break;
        } else if(offset < 0 && errno != EINTR){
            exit(1);
        }
    }
}

void write_pipe(int pipe, void* buffer, size_t len){
    while(1){
        ssize_t ret_value = write(pipe, buffer, len);
        if(ret_value == len){
            break;
        } else if(ret_value < 0 && errno != EINTR){
            exit(1);
        }
    }
}