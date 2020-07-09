#ifndef _CUDA_API_H_
#define _CUDA_API_H_

#include <stdio.h>
#include <stdarg.h>
typedef struct int_dim3
{
	int x, y, z;
}int_dim3;

typedef struct int_dim4
{
	int n, c, h, w;
}int_dim4;


/**
 * @file
 * @ingroup dlp-framework
 * Deep Learning framework C-API header
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <cuda.h>
#include <setjmp.h>

/**
 * @ingroup gpu_module
 * @{
 * 
 * @defgroup cuda_context CUDA Driver API wrapper
 * @{
 * @defgroup cuda_device Device Management
 * @{
 * @}
 * 
 * @defgroup cuda_module Module Management
 * @{
 * 
 * @defgroup cuda_function Execution Control
 * @{
 * @}
 * 
 * @}
 * 
 * @}
 * 
 * @}
 */

/**
 * @ingroup cuda_function
 * @{
 */
typedef struct cuda_function cuda_function_t;
enum cuda_function_description_parse_stage
{
	function_description_parse_stage_invalid = -1,
	function_description_parse_stage_unknown,
	function_description_parse_stage_begin,
	function_description_parse_stage_params,
	function_description_parse_stage_final,
};

typedef struct cuda_function_description
{
	char func_name[200];
	int num_params;
	int index;
	enum cuda_function_description_parse_stage stage;
}cuda_function_description_t;

struct cuda_function
{
	union
	{
		cuda_function_description_t desc[1];
		struct
		{
			char func_name[200];
			int num_params;
			int index;
		};
	};
	
	CUfunction kernel;			///< CUfunction
	int_dim3 grid;				///< blocks dim in grid
	int_dim3 block;				///< threads dim in block
	
	unsigned int shared_memory_size;	///< 
	CUstream shared_mem;
	
//	size_t params_count;
	void ** params;
	
	size_t extras_count;
	void ** extras;						///< extra params
	
	int (* execute)(cuda_function_t * func);
	int (* set_blocks)(cuda_function_t * func, size_t elements_count, size_t block_size);		// blocks = ( grid.x * grid.y * grid.z) = (elements_count + block_size - 1) / blocks_size
	int (* set_params)(cuda_function_t * func, size_t params_count, ...);
};
cuda_function_t * cuda_function_init(cuda_function_t * func, const char * func_name, CUfunction kernel, size_t params_count, size_t extras_count);
void cuda_function_cleanup(cuda_function_t * func);

//~ int cuda_function_exec(cuda_function_t * func, size_t params_count, void * params[]);
//~ int cuda_function_execv(cuda_function_t * func, size_t params_count, ...);
//~ int cuda_function_set_blocks(cuda_function_t * func, size_t elements_count, size_t block_size);
/**
 * @}
 */


typedef struct cuda_module cuda_module_t; 
/** 
 * @ingroup cuda_module
 * @{
 */
enum cuda_source_type
{
	cuda_source_type_unknown,
	cuda_source_type_source_code,
	cuda_source_type_assembly,
	cuda_source_type_object,
	cuda_source_type_module,
	cuda_source_types_count,
};
#define CUDA_BASELINE_MAX_FUNCTIONS	(4096)
struct cuda_module
{
	void * user_data;
	struct cuda_context * cuda;
	struct cuda_device * dev;
	CUmodule ctx;
	size_t functions_count;
	cuda_function_t functions[CUDA_BASELINE_MAX_FUNCTIONS];
	void * functions_table;	// tsearch root
	
	int (* link)(struct cuda_module * module, const char * kernels_path, int gpu_arch);
	int (* load)(struct cuda_module * module, void * cubin_data, size_t cubin_size);
	int (* add_function)(struct cuda_module * module, const cuda_function_description_t * desc);
	cuda_function_t * (* get_function)(struct cuda_module * module, const char * func_name);
	
	void * priv;
	int arch;
	int (* link_begin)(struct cuda_module * module, int arch);
	int (* add_kernel_file)(struct cuda_module * module, enum cuda_source_type source_type, const char * kernel_file);
	int (* add_kernel_data)(struct cuda_module * module, enum cuda_source_type source_type, const unsigned char * kernel_data, size_t length);
	int (* link_final)(struct cuda_module * module);
};
cuda_module_t * cuda_module_init(cuda_module_t * module, struct cuda_context * cuda, int device_index, void * user_data);
void cuda_module_cleanup(cuda_module_t * module);

#define cuda_module_attach(module) do {									\
		assert(module && module->dev && module->dev->context);			\
		CUresult ret = cuCtxSetCurrent(module->dev->context);			\
		cuda_check_error(module->cuda, ret, cuCtxSetCurrent);			\
	} while(0)

#define cuda_module_detach(module) do {									\
		assert(module && module->dev && module->dev->context);			\
		CUresult ret = cuCtxSetCurrent(NULL);							\
		cuda_check_error(module->cuda, ret, cuCtxSetCurrent);			\
	} while(0)


int cuda_module_save_module(cuda_module_t * module, const char * module_file, const char * def_file);
int cuda_module_load_module(cuda_module_t * module, const char * module_file, const char * def_file);

/**
 * @}
 */


struct cuda_error_mgr;
typedef void (* cuda_error_handler_callback_ptr)(struct cuda_error_mgr * mgr, CUresult error);

typedef struct cuda_error_mgr
{
	void * user_data;
	const char * prefix;
	jmp_buf jmp;

	void (* error_handler)(struct cuda_error_mgr * mgr, CUresult error);
	
	const char * err_name;
	const char * err_desc;
	char err_msg[4096];
}cuda_error_mgr_t;
cuda_error_mgr_t * cuda_error_mgr_init(cuda_error_mgr_t * mgr, cuda_error_handler_callback_ptr error_handler, void * user_data);



/** 
 * @ingroup cuda_device
 * @{
 */
#define CUDA_CONTEXT_MAX_DEVICES 	(4096)
#define CUDA_CONTEXT_MAX_NAME_LEN	(256)
typedef struct cuda_device
{
	CUdevice device;
	int index;
	char name[CUDA_CONTEXT_MAX_NAME_LEN];
	CUuuid uuid[1];
	size_t total_memory;
	
	int arch;		// gpu architecture, default: 61 ( sm_61, Nvidia Quadro P5000 serials )
	int attributes[CU_DEVICE_ATTRIBUTE_MAX];
	int disabled;
	CUcontext context;
}cuda_device_t;
int cuda_device_attach(struct cuda_context * cuda, cuda_device_t * dev);
int cuda_device_detach(struct cuda_context * cuda, cuda_device_t * dev);
/**
 * @}
 */
 
 

/** 
 * @ingroup cuda_context
 * @{
 */
 
typedef struct cuda_context
{
	void * user_data;
	cuda_error_mgr_t * error_mgr;
	

	int devices_count;
	cuda_device_t devices[CUDA_CONTEXT_MAX_DEVICES];
}cuda_context_t;
cuda_context_t * cuda_context_init(cuda_context_t * cuda, void * user_data);
void cuda_context_cleanup(cuda_context_t * cuda);

cuda_context_t * cuda_context_get_default();

int cuda_device_new_context(cuda_context_t * cuda, cuda_device_t * dev, unsigned int flags);


#define cuda_check_error(cuda, ret, func)	do {								\
		if(ret != CUDA_SUCCESS) {												\
			fprintf(stderr, "[ERROR]::%s()::%s() --> ret=%d\n", __FUNCTION__, 	\
				#func, (int)ret);												\
			cuda->error_mgr->error_handler(cuda->error_mgr, ret);				\
		}																		\
	} while(0)
	
#define cuda_exec(cuda, func, ...) do {									\
		CUresult ret = func(__VA_ARGS__);								\
		if(ret != CUDA_SUCCESS) cuda_check_error(cuda, ret, func);		\
	}while(0)


/**
 * @}
 */
 
const char * cuda_strerror(int ret);


#ifdef __cplusplus
}
#endif
#endif
