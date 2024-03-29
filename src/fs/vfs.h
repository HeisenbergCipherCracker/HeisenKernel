#include <video.h>
#include <vfs.h>
#include <stdio.h>
#ifdef LEGACY
#include <kheap.h>
#endif
#ifdef LATEST
#include <heap.h>
#endif
#include <stddef.h>
#include <string.h>
#include <kheap.h>
#include <unistd.h>
#include <initrd.h>
#include <dirent.h>
#include <kernel.h>
#include <shell.h>

struct mountpoint_t mountpoint_list[MAX_MOUNTPOINT];
file_descriptor_t fd_list[_SC_OPEN_MAX];
char *module_start;
int cur_fd;

void vfs_init(){
	int i,j;
	i=0;
	j=0;
	cur_fd=0;
	
	printf(LNG_VFS); 	
 	
 	while(i<MAX_MOUNTPOINT) {
 		strcpy(mountpoint_list[i].mountpoint,"");
 		mountpoint_list[i].uid = 0;
 		mountpoint_list[i].gid = 0;
 		mountpoint_list[i].pmask = 0;
 		mountpoint_list[i].dev_id = 0;
	 	mountpoint_list[i].start_address = 0;	 	
	 	mountpoint_list[i].dir_op.opendir_f = NULL;
		mountpoint_list[i].operations.open = NULL;
		mountpoint_list[i].operations.close = NULL;
		mountpoint_list[i].operations.read = NULL;
		mountpoint_list[i].operations.write = NULL;
		mountpoint_list[i].stat_op.stat = NULL;
 		i++;
	}
	
 	strcpy(mountpoint_list[0].mountpoint,"/");
 	mountpoint_list[0].uid = 0;
 	mountpoint_list[0].gid = 0;
 	mountpoint_list[0].pmask = 0;
 	mountpoint_list[0].dev_id = 0;
 	mountpoint_list[0].start_address = (unsigned int) module_start;
 	mountpoint_list[0].dir_op.opendir_f = initfs_opendir;
 	mountpoint_list[0].dir_op.readdir_f = initrd_readdir;
 	mountpoint_list[0].operations.open = &initfs_open;
 	mountpoint_list[0].operations.close = &initrd_close;
 	mountpoint_list[0].operations.read = &initfs_read;
 	mountpoint_list[0].operations.write = &initrd_write;
 	mountpoint_list[0].stat_op.stat = initrd_stat;
 	//mountpoint_list[0].operations = kmalloc(sizeof(struct super_node_operations));

	strcpy(mountpoint_list[1].mountpoint,"/dev"); 	
	mountpoint_list[1].uid = 0;
 	mountpoint_list[1].gid = 0;
 	mountpoint_list[1].pmask = 0;
 	mountpoint_list[1].dev_id = 0;
 	mountpoint_list[1].start_address = 0;
 	//mountpoint_list[1].operations = kmalloc(sizeof(struct super_node_operations));
	
	strcpy(mountpoint_list[2].mountpoint,"/dev/video"); 	
	mountpoint_list[2].uid = 0;
 	mountpoint_list[2].gid = 0;
 	mountpoint_list[2].pmask = 0;
 	mountpoint_list[2].dev_id = 0;
 	mountpoint_list[2].start_address = 0;
 	//mountpoint_list[2].operations = kmalloc(sizeof(struct super_node_operations));
	
	strcpy(mountpoint_list[3].mountpoint,"/root");
 	mountpoint_list[3].uid = 0;
 	mountpoint_list[3].gid = 0;
 	mountpoint_list[3].pmask = 0;
 	mountpoint_list[3].dev_id = 0;
 	mountpoint_list[3].start_address = 0;
 	//mountpoint_list[3].operations = kmalloc(sizeof(struct super_node_operations));
 	
 		while (j < _SC_OPEN_MAX){
		fd_list[j].fs_spec_id = -1;
		fd_list[j].mountpoint_id = -1;
		fd_list[j].offset = 0;
		j++;
	}
	_kprintOK();
}

int get_mountpoint_id(char *path){
       int i = 0;
       int last=-1;
       while(i<MAX_MOUNTPOINT ){						
                       if(!_kstrncmp(path, mountpoint_list[i].mountpoint, strlen(mountpoint_list[i].mountpoint))) {                 
							   if(strlen(mountpoint_list[i].mountpoint) > strlen(mountpoint_list[last].mountpoint) && i>0)  
                               		last = i;
							   else if(i==0) last = i;
                       }					   					   
                       i++;
               }               
			   //if(last!=-1) printf("Changing dir %s\n", mountpoint_list[last].mountpoint, last);			   
               return last;
}

/**
  * @author Ivan Gualandri
  * @param int mountpoint_id id del punto di mountpoint del file
  * @param char* path percorso del file da aprire
  *
  * Dato un path viene estratto il percorso relativo, escluso il mountpoint.
  * @return path senza la parte relativa al mountpoint.
  */
char *get_rel_path(int mountpoint_id, const char* path){
	int rel_size = 0;
	int j=0;
	char *tmp_path;
	tmp_path = NULL;
	rel_size = strlen(path) - strlen(mountpoint_list[mountpoint_id].mountpoint);	
	//printf("rel_size: %d mp_id: %d\n", rel_size, mountpoint_id);
	if(rel_size>0){		
		int mp_size= 0;
		tmp_path = kmalloc(rel_size * sizeof(char));		
		mp_size = strlen(mountpoint_list[mountpoint_id].mountpoint);		
		while(j<rel_size){			
			tmp_path[j] = path[mp_size + j];			
			j++;
		}
		tmp_path[j]='\0';
	} 
	else strcpy(tmp_path, path);
	//printf("Tmp path: %s\n", tmp_path);
	return tmp_path;
}

/**
  * @author Ivan Gualandri
  * @param char* path percorso del file da aprire  
  *
  * Dato un path ne estrae il percorso assoluto (a partire da quello corrente)
  * @return error code  
  */
int get_abs_path(char *path){
		if(path[0]!='/') {			
		char abspath[CURPATH_LEN];
		int abs_size = 0;
		abs_size = strlen(current_user.cur_path);		
		memset(abspath, '\0', CURPATH_LEN);
		//printf("Abs: %s\n", abspath);
		strcpy(abspath, current_user.cur_path);
		if(abspath[abs_size-1] == '/')
			strncat(abspath, path, strlen(path));
		else {
			strncat(abspath, "/", strlen(path));
			strncat(abspath, path, strlen(path));
		}
		//printf("abspath: %s\n", abspath);
		strcpy(path, abspath);				
		return strlen(path);
	}
	else return 0;
}