#ifndef _SHELL_H
#define _SHELL_H

#define USER_LEN 22
#define CMD_LEN 256
#define DESC_LEN 256
#define CURPATH_LEN 256
#define NUM_COM 22
#define DEF_COM 18
#define HST_LEN 10


struct cmd {
	char cmdname[CMD_LEN];
	void (*h_func)(void);
	char cmddesc[DESC_LEN];
};

struct user_env {
	char username[USER_LEN];
	char cur_path[CURPATH_LEN];
	unsigned int uid;
	unsigned int gid;
};

//extern char cur_path[256];
extern struct cmd shell_cmd[NUM_COM];
typedef struct user_env userenv_t;
extern userenv_t current_user;
void shell_init();
int shell(void *);
void history (char *);
void history_start(void);
void _getCommand(char *);

#endif