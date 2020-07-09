/*
 * cuda-api.c
 * 
 * Copyright 2019 chehw <htc.chehw@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>
#include <pthread.h>

#include <search.h>
#include "cuda-api.h"

#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>

#include "utils.h"
#include <math.h>

#define trim(p, p_end)	 trim_right(trim_left(p, p_end), p_end)
#define is_white_char(c) 	((c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n')
char * trim_left(char * p_begin, char * p_end)
{
	assert(p_begin);
	if(NULL == p_end) p_end = p_begin + strlen(p_begin);
	while(p_begin < p_end && is_white_char(*p_begin)) ++p_begin;
	
	return p_begin;
}
char * trim_right(char * p_begin, char * p_end)
{
    assert(p_begin);
    if(NULL == p_end) p_end = p_begin + strlen(p_begin);
    while(p_end > p_begin && is_white_char(p_end[-1])) *--p_end = '\0';
    return p_begin;
}


ssize_t load_binary_data(const char * filename, unsigned char **p_dst)
{
	struct stat st[1];
	int rc;
	rc = stat(filename, st);
	if(rc)
	{
		fprintf(stderr, "[ERROR]::%s(%d)::%s(%s)::stat::%s\n", 
			__FILE__, __LINE__, __FUNCTION__, filename,
			strerror(rc));
		return -1;
	}
	
	if(!S_ISREG(st->st_mode) )
	{
		fprintf(stderr, "[ERROR]::%s(%d)::%s(%s)::not regular file!\n", 
			__FILE__, __LINE__, __FUNCTION__, filename);
		return -1;
	}
	
	ssize_t size = st->st_size;
	if(size <= 0)
	{
		fprintf(stderr, "[ERROR]::%s(%d)::%s(%s)::invalid file-length: %ld!\n", 
			__FILE__, __LINE__, __FUNCTION__, filename,
			(long)size
		);
		return -1;
	}
	if(NULL == p_dst) return (size + 1);		// return buffer size	( append '\0' for ptx file)
	
	FILE * fp = fopen(filename, "rb");
	assert(fp);
	
	unsigned char * data = *p_dst;
	*p_dst = realloc(data, size + 1);
	assert(*p_dst);
	
	data = *p_dst;
	ssize_t length = fread(data, 1, size, fp);
	fclose(fp);
	
	assert(length == size);	
	data[length] = '\0';
	return length;
}


#define cuda_print_error(ret) do {													\
		const char * name = NULL;													\
		const char * err_msg = NULL;												\
		cuGetErrorName(ret, &name);													\
		cuGetErrorString(ret, &err_msg);											\
		fprintf(stderr, "[ERROR]::%s()::%s: %s\n", __FUNCTION__, name, err_msg);	\
	} while(0)
	
static int cuda_module_add_kernel_file(cuda_module_t * module, enum cuda_source_type source_type, const char * filename);
static int cuda_module_add_kernel_data(cuda_module_t * module, 
	enum cuda_source_type source_type, 
	const unsigned char * kernel_data,
	size_t length);
	
static int cuda_module_link_begin(struct cuda_module * module, int arch);
static int cuda_module_link_final(struct cuda_module * module);


typedef struct cuda_link_option
{
	int count;
	CUjit_option keys[CU_JIT_NUM_OPTIONS];
	void * values[CU_JIT_NUM_OPTIONS];
}cuda_link_option_t;

typedef struct cuda_module_private
{
	cuda_module_t * module;
	int arch;
	
	void * cubin_data;			// compiled object
	size_t cubin_length;
	
	CUlinkState linker;
	int is_compiling;
	CUresult err_code;
	cuda_link_option_t option[1];


}cuda_module_private_t;


/*************************************************************************************
 ** @ingroup cuda_device
**************************************************************************************/
/**
 * Device properties
 */
const char * s_cu_device_attribute_name[CU_DEVICE_ATTRIBUTE_MAX] = {
    [CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK] 				= "CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK", // 1 /**< Maximum number of threads per block */ 
    [CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X] 						= "CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X", // 2 /**< Maximum block dimension X */ 
    [CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y] 						= "CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y", // 3 /**< Maximum block dimension Y */ 
    [CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z] 						= "CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z", // 4 /**< Maximum block dimension Z */ 
    [CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X] 						= "CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X", // 5 /**< Maximum grid dimension X */ 
    [CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y] 						= "CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y", // 6 /**< Maximum grid dimension Y */ 
    [CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z] 						= "CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z", // 7 /**< Maximum grid dimension Z */ 
    [CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK] 			= "CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK", // 8 /**< Maximum shared memory available per block in bytes */ 
    [CU_DEVICE_ATTRIBUTE_SHARED_MEMORY_PER_BLOCK] 				= "CU_DEVICE_ATTRIBUTE_SHARED_MEMORY_PER_BLOCK", // 8 /**< Deprecated, use CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK */ 
    [CU_DEVICE_ATTRIBUTE_TOTAL_CONSTANT_MEMORY] 				= "CU_DEVICE_ATTRIBUTE_TOTAL_CONSTANT_MEMORY", // 9 /**< Memory available on device for __constant__ variables in a CUDA C kernel in bytes */ 
    [CU_DEVICE_ATTRIBUTE_WARP_SIZE] 							= "CU_DEVICE_ATTRIBUTE_WARP_SIZE", // 10 /**< Warp size in threads */ 
    [CU_DEVICE_ATTRIBUTE_MAX_PITCH] 							= "CU_DEVICE_ATTRIBUTE_MAX_PITCH", // 11 /**< Maximum pitch in bytes allowed by memory copies */ 
    [CU_DEVICE_ATTRIBUTE_MAX_REGISTERS_PER_BLOCK] 				= "CU_DEVICE_ATTRIBUTE_MAX_REGISTERS_PER_BLOCK", // 12 /**< Maximum number of 32-bit registers available per block */ 
    [CU_DEVICE_ATTRIBUTE_REGISTERS_PER_BLOCK] 					= "CU_DEVICE_ATTRIBUTE_REGISTERS_PER_BLOCK", // 12 /**< Deprecated, use CU_DEVICE_ATTRIBUTE_MAX_REGISTERS_PER_BLOCK */ 
    [CU_DEVICE_ATTRIBUTE_CLOCK_RATE] 							= "CU_DEVICE_ATTRIBUTE_CLOCK_RATE", // 13 /**< Typical clock frequency in kilohertz */ 
    [CU_DEVICE_ATTRIBUTE_TEXTURE_ALIGNMENT] 					= "CU_DEVICE_ATTRIBUTE_TEXTURE_ALIGNMENT", // 14 /**< Alignment requirement for textures */ 
    [CU_DEVICE_ATTRIBUTE_GPU_OVERLAP] 							= "CU_DEVICE_ATTRIBUTE_GPU_OVERLAP", // 15 /**< Device can possibly copy memory and execute a kernel concurrently. Deprecated. Use instead CU_DEVICE_ATTRIBUTE_ASYNC_ENGINE_COUNT. */ 
    [CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT] 					= "CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT", // 16 /**< Number of multiprocessors on device */ 
    [CU_DEVICE_ATTRIBUTE_KERNEL_EXEC_TIMEOUT] 					= "CU_DEVICE_ATTRIBUTE_KERNEL_EXEC_TIMEOUT", // 17 /**< Specifies whether there is a run time limit on kernels */ 
    [CU_DEVICE_ATTRIBUTE_INTEGRATED] 							= "CU_DEVICE_ATTRIBUTE_INTEGRATED", // 18 /**< Device is integrated with host memory */ 
    [CU_DEVICE_ATTRIBUTE_CAN_MAP_HOST_MEMORY] 					= "CU_DEVICE_ATTRIBUTE_CAN_MAP_HOST_MEMORY", // 19 /**< Device can map host memory into CUDA address space */ 
    [CU_DEVICE_ATTRIBUTE_COMPUTE_MODE] 							= "CU_DEVICE_ATTRIBUTE_COMPUTE_MODE", // 20 /**< Compute mode (See ::CUcomputemode for details) */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_WIDTH] 				= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_WIDTH", // 21 /**< Maximum 1D texture width */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_WIDTH] 				= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_WIDTH", // 22 /**< Maximum 2D texture width */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_HEIGHT] 				= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_HEIGHT", // 23 /**< Maximum 2D texture height */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_WIDTH] 				= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_WIDTH", // 24 /**< Maximum 3D texture width */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_HEIGHT] 				= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_HEIGHT", // 25 /**< Maximum 3D texture height */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_DEPTH] 				= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_DEPTH", // 26 /**< Maximum 3D texture depth */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LAYERED_WIDTH] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LAYERED_WIDTH", // 27 /**< Maximum 2D layered texture width */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LAYERED_HEIGHT] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LAYERED_HEIGHT", // 28 /**< Maximum 2D layered texture height */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LAYERED_LAYERS] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LAYERED_LAYERS", // 29 /**< Maximum layers in a 2D layered texture */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_ARRAY_WIDTH] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_ARRAY_WIDTH", // 27 /**< Deprecated, use CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LAYERED_WIDTH */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_ARRAY_HEIGHT] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_ARRAY_HEIGHT", // 28 /**< Deprecated, use CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LAYERED_HEIGHT */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_ARRAY_NUMSLICES] 	= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_ARRAY_NUMSLICES", // 29 /**< Deprecated, use CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LAYERED_LAYERS */ 
    [CU_DEVICE_ATTRIBUTE_SURFACE_ALIGNMENT] 					= "CU_DEVICE_ATTRIBUTE_SURFACE_ALIGNMENT", // 30 /**< Alignment requirement for surfaces */ 
    [CU_DEVICE_ATTRIBUTE_CONCURRENT_KERNELS] 					= "CU_DEVICE_ATTRIBUTE_CONCURRENT_KERNELS", // 31 /**< Device can possibly execute multiple kernels concurrently */ 
    [CU_DEVICE_ATTRIBUTE_ECC_ENABLED] 							= "CU_DEVICE_ATTRIBUTE_ECC_ENABLED", // 32 /**< Device has ECC support enabled */ 
    [CU_DEVICE_ATTRIBUTE_PCI_BUS_ID] 							= "CU_DEVICE_ATTRIBUTE_PCI_BUS_ID", // 33 /**< PCI bus ID of the device */ 
    [CU_DEVICE_ATTRIBUTE_PCI_DEVICE_ID] 						= "CU_DEVICE_ATTRIBUTE_PCI_DEVICE_ID", // 34 /**< PCI device ID of the device */ 
    [CU_DEVICE_ATTRIBUTE_TCC_DRIVER] 							= "CU_DEVICE_ATTRIBUTE_TCC_DRIVER", // 35 /**< Device is using TCC driver model */ 
    [CU_DEVICE_ATTRIBUTE_MEMORY_CLOCK_RATE] 					= "CU_DEVICE_ATTRIBUTE_MEMORY_CLOCK_RATE", // 36 /**< Peak memory clock frequency in kilohertz */ 
    [CU_DEVICE_ATTRIBUTE_GLOBAL_MEMORY_BUS_WIDTH] 				= "CU_DEVICE_ATTRIBUTE_GLOBAL_MEMORY_BUS_WIDTH", // 37 /**< Global memory bus width in bits */ 
    [CU_DEVICE_ATTRIBUTE_L2_CACHE_SIZE] 						= "CU_DEVICE_ATTRIBUTE_L2_CACHE_SIZE", // 38 /**< Size of L2 cache in bytes */ 
    [CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_MULTIPROCESSOR] 		= "CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_MULTIPROCESSOR", // 39 /**< Maximum resident threads per multiprocessor */ 
    [CU_DEVICE_ATTRIBUTE_ASYNC_ENGINE_COUNT] 					= "CU_DEVICE_ATTRIBUTE_ASYNC_ENGINE_COUNT", // 40 /**< Number of asynchronous engines */ 
    [CU_DEVICE_ATTRIBUTE_UNIFIED_ADDRESSING] 					= "CU_DEVICE_ATTRIBUTE_UNIFIED_ADDRESSING", // 41 /**< Device shares a unified address space with the host */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_LAYERED_WIDTH] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_LAYERED_WIDTH", // 42 /**< Maximum 1D layered texture width */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_LAYERED_LAYERS] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_LAYERED_LAYERS", // 43 /**< Maximum layers in a 1D layered texture */ 
    [CU_DEVICE_ATTRIBUTE_CAN_TEX2D_GATHER] 						= "CU_DEVICE_ATTRIBUTE_CAN_TEX2D_GATHER", // 44 /**< Deprecated, do not use. */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_GATHER_WIDTH] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_GATHER_WIDTH", // 45 /**< Maximum 2D texture width if CUDA_ARRAY3D_TEXTURE_GATHER is set */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_GATHER_HEIGHT] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_GATHER_HEIGHT", // 46 /**< Maximum 2D texture height if CUDA_ARRAY3D_TEXTURE_GATHER is set */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_WIDTH_ALTERNATE] 	= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_WIDTH_ALTERNATE", // 47 /**< Alternate maximum 3D texture width */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_HEIGHT_ALTERNATE] 	= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_HEIGHT_ALTERNATE", // 48 /**< Alternate maximum 3D texture height */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_DEPTH_ALTERNATE] 	= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_DEPTH_ALTERNATE", // 49 /**< Alternate maximum 3D texture depth */ 
    [CU_DEVICE_ATTRIBUTE_PCI_DOMAIN_ID] 						= "CU_DEVICE_ATTRIBUTE_PCI_DOMAIN_ID", // 50 /**< PCI domain ID of the device */ 
    [CU_DEVICE_ATTRIBUTE_TEXTURE_PITCH_ALIGNMENT] 				= "CU_DEVICE_ATTRIBUTE_TEXTURE_PITCH_ALIGNMENT", // 51 /**< Pitch alignment requirement for textures */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURECUBEMAP_WIDTH] 			= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURECUBEMAP_WIDTH", // 52 /**< Maximum cubemap texture width/height */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURECUBEMAP_LAYERED_WIDTH] 	= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURECUBEMAP_LAYERED_WIDTH", // 53 /**< Maximum cubemap layered texture width/height */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURECUBEMAP_LAYERED_LAYERS] = "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURECUBEMAP_LAYERED_LAYERS", // 54 /**< Maximum layers in a cubemap layered texture */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE1D_WIDTH] 				= "CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE1D_WIDTH", // 55 /**< Maximum 1D surface width */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_WIDTH] 				= "CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_WIDTH", // 56 /**< Maximum 2D surface width */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_HEIGHT] 				= "CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_HEIGHT", // 57 /**< Maximum 2D surface height */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE3D_WIDTH] 				= "CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE3D_WIDTH", // 58 /**< Maximum 3D surface width */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE3D_HEIGHT] 				= "CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE3D_HEIGHT", // 59 /**< Maximum 3D surface height */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE3D_DEPTH] 				= "CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE3D_DEPTH", // 60 /**< Maximum 3D surface depth */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE1D_LAYERED_WIDTH] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE1D_LAYERED_WIDTH", // 61 /**< Maximum 1D layered surface width */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE1D_LAYERED_LAYERS] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE1D_LAYERED_LAYERS", // 62 /**< Maximum layers in a 1D layered surface */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_LAYERED_WIDTH] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_LAYERED_WIDTH", // 63 /**< Maximum 2D layered surface width */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_LAYERED_HEIGHT] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_LAYERED_HEIGHT", // 64 /**< Maximum 2D layered surface height */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_LAYERED_LAYERS] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_LAYERED_LAYERS", // 65 /**< Maximum layers in a 2D layered surface */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACECUBEMAP_WIDTH] 			= "CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACECUBEMAP_WIDTH", // 66 /**< Maximum cubemap surface width */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACECUBEMAP_LAYERED_WIDTH] 	= "CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACECUBEMAP_LAYERED_WIDTH", // 67 /**< Maximum cubemap layered surface width */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACECUBEMAP_LAYERED_LAYERS] = "CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACECUBEMAP_LAYERED_LAYERS", // 68 /**< Maximum layers in a cubemap layered surface */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_LINEAR_WIDTH] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_LINEAR_WIDTH", // 69 /**< Maximum 1D linear texture width */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LINEAR_WIDTH] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LINEAR_WIDTH", // 70 /**< Maximum 2D linear texture width */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LINEAR_HEIGHT] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LINEAR_HEIGHT", // 71 /**< Maximum 2D linear texture height */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LINEAR_PITCH] 		= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LINEAR_PITCH", // 72 /**< Maximum 2D linear texture pitch in bytes */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_MIPMAPPED_WIDTH] 	= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_MIPMAPPED_WIDTH", // 73 /**< Maximum mipmapped 2D texture width */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_MIPMAPPED_HEIGHT] 	= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_MIPMAPPED_HEIGHT", // 74 /**< Maximum mipmapped 2D texture height */ 
    [CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR] 				= "CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR", // 75 /**< Major compute capability version number */ 
    [CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR] 				= "CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR", // 76 /**< Minor compute capability version number */ 
    [CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_MIPMAPPED_WIDTH] 	= "CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_MIPMAPPED_WIDTH", // 77 /**< Maximum mipmapped 1D texture width */ 
    [CU_DEVICE_ATTRIBUTE_STREAM_PRIORITIES_SUPPORTED] 			= "CU_DEVICE_ATTRIBUTE_STREAM_PRIORITIES_SUPPORTED", // 78 /**< Device supports stream priorities */ 
    [CU_DEVICE_ATTRIBUTE_GLOBAL_L1_CACHE_SUPPORTED] 			= "CU_DEVICE_ATTRIBUTE_GLOBAL_L1_CACHE_SUPPORTED", // 79 /**< Device supports caching globals in L1 */ 
    [CU_DEVICE_ATTRIBUTE_LOCAL_L1_CACHE_SUPPORTED] 				= "CU_DEVICE_ATTRIBUTE_LOCAL_L1_CACHE_SUPPORTED", // 80 /**< Device supports caching locals in L1 */ 
    [CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_MULTIPROCESSOR] 	= "CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_MULTIPROCESSOR", // 81 /**< Maximum shared memory available per multiprocessor in bytes */ 
    [CU_DEVICE_ATTRIBUTE_MAX_REGISTERS_PER_MULTIPROCESSOR] 		= "CU_DEVICE_ATTRIBUTE_MAX_REGISTERS_PER_MULTIPROCESSOR", // 82 /**< Maximum number of 32-bit registers available per multiprocessor */ 
    [CU_DEVICE_ATTRIBUTE_MANAGED_MEMORY] 						= "CU_DEVICE_ATTRIBUTE_MANAGED_MEMORY", // 83 /**< Device can allocate managed memory on this system */ 
    [CU_DEVICE_ATTRIBUTE_MULTI_GPU_BOARD] 						= "CU_DEVICE_ATTRIBUTE_MULTI_GPU_BOARD", // 84 /**< Device is on a multi-GPU board */ 
    [CU_DEVICE_ATTRIBUTE_MULTI_GPU_BOARD_GROUP_ID] 				= "CU_DEVICE_ATTRIBUTE_MULTI_GPU_BOARD_GROUP_ID", // 85 /**< Unique id for a group of devices on the same multi-GPU board */ 
    [CU_DEVICE_ATTRIBUTE_HOST_NATIVE_ATOMIC_SUPPORTED] 			= "CU_DEVICE_ATTRIBUTE_HOST_NATIVE_ATOMIC_SUPPORTED", // 86 /**< Link between the device and the host supports native atomic operations (this is a placeholder attribute, and is not supported on any current hardware)*/ 
    [CU_DEVICE_ATTRIBUTE_SINGLE_TO_DOUBLE_PRECISION_PERF_RATIO] = "CU_DEVICE_ATTRIBUTE_SINGLE_TO_DOUBLE_PRECISION_PERF_RATIO", // 87 /**< Ratio of single precision performance (in floating-point operations per second) to double precision performance */ 
    [CU_DEVICE_ATTRIBUTE_PAGEABLE_MEMORY_ACCESS] 				= "CU_DEVICE_ATTRIBUTE_PAGEABLE_MEMORY_ACCESS", // 88 /**< Device supports coherently accessing pageable memory without calling cudaHostRegister on it */ 
    [CU_DEVICE_ATTRIBUTE_CONCURRENT_MANAGED_ACCESS] 			= "CU_DEVICE_ATTRIBUTE_CONCURRENT_MANAGED_ACCESS", // 89 /**< Device can coherently access managed memory concurrently with the CPU */ 
    [CU_DEVICE_ATTRIBUTE_COMPUTE_PREEMPTION_SUPPORTED] 			= "CU_DEVICE_ATTRIBUTE_COMPUTE_PREEMPTION_SUPPORTED", // 90 /**< Device supports compute preemption. */ 
    [CU_DEVICE_ATTRIBUTE_CAN_USE_HOST_POINTER_FOR_REGISTERED_MEM] = "CU_DEVICE_ATTRIBUTE_CAN_USE_HOST_POINTER_FOR_REGISTERED_MEM", // 91 /**< Device can access host registered memory at the same virtual address as the CPU */ 
    [CU_DEVICE_ATTRIBUTE_CAN_USE_STREAM_MEM_OPS] 				= "CU_DEVICE_ATTRIBUTE_CAN_USE_STREAM_MEM_OPS", // 92 /**< ::cuStreamBatchMemOp and related APIs are supported. */ 
    [CU_DEVICE_ATTRIBUTE_CAN_USE_64_BIT_STREAM_MEM_OPS] 		= "CU_DEVICE_ATTRIBUTE_CAN_USE_64_BIT_STREAM_MEM_OPS", // 93 /**< 64-bit operations are supported in ::cuStreamBatchMemOp and related APIs. */ 
    [CU_DEVICE_ATTRIBUTE_CAN_USE_STREAM_WAIT_VALUE_NOR] 		= "CU_DEVICE_ATTRIBUTE_CAN_USE_STREAM_WAIT_VALUE_NOR", // 94 /**< ::CU_STREAM_WAIT_VALUE_NOR is supported. */ 
    [CU_DEVICE_ATTRIBUTE_COOPERATIVE_LAUNCH] 					= "CU_DEVICE_ATTRIBUTE_COOPERATIVE_LAUNCH", // 95 /**< Device supports launching cooperative kernels via ::cuLaunchCooperativeKernel */ 
    [CU_DEVICE_ATTRIBUTE_COOPERATIVE_MULTI_DEVICE_LAUNCH] 		= "CU_DEVICE_ATTRIBUTE_COOPERATIVE_MULTI_DEVICE_LAUNCH", // 96 /**< Device can participate in cooperative kernels launched via ::cuLaunchCooperativeKernelMultiDevice */ 
    [CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK_OPTIN] 	= "CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK_OPTIN", // 97 /**< Maximum optin shared memory per block */ 
    [CU_DEVICE_ATTRIBUTE_CAN_FLUSH_REMOTE_WRITES] 				= "CU_DEVICE_ATTRIBUTE_CAN_FLUSH_REMOTE_WRITES", // 98 /**< Both the ::CU_STREAM_WAIT_VALUE_FLUSH flag and the ::CU_STREAM_MEM_OP_FLUSH_REMOTE_WRITES MemOp are supported on the device. See \ref CUDA_MEMOP for additional details. */ 
    [CU_DEVICE_ATTRIBUTE_HOST_REGISTER_SUPPORTED] 				= "CU_DEVICE_ATTRIBUTE_HOST_REGISTER_SUPPORTED", // 99 /**< Device supports host memory registration via ::cudaHostRegister. */ 
    [CU_DEVICE_ATTRIBUTE_PAGEABLE_MEMORY_ACCESS_USES_HOST_PAGE_TABLES] = "CU_DEVICE_ATTRIBUTE_PAGEABLE_MEMORY_ACCESS_USES_HOST_PAGE_TABLES", // 100 /**< Device accesses pageable memory via the host's page tables. */ 
    [CU_DEVICE_ATTRIBUTE_DIRECT_MANAGED_MEM_ACCESS_FROM_HOST] 	= "CU_DEVICE_ATTRIBUTE_DIRECT_MANAGED_MEM_ACCESS_FROM_HOST", // 101 /**< The host can directly access managed memory on the device without migration. */ 
//    CU_DEVICE_ATTRIBUTE_MAX
};

static pthread_once_t s_once_key = PTHREAD_ONCE_INIT;
static pthread_key_t s_tls_key;		// thread local storage
static void init_thread_local_storage(void)
{
	int rc = pthread_key_create(&s_tls_key, free);
	assert(0 == rc);
	return; 
}

static cuda_error_mgr_t * cuda_error_mgr_get_default()
{
	debug_printf("%s()...", __FUNCTION__);
	pthread_once(&s_once_key, init_thread_local_storage);

	cuda_error_mgr_t * mgr = pthread_getspecific(s_tls_key);
	if(NULL == mgr)
	{
		mgr = cuda_error_mgr_init(NULL, NULL, NULL);
		int rc = pthread_setspecific(s_tls_key, mgr);
		assert(0 == rc);
	}
	return mgr;
}

static void cuda_error_handler(cuda_error_mgr_t * mgr, CUresult error)
{
	debug_printf("%s()...", __FUNCTION__);
	if(NULL == mgr) mgr = cuda_error_mgr_get_default();
	
	const char * err_name = NULL;
	const char * err_msg = NULL;
	CUresult ret = cuGetErrorName(error, &err_name);
	if(ret != CUDA_SUCCESS)
	{
		fprintf(stderr, "[CUDA ERROR]::%s()::unable to get error name. (CUDA_ERROR_INVALID_VALUE)\n", __FUNCTION__);
	}else
	{
		ret = cuGetErrorString(error, &err_msg);
		assert(ret == CUDA_SUCCESS);
		fprintf(stderr, "[ERROR]::%s::err=%s, msg=%s\n", mgr->prefix?mgr->prefix:"CUDA", err_name, err_msg);
	}
	
	cuda_context_t * cuda = mgr->user_data;
	if(cuda)
	{
		cuda_context_cleanup(cuda);
		mgr->user_data = NULL;
	}
	longjmp(mgr->jmp, (int)error);
}

cuda_error_mgr_t * cuda_error_mgr_init(cuda_error_mgr_t * mgr, cuda_error_handler_callback_ptr error_handler, void * user_data)
{
	debug_printf("%s()...", __FUNCTION__);
	if(NULL == mgr) mgr = calloc(1, sizeof(*mgr));
	assert(mgr);

	if(NULL == error_handler) error_handler = cuda_error_handler;
	
	mgr->user_data = user_data;
	if(NULL == mgr->prefix) mgr->prefix = "cuda-api::error_mgr";
	if(NULL == mgr->error_handler) mgr->error_handler = error_handler;

	return mgr;
}

cuda_error_mgr_t g_error_mgr[1] = {{
	.prefix = "cuda",
	.error_handler = cuda_error_handler,
}};

#ifdef _USE_UUID_LIB
#include <uuid/uuid.h>		// for uuid_unparse
#endif
void cuda_device_dump(cuda_device_t * dev)
{
#ifdef _DEBUG
	printf("====== %s() ======\n", __FUNCTION__);
	printf("DeviceIndex: %d\n", dev->index);
	printf("DeviceName: %s\n", dev->name);

#ifdef _USE_UUID_LIB
	char uuid[100] = "";
	uuid_unparse(*(uuid_t *)dev->uuid, uuid);
	printf("DeviceUUID: %s\n", uuid);
#endif

	printf("TotalMemory: %.3fM\n", (double)dev->total_memory/ (1 << 20));

#if defined(_VERBOSE) && (_VERBOSE > 0)
	printf("attributes: \n");
	for(int i = 1; i < CU_DEVICE_ATTRIBUTE_MAX; ++i)
	{
		if(dev->attributes[i] < 0) printf("\e[31m");
		printf("\t (%.3d) [%s]=%d", i, s_cu_device_attribute_name[i], dev->attributes[i]);
		if(dev->attributes[i] < 0) printf("\e[39m");
		printf("\n");
	}
#endif
    printf("compute_arch: %d\n", dev->arch);

#endif
	return;
}

int cuda_device_init(cuda_context_t * cuda, int index)
{
	int rc = 0;
	debug_printf("%s()...", __FUNCTION__);
	assert(cuda && index >= 0 && index < CUDA_CONTEXT_MAX_DEVICES);
	
	cuda_device_t * dev = &cuda->devices[index];
	dev->index = index;
	CUresult ret = cuDeviceGet(&dev->device, index);
	cuda_check_error(cuda, ret, cuDeviceGet);

	ret = cuDeviceGetName(dev->name, sizeof(dev->name) - 1, dev->device);
	cuda_check_error(cuda, ret, cuDeviceGetName);

	ret = cuDeviceGetUuid(dev->uuid, dev->device);
	cuda_check_error(cuda, ret, cuDeviceGetUuid);

	ret = cuDeviceTotalMem(&dev->total_memory, dev->device);
	cuda_check_error(cuda, ret, cuDeviceTotalMem);

	for(int i = 0; i < CU_DEVICE_ATTRIBUTE_MAX; ++i)
	{
		ret = cuDeviceGetAttribute(&dev->attributes[i], (CUdevice_attribute)i, dev->device);
		if(ret != CUDA_SUCCESS)
		{
			dev->attributes[i] = -1;
		}else
		{
			if(i == CU_DEVICE_ATTRIBUTE_COMPUTE_MODE)
			{
				dev->disabled = (dev->attributes[i] != CU_COMPUTEMODE_DEFAULT);
			}
		}
	}
	
	if(!dev->disabled)
	{
		// create one context per device ( do not used the default primary context)
		rc = cuda_device_new_context(cuda, dev, 0);
		assert(0 == rc);
		
		CUcontext old_ctx;
		ret = cuCtxPopCurrent(&old_ctx);
		cuda_check_error(cuda, ret, cuCtxPopCurrent);
		assert(dev->context == old_ctx);
	}
	
	int major = dev->attributes[CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR];
    int minor = dev->attributes[CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR];

    dev->arch = major * 10 + minor;

	cuda_device_dump(dev);
	return 0;
}

int cuda_device_new_context(cuda_context_t * cuda, cuda_device_t * dev, unsigned int flags)
{
	debug_printf("%s()...", __FUNCTION__);
	flags = CU_CTX_SCHED_BLOCKING_SYNC;
	// flags = CU_CTX_SCHED_SPIN | CU_CTX_MAP_HOST;
	CUresult ret = cuCtxCreate(&dev->context, flags, dev->device);
	cuda_check_error(cuda, ret, cuCtxCreate);
	
	return (int)ret;
}

#include <stdarg.h>
#define cuda_exec(cuda, func, ...) do {									\
		CUresult ret = func(__VA_ARGS__);								\
		if(ret != CUDA_SUCCESS) cuda_check_error(cuda, ret, func);		\
	}while(0)

void cuda_device_cleanup(cuda_context_t * cuda, cuda_device_t * dev)
{
	debug_printf("%s()...", __FUNCTION__);
	
	CUcontext current_ctx;
	CUresult ret;
	ret = cuCtxGetCurrent(&current_ctx);
	if(ret == CUDA_SUCCESS)
	{
	
		if(current_ctx && current_ctx == dev->context)
		{
			cuCtxSetCurrent(NULL);
		}
		
	}
	if(dev->context)
	{
		//~ CUresult ret = cuCtxDestroy(dev->context);
		//~ cuda_check_error(cuda, ret, cuCtxDestroy);
		cuda_exec(cuda, cuCtxDestroy, dev->context);
		dev->context = NULL;
	}
	
	return;
}




/*************************************************************************************
 ** @ingroup cuda_context
**************************************************************************************/
static pthread_once_t s_context_once_key = PTHREAD_ONCE_INIT;

static cuda_context_t s_context_default[1];

static void init_cuda_library(void)
{
	debug_printf("%s()...", __FUNCTION__);
	CUresult ret = cuInit(0);
	assert(ret == CUDA_SUCCESS);
	return;
}

cuda_context_t * cuda_context_get_default()
{
	return s_context_default;
}


cuda_context_t * cuda_context_init(cuda_context_t * cuda, void * user_data)
{
	debug_printf("%s()...", __FUNCTION__);
	pthread_once(&s_context_once_key, init_cuda_library);

	CUresult ret = CUDA_SUCCESS;
	if(NULL == cuda) cuda = calloc(1, sizeof(*cuda));
	assert(cuda);

	cuda->user_data = user_data;
	
	
	
	debug_printf("cuda_context=%p\n", cuda);
	if(NULL == cuda->error_mgr) cuda->error_mgr = cuda_error_mgr_init(NULL, cuda_error_handler, cuda);
	assert(cuda->error_mgr);

	int rc = setjmp(cuda->error_mgr->jmp);
	if(rc)
	{
		debug_printf("[ERROR]::%s()::longjmp=%d", __FUNCTION__, rc);
		
		exit(rc);
	}

	int devices_count = 0;
	
	ret = cuDeviceGetCount(&devices_count);
	cuda_check_error(cuda, ret, cuDeviceGetCount);

	log_printf("devices_count: %d\n", devices_count);
	assert(devices_count >= 0 && devices_count < CUDA_CONTEXT_MAX_DEVICES);

	cuda->devices_count = devices_count;
	for(int i = 0; i < devices_count; ++i)
	{
		int rc = cuda_device_init(cuda , i);
		assert(0 == rc);
	}
	return cuda;
}
void cuda_context_cleanup(cuda_context_t * cuda)
{
	debug_printf("%s()...", __FUNCTION__);

	for(int i = 0; i < cuda->devices_count; ++i)
	{
		cuda_device_t * dev = &cuda->devices[i];
		if(dev->device >= 0) cuda_device_cleanup(cuda, dev);
		
	}
	
	if(cuda->error_mgr)
	{
		free(cuda->error_mgr);
		cuda->error_mgr = NULL;
	}
	return;
}






/*************************************************************************************
 ** @ingroup cuda_function
**************************************************************************************/
typedef int (* file_filter_func)(const struct dirent *);
static int kernel_files_filter(const struct dirent * entry)
{
	char * p_ext = strrchr(entry->d_name, '.');
	return (p_ext && (strcmp(p_ext, ".ptx") == 0 || strcmp(p_ext, ".cubin") == 0));
}

static int cuda_function_compare(const void * a, const void * b)
{
	assert(a || b);
	if(NULL == a) return -1;
	if(NULL == b) return 1;
	return strcmp(((cuda_function_t *)a)->func_name, ((cuda_function_t *)b)->func_name);
}


//~ static int cuda_function_exec(cuda_function_t * func, size_t params_count, void * params[])
//~ {
	//~ assert(func);
	//~ int_dim3 grid = func->grid;
	//~ int_dim3 block = func->block;
	
	//~ CUresult ret = cuLaunchKernel(func->kernel, grid.x, grid.y, grid.z, block.x, block.y, block.z, 
		//~ func->shared_memory_size,
		//~ func->shared_mem,
		//~ params, 
		//~ func->extras);
	//~ assert(ret == CUDA_SUCCESS);
	//~ return 0;
//~ }

//~ static int cuda_function_execv(cuda_function_t * func, size_t params_count, ...)
//~ {
	//~ assert(params_count > 0);
	//~ void * params[params_count];
	//~ memset(params, 0, sizeof(params));
	//~ va_list args;
	//~ va_start(args, params_count);
	//~ size_t i = 0;
	//~ for(; i < params_count; ++i)
	//~ {
		//~ params[i] = va_arg(args, void *);
		//~ if(NULL == params[i]) break;
	//~ }
	//~ assert(i == params_count);
	//~ va_end(args);
	
	//~ cuda_function_exec(func, params_count, params);
	//~ return 0;
//~ }

static int cuda_function_execute(cuda_function_t * func)
{
	assert(func && func->kernel);
	int_dim3 grid = func->grid;
	int_dim3 block = func->block;
	
	CUresult ret = cuLaunchKernel(func->kernel, 
		grid.x, grid.y, grid.z, 
		block.x, block.y, block.z, 
		func->shared_memory_size, func->shared_mem,
		func->params, 
		func->extras);
	assert(ret == CUDA_SUCCESS);
	return 0;
}


static int cuda_function_set_blocks(cuda_function_t * func, size_t n, size_t block_size)
{
	int k = (n + block_size - 1) / block_size;
	int x = k;
	int y = 1;
	if(x > 65535) 
	{
		x = ceil(sqrt(k)); 
		y = (n - 1) / (x * block_size) + 1;
	}
	func->grid = (int_dim3){x, y, 1};
	func->block = (int_dim3){block_size, 1, 1};
	return 0;
}

//~ static int cuda_function_set_blocks(cuda_function_t * func, size_t elements_count, size_t block_size)
//~ {
//~ #define MAX_GRID_X 	0x7Fffffff
//~ #define MAX_GRID_Y 	0xFFFF
//~ #define MAX_GRID_Z 	0xFFFF
//~ #define MAX_BLOCK_X 1024
//~ #define MAX_BLOCK_Y 1024
//~ #define MAX_BLOCK_Z 64
	
	//~ if(block_size == 0) block_size = 256;
	//~ size_t blocks = (elements_count + block_size - 1) / block_size;
	
	//~ assert(block_size <= (MAX_BLOCK_X * MAX_BLOCK_Y * MAX_BLOCK_Z));

	//~ size_t x = blocks;
	//~ size_t y = 1;
	//~ size_t z = 1;
	//~ if(blocks > MAX_GRID_X)
	//~ {
		//~ x = MAX_GRID_X;
		//~ y = (blocks + MAX_GRID_X - 1) / MAX_GRID_X;
		//~ assert( y <= MAX_GRID_Y);
	//~ }
	//~ func->grid.x = (int)x; func->grid.y = (int)y; func->grid.z = (int)z;
	
	//~ x = block_size;
	//~ y = 1;
	//~ z = 1;
	//~ if(block_size > MAX_BLOCK_X)
	//~ {
		//~ x = MAX_BLOCK_X;
		//~ y = (block_size + MAX_BLOCK_X - 1) / MAX_BLOCK_X;
		
		//~ if( y > MAX_BLOCK_Z)
		//~ {
			//~ z = ( y + MAX_BLOCK_Y - 1) / MAX_BLOCK_Y;
			//~ y = MAX_BLOCK_Y;
		//~ }
		//~ assert(y <= MAX_BLOCK_Y && z <= MAX_BLOCK_Z);
	//~ }
	//~ func->block.x = (int)x; func->block.y = (int)y; func->block.z = (int)z;
	//~ return 0;
//~ }

static int cuda_function_set_params(cuda_function_t * func, size_t params_count, ...)
{
#define CUDA_FUNCTION_MAX_PARAMS (4096)
	
	void * params[CUDA_FUNCTION_MAX_PARAMS] = { NULL };
	void * param = NULL;
	
	size_t max_params = params_count?params_count:CUDA_FUNCTION_MAX_PARAMS;
	size_t i = 0;
	
	va_list args;
	va_start(args, params_count);
	while((param = va_arg(args, void *)) && i < max_params)
	{
		params[i++] = param;
	}
	va_end(args);
	
	if(params_count == 0) params_count = i;
	assert(params_count	== i);
	
	func->params = realloc(func->params, params_count * sizeof(void *));
	assert(func->params);
	
	for(size_t i = 0; i < params_count; ++i)
	{
		func->params[i] = params[i];
	}
	return 0;
}

cuda_function_t * cuda_function_init(cuda_function_t * func, const char * func_name, CUfunction kernel, size_t params_count, size_t extras_count)
{
	if(NULL == func)
	{
		func = calloc(1, sizeof(*func));
		assert(func);
	}
	/* init member functions */
	func->execute 		= cuda_function_execute;
	func->set_blocks 	= cuda_function_set_blocks;
	func->set_params 	= cuda_function_set_params;
	
	/* init data */
	if(func_name) 
	{
		strncpy(func->func_name, func_name, sizeof(func->func_name));
	}
	if(kernel) func->kernel = kernel;
	if(params_count) func->num_params = params_count;
	if(extras_count) func->extras_count = extras_count;
	return func;
}



/************************************************************************
 * cuda_module_private
************************************************************************/
static cuda_module_private_t * cuda_module_private_new(cuda_module_t * module, int arch)
{
	cuda_module_private_t * priv = calloc(1, sizeof(*priv));
	assert(priv);
	
	if(arch == 0) arch = 61;	// default: Qudra P5000 serials
	priv->arch = arch;
	
	cuda_link_option_t * option = priv->option;
	option->keys[option->count] = CU_JIT_TARGET;
	option->values[option->count++] = (void *)(long)arch;
	
	priv->module = module;
	module->priv = priv;
	
	module->link_begin = cuda_module_link_begin;
	module->link_final = cuda_module_link_final;
	module->add_kernel_data = cuda_module_add_kernel_data;
	module->add_kernel_file = cuda_module_add_kernel_file;
	return priv;
}

static void cuda_module_private_cleanup(cuda_module_private_t * priv)
{
	if(NULL == priv) return;
	if(priv->linker)
	{
		cuLinkDestroy(priv->linker);
		priv->linker = NULL;
	}
	free(priv);
}

//~ static int cuda_module_set_arch(cuda_module_t * module, int arch)
//~ {
	//~ module->arch = arch;
	//~ return 0;
//~ }

static inline CUjitInputType cuda_source_type_parse(enum cuda_source_type source_type)
{
	switch(source_type)
	{
	case cuda_source_type_object: return CU_JIT_INPUT_CUBIN;
	case cuda_source_type_source_code: break;
	case cuda_source_type_module:return CU_JIT_INPUT_LIBRARY;
	case cuda_source_type_assembly: return CU_JIT_INPUT_PTX;
	default: break;
	}
	return (CUjitInputType)-1;
}

int cuda_module_add_kernel_file(cuda_module_t * module, enum cuda_source_type source_type, const char * filename)
{
	cuda_module_private_t * priv = module->priv;
	if(source_type <= cuda_source_type_unknown || source_type > cuda_source_types_count)
	{
		source_type = cuda_source_type_unknown;
		const char * p_ext = strrchr(filename, '.');
		if(p_ext)
		{
			if(strcasecmp(p_ext, ".cu") == 0) 		source_type = cuda_source_type_source_code;
			else if(strcasecmp(p_ext, ".ptx") == 0) source_type = cuda_source_type_assembly;
			else if(strcasecmp(p_ext, ".cubin") == 0) 	source_type = cuda_source_type_object;
			else if(strcasecmp(p_ext, ".so") == 0) 	source_type = cuda_source_type_module;
		}
	}
	
	char output_file[PATH_MAX] = "";
	
	if(source_type == cuda_source_type_source_code || source_type == cuda_source_type_assembly)
	{
		if(source_type == cuda_source_type_source_code)
		{
			// compile to assembly code
			int arch = module->arch;
			if(0 == arch) arch = 61;
			char compile_command[4096] = "";
			
			char * tmp = strdup(filename);
			assert(tmp);
			char * base_name = basename(tmp);
			snprintf(output_file, sizeof(output_file), "/tmp/%s.tmp.ptx", base_name);
			free(tmp);
		
			
			snprintf(compile_command, sizeof(compile_command), 
				"/usr/local/cuda/bin/nvcc --ptx -arch=sm_%d -o %s %s", 
				arch, output_file, filename);
			int rc = system(compile_command);
			if(rc)
			{
				return -1;
			}
			source_type = cuda_source_type_assembly;
			filename = output_file;
		}
		
		assert(cuda_source_type_assembly == source_type);
		
		unsigned char * source_data = NULL;
		ssize_t cb_data = 0;
		
		FILE * fp = fopen(filename, "r");
		if(NULL == fp) {
			fprintf(stderr, "source file not found: %s\n", filename);
			if(output_file[0]) remove(output_file);
			return -1;
		}
		
		fseek(fp, 0, SEEK_END);
		ssize_t file_size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		assert(file_size > 0);
		
		source_data = malloc(file_size + 1);
		cb_data = fread(source_data, 1, file_size, fp);
		if(cb_data > 0) source_data[cb_data] = '\0';
		fclose(fp);
		if(output_file[0]) remove(output_file);
		
		int rc = module->add_kernel_data(module, source_type, source_data, cb_data);
		free(source_data);
		return rc;
	}
	
	int type = cuda_source_type_parse(source_type);
	if(type == -1)
	{
		fprintf(stderr, "[ERROR]::%s()::unknown source type: %d\n", __FUNCTION__, (int)source_type);
		return -1;
	}
	
	CUresult ret = cuLinkAddFile_v2(priv->linker, type, filename, 0, NULL, NULL);
	if(ret == CUDA_SUCCESS) return 0;

	cuda_print_error(ret);
	return -1;
}

static enum cuda_function_description_parse_stage cuda_function_description_parse_partial(cuda_function_description_t * desc, const char * line);
int cuda_module_add_kernel_data(cuda_module_t * module, 
	enum cuda_source_type source_type, 
	const unsigned char * kernel_data,
	size_t length)
{
	int type = cuda_source_type_parse(source_type);
	if(type == -1)
	{
		fprintf(stderr, "[ERROR]::%s()::unknown source type: %d\n", __FUNCTION__, (int)source_type);
		return -1;
	}
	
	cuda_module_private_t * priv = module->priv;
	CUresult ret = cuLinkAddData_v2(priv->linker, type, 
		(void *)kernel_data, length, 
		NULL, 
		0, NULL, NULL);
	priv->err_code = ret;
	
	if(ret) 
	{
		cuda_print_error(ret);
		return -1;
	}
	
	// auto add functions
	if(type == CU_JIT_INPUT_PTX)
	{
		enum cuda_function_description_parse_stage stage = function_description_parse_stage_unknown;
		cuda_function_description_t desc[1];
		memset(desc, 0, sizeof(desc));
		
		char * line = (char *)kernel_data;
		char * p_end = line + length;

		while(line && line < p_end)
		{
			char * p_next_line = strchr(line, '\n');
			if(p_next_line) *p_next_line++ = '\0';
			
			stage = cuda_function_description_parse_partial(desc, line);
			if(stage == function_description_parse_stage_final)
			{
				module->add_function(module, desc);
				memset(desc, 0, sizeof(desc));
			}
			line = p_next_line;
		}
		
	}
	
	
	
	if(0 == ret) return 0;
	
	cuda_print_error(ret);
	return -1;
}


static int cuda_module_link_begin(struct cuda_module * module, int arch)
{
	cuda_module_private_t * priv = module->priv;
	assert(priv && priv->module == module);
	
	if(priv->linker)
	{
		cuLinkDestroy(priv->linker);
		priv->linker = NULL;
	}
	
	CUresult ret = cuLinkCreate(priv->option->count, priv->option->keys, 
		priv->option->values, &priv->linker);
	
	priv->err_code = ret;	
	if(ret == CUDA_SUCCESS) return 0;
	
	cuda_print_error(ret);
	return -1;
}

static int cuda_module_link_final(struct cuda_module * module)
{
	cuda_module_private_t * priv = module->priv;
	
	CUresult ret = cuLinkComplete(priv->linker, &priv->cubin_data, &priv->cubin_length);
	priv->err_code = ret;
	if(ret == CUDA_SUCCESS) {
		ret = module->load(module, priv->cubin_data, priv->cubin_length);
		if(CUDA_SUCCESS == ret) return 0;
	}
	cuda_print_error(ret);
	return -1;
}

/*************************************************************************************
 ** @ingroup cuda_module
**************************************************************************************/
static int cuda_module_link(struct cuda_module * module, const char * kernels_path, int gpu_arch);
static int cuda_module_load(struct cuda_module * module, void * cubin_data, size_t cubin_size);
static int cuda_module_add_function(struct cuda_module * module, const cuda_function_description_t * desc);
static cuda_function_t * cuda_module_get_function(struct cuda_module * module, const char * func_name);

cuda_module_t * cuda_module_init(cuda_module_t * module, cuda_context_t * cuda, int device_index, void * user_data)
{
	debug_printf("%s(ctx=%p, gpu_index=%d)...", __FUNCTION__, cuda, device_index);
	assert(cuda && device_index >= 0 &&  device_index < cuda->devices_count);
	if(NULL == module) module = calloc(1, sizeof(*module));
	assert(module);
	module->cuda = cuda;
	module->user_data = user_data;
	module->dev = &cuda->devices[device_index];

	cuda_device_t * dev = module->dev;
	if(NULL == dev->context) {
		CUresult ret = cuCtxGetCurrent(&dev->context);
		
		if(ret != CUDA_SUCCESS || NULL == dev->context)
		{
			int rc = cuda_device_new_context(cuda, dev, 0);
			assert(0 == rc);
		}
	}
	module->link = cuda_module_link;
	module->load = cuda_module_load;
	module->add_function = cuda_module_add_function;
	module->get_function = cuda_module_get_function;

	
	cuda_module_private_t * priv = cuda_module_private_new(module, dev->arch?dev->arch:61);	// default: sm_61
	assert(priv && priv->module == module);
	
	module->link_begin = cuda_module_link_begin;
	module->link_final = cuda_module_link_final;
	module->add_kernel_data = cuda_module_add_kernel_data;
	module->add_kernel_file = cuda_module_add_kernel_file;

	return module;
}


void cuda_function_cleanup(cuda_function_t * func)
{
	debug_printf("%s(%p)::name=%s", __FUNCTION__, func, func?func->func_name:"(null)");
	if(func)
	{
		if(func->params) free(func->params);
		func->params = NULL;
		
		if(func->extras) free(func->extras);
		func->extras = NULL;
	}
}

void cuda_module_cleanup(cuda_module_t *module)
{
	if(NULL == module) return;
	if(module->functions_table)
	{
		tdestroy(module->functions_table, (void (*)(void *))cuda_function_cleanup);
		module->functions_table = NULL;
	}
	module->functions_count = 0;
	
	cuda_module_private_cleanup(module->priv);
	module->priv = NULL;
	return;
}

static int cuda_module_link(struct cuda_module * module, const char * kernels_path, int gpu_arch)
{
	debug_printf("%s()...", __FUNCTION__);
	CUcontext context;
	CUresult ret;
	cuda_context_t * cuda = module->cuda;
	cuda_device_t * dev = module->dev;
	assert(cuda && dev);
	int rc = 0;
	
	assert(!dev->disabled);
	
	context = dev->context;
	assert(context);
	
	if(NULL == context)
	{
		printf("create new context...\n");
		rc = cuda_device_new_context(cuda, dev, 0);	// attach new context to current thread
		assert(0 == rc);
		context = dev->context;
		printf("\t --> context: %p\n", context);
	}
	assert(context == dev->context);		// must be called by the same thread
	
	//~ ret = cuCtxGetCurrent(&context);
	//~ cuda_check_error(cuda, ret, cuCtxGetCurrent);
	cuCtxSetCurrent(context);

	struct dirent ** filelist = NULL;
	ssize_t files_count = scandir(kernels_path, &filelist, kernel_files_filter, versionsort);
	assert(files_count > 0);
	
	char cur_path[PATH_MAX] = "";
	char * old_path = getcwd(cur_path, sizeof(cur_path));
	assert(old_path);
	
	rc = chdir(kernels_path);
	assert(0 == rc);
	
	static CUjit_option default_options[] = {
		CU_JIT_TARGET,
		CU_JIT_TARGET_FROM_CUCONTEXT,
	};

	static void * default_option_values[] = {
		(void *)CU_TARGET_COMPUTE_61,
		(void *)1
	};
	
	if(gpu_arch <= 0) gpu_arch = (int)(long)default_option_values[0];

	void * option_values[2] = 
	{
		[0] = (void *)(long)gpu_arch,
		[1] = default_option_values,
	};
	
	CUlinkState linker;
	ret = cuLinkCreate(sizeof(default_options) / sizeof(default_options[0]), 
		default_options, option_values, 
		&linker);
	cuda_check_error(cuda, ret, cuLinkCreate);
	
	for(ssize_t i = 0; i < files_count; ++i)
	{
		const char * filename = filelist[i]->d_name;
		CUjitInputType type = CU_JIT_INPUT_CUBIN;
		const char * p_ext = strrchr(filename, '.');
		assert(p_ext);
		if(strcmp(p_ext, ".ptx") == 0) type = CU_JIT_INPUT_PTX;
		printf("link %s\n", filename);
		ret = cuLinkAddFile(linker, type, filename, 0, NULL, NULL);
		printf("\t  -> ret = %d\n", (int)ret);
		cuda_check_error(cuda, ret, cuLinkAddFile);
		
		free(filelist[i]);
	}
	free(filelist);
	
	rc = chdir(old_path);
	assert(0 == rc);
	
	void * cubin_data = NULL;
	size_t cubin_size = 0;
	ret = cuLinkComplete(linker, &cubin_data, &cubin_size);
	cuda_check_error(cuda, ret, cuLinkComplete);
	
	assert(cubin_size > 0);
	
	rc = cuda_module_load(module, cubin_data, cubin_size);
	assert(0 == rc);
	
	ret = cuLinkDestroy(linker);
	cuda_check_error(cuda, ret, cuLinkDestroy);
	
	cuCtxSetCurrent(NULL);
	return 0;
}
static int cuda_module_load(struct cuda_module * module, void * cubin_data, size_t cubin_size)
{
	CUresult ret = cuModuleLoadData(&module->ctx, cubin_data);
	assert(ret == CUDA_SUCCESS);
	return 0;
}

static int cuda_module_add_function(struct cuda_module * module, const cuda_function_description_t * desc)
{
	assert(module && module->dev);
//	assert(module->ctx);
	assert(desc && desc->func_name[0]);
	
	int count = module->functions_count;
	assert(count < CUDA_BASELINE_MAX_FUNCTIONS);

	cuda_function_t * func = &module->functions[count];
	memset(func, 0, sizeof(*func));
	memcpy(func->desc, desc, sizeof(*desc));
	
	if(module->ctx)	// confirm func_name if module has been loaded 
	{
		CUresult ret;
		ret = cuModuleGetFunction(&func->kernel, module->ctx , desc->func_name);
		if(ret != CUDA_SUCCESS)
		{
			cuda_print_error(ret);
			return -1;
		}
	}

	cuda_function_t ** p_node = tsearch(func, &module->functions_table, cuda_function_compare);
	assert(p_node);
	
	if(*p_node != func)
	{
	//	cuda_function_cleanup(func);
		func = *p_node;
		memcpy(func->desc, desc, sizeof(*desc));
		fprintf(stderr, "[INFO]::%s()::kernel symbol found! update settings of kernel '%s'\n", 
			__FUNCTION__,
			desc->func_name);
	}else
	{
		func = cuda_function_init(func, NULL, NULL, 0, 0);
		++module->functions_count;
	}
	
	fprintf(stderr, "\t\t --> add func: %s, params_count=%d\n", func->desc->func_name, (int)func->desc->num_params);

	return 0;
}
static cuda_function_t * cuda_module_get_function(struct cuda_module * module, const char * func_name)
{
	if(NULL == func_name) return NULL;
	cuda_function_t pattern[1] = {{
		.num_params = 0,
	}};
	
	strncpy(pattern->func_name, func_name, sizeof(pattern->func_name));
	cuda_function_t ** p_node = tfind(pattern, &module->functions_table, cuda_function_compare);
	if(p_node) {
		cuda_function_t * func = *p_node;
		assert(func);
		
		assert(module->ctx);
		if(NULL == func->kernel)
		{
			CUresult ret;
			ret = cuModuleGetFunction(&func->kernel, module->ctx , func_name);
			if(ret != CUDA_SUCCESS)
			{
				cuda_print_error(ret);
				return NULL;
			}
		}
		return *p_node;
	}
	return NULL;
}



//~ static const char * s_cuda_error_desc[CUDA_ERROR_UNKNOWN + 1] = {
	//~ [CUDA_ERROR_INVALID_VALUE] = "CUDA_ERROR_INVALID_VALUE: "
		//~ "* This indicates that one or more of the parameters passed to the API call"
		//~ "* is not within an acceptable range of values.",
	//~ [CUDA_ERROR_DEINITIALIZED] = "CUDA_ERROR_DEINITIALIZED",
	//~ [CUDA_ERROR_NOT_INITIALIZED] = "CUDA_ERROR_NOT_INITIALIZED",
	//~ [CUDA_ERROR_INVALID_CONTEXT] = "CUDA_ERROR_INVALID_CONTEXT",
	//~ [CUDA_ERROR_OUT_OF_MEMORY] = "CUDA_ERROR_OUT_OF_MEMORY",
//~ }; 

const char * cuda_strerror(int ret) 
{
//	return ((ret >= 0 && ret <= CUDA_ERROR_UNKNOWN)?s_cuda_error_desc[ret]:"unknown error");
	cuda_error_mgr_t * err_mgr = cuda_error_mgr_get_default();
	
	err_mgr->err_name = NULL;
	err_mgr->err_desc = NULL;
	(void) cuGetErrorName(ret, &err_mgr->err_name);
	(void) cuGetErrorString(ret, &err_mgr->err_desc);
	snprintf(err_mgr->err_msg, sizeof(err_mgr->err_msg), "[%s]::%s: %s", err_mgr->prefix, err_mgr->err_name, err_mgr->err_desc);
	return err_mgr->err_msg;
}

// attach gpu context to current cpu thread
int cuda_device_attach(cuda_context_t * cuda, cuda_device_t * dev)
{
	CUresult ret;
	assert(dev->context);
	ret = cuCtxSetCurrent(dev->context);
	
	if(ret == CUDA_SUCCESS) return 0;
	
	cuda_check_error(cuda, ret, cuCtxSetCurrent);		// force exit
	
	fprintf(stderr, "[ERROR]::%s()::ret=%d, err=%s\n", 
		__FUNCTION__, 
		(int)ret,
		cuda_strerror(ret));
	return -1;
}

// detach gpu context of current cpu thread
int cuda_device_detach(cuda_context_t * cuda, cuda_device_t * dev)	
{
	CUresult ret;
	//~ if(dev)
	//~ {
		//~ assert(dev->context);
		
	//~ }
	ret = cuCtxSetCurrent(NULL);
	if(ret == CUDA_SUCCESS) return 0;
	fprintf(stderr, "[ERROR]::%s()::ret=%d, err=%s\n", 
		__FUNCTION__, 
		(int)ret,
		cuda_strerror(ret));
	return -1;
}


/**************************************************************************
 * cuda_function_description
**************************************************************************/
#define MAX_FUNCTION_DSECRIPTION_PARSE_LINE_SIZE (4096)
static enum cuda_function_description_parse_stage cuda_function_description_parse_partial(cuda_function_description_t * desc, const char * line)
{
	#define FUNC_PARAMS_BEGIN	'('
	#define FUNC_PARAMS_END		')'
	
	assert(line);
	int cb = strlen(line);
	if(cb <= 0) return -1;
	assert(cb < MAX_FUNCTION_DSECRIPTION_PARSE_LINE_SIZE);
	
	char buf[MAX_FUNCTION_DSECRIPTION_PARSE_LINE_SIZE] = "";
	strncpy(buf, line, sizeof(buf));
	
	// skip comments
	char * p_comments = strstr(buf, "//");
	if(p_comments) *p_comments = '\0';
	
	if(desc->stage == function_description_parse_stage_final)	// clear old data
	{
		memset(desc, 0, sizeof(*desc));
		desc->stage = function_description_parse_stage_unknown;
	}
	
	// parse function decl.
	if(desc->stage <= function_description_parse_stage_unknown)
	{
		if(!strstr(buf, ".visible")) 
		{
			return function_description_parse_stage_invalid;
		}
		
		char * p_end = strrchr(buf, FUNC_PARAMS_BEGIN);
		if(NULL == p_end)
		{
			return function_description_parse_stage_invalid;
		}
		*p_end = '\0';
		
		char * func_name = strstr(buf, ".entry ");
		if(NULL == func_name) return function_description_parse_stage_invalid;
		
		func_name += sizeof(".entry ") - 1;
		trim(func_name, p_end);
		
		int cb = strlen(func_name);
		if(cb <= 0) return function_description_parse_stage_invalid;
		assert(cb < sizeof(desc->func_name));
		strncpy(desc->func_name, func_name, sizeof(desc->func_name));
		desc->stage = function_description_parse_stage_begin;
		return desc->stage;
	}
	
	desc->stage = function_description_parse_stage_params;
	char * p_final = strchr(buf, FUNC_PARAMS_END);
	if(p_final)
	{
		assert(desc->num_params > 0);
		desc->stage = function_description_parse_stage_final;
	}else
	{
		char * param = strstr(buf, ".param");
		if(param) ++desc->num_params;
	}
	return desc->stage;
}
#undef MAX_FUNCTION_DSECRIPTION_PARSE_LINE_SIZE


void cuda_function_description_dump(const cuda_function_description_t * desc)
{
	printf("\tindex: %d\n", desc->index);
	printf("\tfunc_name: %s\n", desc->func_name);
	printf("\tparams_count: %d\n", desc->num_params);
	return;
}


/************************************************************************
 * utils
************************************************************************/
int cuda_module_save_module(cuda_module_t * module, const char * module_file, const char * def_file)
{
	cuda_module_private_t * priv = module->priv;
	if(NULL == module_file && NULL == def_file) return -1;
	if(NULL == priv->cubin_data || priv->cubin_length <= 0) return -1;
	
	if(module_file)
	{
		FILE * fp = fopen(module_file, "wb+");
		if(NULL == fp)
		{
			perror("cuda_module_save(BIN_DATA)");
			return -1;
		}
		fwrite(priv->cubin_data, 1, priv->cubin_length, fp);
		fclose(fp);
	}
	
	if(def_file)
	{
		FILE * fp = fopen(def_file, "w+");
		if(NULL == fp)
		{
			perror("cuda_module_save(DEF_FILE)");
			return -1;
		}
		
		for(int i = 0; i < module->functions_count; ++i)
		{
			cuda_function_description_t * desc = (cuda_function_description_t *)&module->functions[i];
			fprintf(fp, "%s@%d\n", desc->func_name, (int)desc->num_params);
		}
		fclose(fp);
	}
	return 0;
}
int cuda_module_load_module(cuda_module_t * module, const char * module_file, const char * def_file)
{
	assert(module && module->priv);
	assert(module_file && def_file);
	cuda_module_private_t * priv = module->priv;
	
	if(priv->linker)
	{
		cuLinkDestroy(priv->linker);
		priv->linker = NULL;
		
	}else
	{
		if(priv->cubin_data) free(priv->cubin_data);
	}
	priv->cubin_data = NULL;
	priv->cubin_length = 0;
	
	assert(module_file);
	
	int rc = 0;
	unsigned char * cubin_data = NULL;
	ssize_t cubin_length = 0;
	
	cubin_length = load_binary_data(module_file, &cubin_data);
	assert(cubin_data && cubin_length > 0);
	
	rc = module->load(module, cubin_data, cubin_length);
	assert(0 == rc);
	
	priv->cubin_data 	= cubin_data;
	priv->cubin_length 	= cubin_length;
	
	FILE * fp = fopen(def_file, "r");
	assert(fp);
	
	char buf[4096] = "";
	char * line = NULL;
	int index = 0;
	while((line = fgets(buf, sizeof(buf) - 1, fp)))
	{
		char * p_comments = strchr(line, '#');
		if(p_comments) *p_comments = '\0';
		int cb = strlen(line);
		if(0 == cb) continue;		// skip empty or comment lines
		
		char * p_end = line + cb;
		line = trim(line, p_end);
		if(NULL == line && ! line[0]) continue; // skip blank lines
		
		char * p_num_params = strchr(line, '@');
		assert(p_num_params);
		*p_num_params++ = '\0';
		
		cuda_function_description_t desc[1];
		memset(desc, 0, sizeof(desc));
		
		char * func_name = trim(line, NULL);
		int num_params = atoi(p_num_params);
		assert(num_params > 0);
		
		strncpy(desc->func_name, func_name, sizeof(desc->func_name));
		desc->num_params = num_params;
		desc->index = index++;
		rc = module->add_function(module, desc);
		assert(0 == rc);
	}	
	fclose(fp);
	
	assert(index == module->functions_count);
	return 0;
}



/************************************************************************
 * TEST Module
************************************************************************/


#if defined(_TEST_CUDA_API) && defined(_STAND_ALONE)
void test_gpu(cuda_context_t * cuda);
int main(int argc, char **argv)
{
	cuda_context_t * cuda = cuda_context_init(NULL, NULL);
	assert(cuda);
	
	test_gpu(cuda);

	cuda_context_cleanup(cuda);
	return 0;
}

#include "matsum-kernel.h"
static void vector_sum(cuda_context_t * cuda, cuda_device_t * dev, int n, const int *  restrict a, const int * restrict b, int * restrict c)
{
	static CUmodule module;
	static CUfunction function;
	static const char * module_file = "matsum.ptx";
	if(NULL == module)
	{
		cuda_exec(cuda, cuModuleLoad, &module, module_file);
	}
	
	if(NULL == function)
	{
		cuda_exec(cuda, cuModuleGetFunction, &function, module, "mat_sum");
	}

	CUdeviceptr mem_a, mem_b, mem_c;
	cuda_exec(cuda, cuMemAlloc, &mem_a, sizeof(int)* n);
	cuda_exec(cuda, cuMemAlloc, &mem_b, sizeof(int)* n);
	cuda_exec(cuda, cuMemAlloc, &mem_c, sizeof(int)* n);

	cuda_exec(cuda, cuMemcpyHtoD, mem_a, a, sizeof(int)* n);
	cuda_exec(cuda, cuMemcpyHtoD, mem_b, b, sizeof(int)* n);
	
	void * args[] = {
		&mem_a,
		&mem_b,
		&mem_c,
		NULL
	};

	cuda_exec(cuda, cuLaunchKernel, function, n, 1, 1,  	1, 1, 1,	0, 0, 	args, 0);
	cuda_exec(cuda, cuMemcpyDtoH, c, mem_c, sizeof(int)* n);

	for(int i = 0; i < n; ++i)
	{
		printf("%d + %d = %d\n", a[i], b[i], c[i]);
		assert(c[i] == (a[i] + b[i]));
	}

	debug_printf("%s()...", __FUNCTION__);
	return;
}

void test_gpu(cuda_context_t * cuda)
{
	int index = 0;
	cuda_device_t * dev = &cuda->devices[index];
	cuda_device_new_context(cuda, dev, 0);

	int a[N], b[N], c[N];
	for(int i = 0; i < N; ++i)
	{
		a[i] = N - i;
		b[i] = i * i;
	}
	vector_sum(cuda, dev, N, a, b, c);

	return;
}
#endif

