#include "tecnicofs_client_api.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

int fserv, fcli, session_id;
char client_pipe[40];

int tfs_mount(const char *client_pipe_path, char const *server_pipe_path) {
    unlink(client_pipe_path);

    if (mkfifo(client_pipe_path, 0777) < 0){
        return -1;
    }

    while(1){
        fserv = open(server_pipe_path, O_WRONLY);
        if(fserv >= 0){
            break;
        } else if(errno != EINTR)
            return -1;
    }

    int a = (int)strlen(client_pipe_path);
    strcpy(client_pipe, client_pipe_path);
    memset(client_pipe + a, '\0', (size_t)(40 - a));

    char op = TFS_OP_CODE_MOUNT;
    size_t i = sizeof(char) + 40;
    char buffer[i];
    memcpy(buffer, &op, sizeof(char));
    memcpy(buffer + sizeof(char), client_pipe, 40);


    while(1){
        ssize_t ret2 = write(fserv, buffer, i);
        if(ret2 == i){
            break;
        } else if(ret2 < 0 && errno != EINTR)
            return -1;
    }

    while(1){
        fcli = open(client_pipe, O_RDONLY);
        if(fcli >= 0){
            break;
        } else if(errno != EINTR)
            return -1;
    }

    ssize_t offset = 0;
    while(1){
        offset += read(fcli, &session_id, sizeof(int));
        if(offset == sizeof(int)){
            break;
        } else if(offset < 0 && errno != EINTR){
            return -1;
        }
    }
    
    return 0;
}

int tfs_unmount() {
    char op = TFS_OP_CODE_UNMOUNT;
    size_t i = sizeof(char) + sizeof(int);
    char buffer[i];
    memcpy(buffer, &op, sizeof(char));
    memcpy(buffer + sizeof(char), &session_id, sizeof(int));

    while(1){
        ssize_t ret2 = write(fserv, buffer, i);
        if(ret2 == i){
            break;
        } else if(ret2 < 0 && errno != EINTR)
            return -1;
    }

    while(1){
        int ret2 = close(fcli);
        if(ret2 >= 0){
            break;
        } else if(errno != EINTR)
            return -1;
    }

    while(1){
        int ret2 = close(fserv);
        if(ret2 >= 0){
            break;
        } else if(errno != EINTR)
            return -1;
    }

    unlink(client_pipe);
    
    return 0;
}

int tfs_open(char const *name, int flags) {
    char op = TFS_OP_CODE_OPEN;
    int ret;
    size_t i = sizeof(char) + 2 * sizeof(int) + 40, offset = 0;
    char buffer[i];

    memcpy(buffer, &op, sizeof(char));
    offset += sizeof(char);
    memcpy(buffer + offset, &session_id, sizeof(int));
    offset += sizeof(int);
    size_t a = strlen(name);
    memcpy(buffer + offset, name, a);
    offset += a;
    memset(buffer + offset, '\0', (size_t)(40 - a));
    offset += 40 - a;
    memcpy(buffer + offset, &flags, sizeof(int));

    while(1){
        ssize_t ret2 = write(fserv, buffer, i);
        if(ret2 == i){
            break;
        } else if(ret2 < 0 && errno != EINTR)
            return -1;;
    }

    ssize_t offset2 = 0;
    while(1){
        offset2 += read(fcli, &ret, sizeof(int));
        if(offset2 == sizeof(int)){
            break;
        } else if(offset2 < 0 && errno != EINTR){
            return -1;
        }
    }

    return ret;
}

int tfs_close(int fhandle) {
    char op = TFS_OP_CODE_CLOSE;
    int ret;

    size_t i = sizeof(char) + 2 * sizeof(int);
    char buffer[i];
    size_t offset = 0;
    memcpy(buffer, &op, sizeof(char));
    offset += sizeof(char);
    memcpy(buffer + offset, &session_id, sizeof(int));
    offset += sizeof(int);
    memcpy(buffer + offset, &fhandle, sizeof(int));

    while(1){
        ssize_t ret2 = write(fserv, buffer, i);
        if(ret2 == i){
            break;
        } else if(ret2 < 0 && errno != EINTR)
            return -1;;
    }

    ssize_t offset2 = 0;
    while(1){
        offset2 += read(fcli, &ret, sizeof(int));
        if(offset2 == sizeof(int)){
            break;
        } else if(offset2 < 0 && errno != EINTR){
            return -1;
        }
    }

    return ret;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t len) {
    char op = TFS_OP_CODE_WRITE;
    ssize_t ret;

    size_t i = sizeof(char) + 2 * sizeof(int) + sizeof(size_t) + len;
    char buffer2[i];
    size_t offset = 0;
    memcpy(buffer2, &op, sizeof(char));
    offset += sizeof(char);
    memcpy(buffer2 + offset, &session_id, sizeof(int));
    offset += sizeof(int);
    memcpy(buffer2 + offset, &fhandle, sizeof(int));
    offset += sizeof(int);
    memcpy(buffer2 + offset, &len, sizeof(size_t));
    offset += sizeof(size_t);
    memcpy(buffer2 + offset, buffer, len);

    while(1){
        ssize_t ret2 = write(fserv, buffer2, i);
        if(ret2 == i){
            break;
        } else if(ret2 < 0 && errno != EINTR)
            return -1;;
    }

    ssize_t offset2 = 0;
    while(1){
        offset2 += read(fcli, &ret, sizeof(ssize_t));
        if(offset2 == sizeof(ssize_t)){
            break;
        } else if(offset2 < 0 && errno != EINTR){
            return -1;
        }
    }

    return ret;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    char op = TFS_OP_CODE_READ;
    ssize_t ret;

    size_t i = sizeof(char) + 2 * sizeof(int) + sizeof(size_t);
    char buffer2[i];
    size_t offset = 0;
    memcpy(buffer2, &op, sizeof(char));
    offset += sizeof(char);
    memcpy(buffer2 + offset, &session_id, sizeof(int));
    offset += sizeof(int);
    memcpy(buffer2 + offset, &fhandle, sizeof(int));
    offset += sizeof(int);
    memcpy(buffer2 + offset, &len, sizeof(size_t));

    while(1){
        ssize_t ret2 = write(fserv, buffer2, i);
        if(ret2 == i){
            break;
        } else if(ret2 < 0 && errno != EINTR)
            return -1;;
    }

    ssize_t offset2 = 0;
    while(1){
        offset2 += read(fcli, &ret, sizeof(ssize_t));
        if(offset2 == sizeof(ssize_t)){
            break;
        } else if(offset2 < 0 && errno != EINTR){
            return -1;
        }
    }

    offset2 = 0;
    while(1){
        offset2 += read(fcli, buffer, (size_t)ret);
        if(offset2 == ret){
            break;
        } else if(offset2 < 0 && errno != EINTR){
            return -1;
        }
    }

    return ret;

}

int tfs_shutdown_after_all_closed() {
    char op = TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED;
    int ret;

    size_t i = sizeof(char) + sizeof(int);
    char buffer[i];
    memcpy(buffer, &op, sizeof(char));
    memcpy(buffer + sizeof(char), &session_id, sizeof(int));

    while(1){
        ssize_t ret2 = write(fserv, buffer, i);
        if(ret2 == i){
            break;
        } else if(ret2 < 0 && errno != EINTR)
            return -1;;
    }

    ssize_t offset = 0;
    while(1){
        offset += read(fcli, &ret, sizeof(int));
        if(offset == sizeof(int)){
            break;
        } else if(offset < 0 && errno != EINTR){
            return -1;
        }
    }

    while(1){
        int ret2 = close(fcli);
        if(ret2 >= 0){
            break;
        } else if(errno != EINTR)
            return -1;
    }

    unlink(client_pipe);

    return ret;

}