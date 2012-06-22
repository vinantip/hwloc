#ifndef HWLOC_BACKEND_H
#define HWLOC_BACKEND_H

#include <hwloc/autogen/config.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif
	
struct hwloc_topology;

/*typedef enum {*/
/*HWLOC_BACKEND_GLOBAL, *//* XML, Synthetic, ... */ 
/*HWLOC_BACKEND_BASE, *//* OS */
/*HWLOC_BACKEND_IO *//* PCI, CUDA, ... */
/*} hwloc_backend_type;*/

struct hwloc_backend_st{
	/*hwloc_backend_type type;*/
	char* name;
	/*void* data; *//* Could be used for XML or linux (for fs_root) backends */
	void (*hwloc_look)(struct hwloc_topology*); /* Fill the bind functions pointers 
										 		* at least the set_cpubinvbd one */
	void (*hwloc_set_hooks)(struct hwloc_topology*); /* Set binding hooks */
	int (*hwloc_backend_init)(struct hwloc_topology*, ...); /* One topology should be passed in parameter */
	void (*hwloc_backend_exit)(struct hwloc_topology*);
};

struct hwloc_backends_loaded{
	struct hwloc_backend_st* backend;
	void* handle; /* The backend's handle */
	struct hwloc_backends_loaded* next;
};

/* Get the backend structure. Must be implemented by the backend dev */
HWLOC_DECLSPEC struct hwloc_backend_st* hwloc_get_backend(void); 

/* Use by hwloc */
extern struct hwloc_backends_loaded* hwloc_backend_load(char* path, char* backend_prefix);
extern void hwloc_backend_unload(struct hwloc_backends_loaded* backends_loaded);

#ifdef __cplusplus
}
#endif

#endif /* HWLOC_BACKEND_H */