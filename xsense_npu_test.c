/*
 * Copyright 2021 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You not obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>  // 添加目录操作头文件
#include <sys/stat.h> // 添加文件状态头文件

//#include "rtsp_demo.h"
#include "sample_comm.h"

// 全局信号量，用于同步IVA处理完成事件
static sem_t g_iva_semaphore;
// 控制是否启用睡眠模式的标志位
static RK_S32 g_enable_sleep = 1;
// IVA上下文结构体，包含IVA相关配置和状态
static SAMPLE_IVA_CTX_S iva_ctx;
// 输入图像文件路径
static char *path = NULL;
// 循环处理次数，-1表示未设置
static RK_S32 loop_count = -1;

// 退出标志和退出结果码
static bool quit = false;
static int quit_result = RK_SUCCESS;

// 目录模式下的YUV文件列表和相关信息
static char **yuv_files = NULL;
static int yuv_file_count = 1;
static int current_file_index = 0;
static RK_CHAR *pIvaModelPath = "/tmp/";
static RK_CHAR *pIvaModelName = "iva_object_detection_v3_pfp_nn_640x384.data";

// 结果输出文件指针和路径
static FILE *result_output_file = NULL;
static char *result_output_path = NULL;

static void sigterm_handler(int sig) {
	fprintf(stderr, "signal %d\n", sig);
	quit = true;
	quit_result = RK_SUCCESS;
}

static void program_handle_error(const char *func, RK_U32 line) {
	printf("func: <%s> line: <%d> error exit!", func, line);
	quit = true;
	quit_result = RK_FAILURE;
}

static void program_normal_exit(const char *func, RK_U32 line) {
	printf("func: <%s> line: <%d> normal exit!", func, line);
	quit = true;
	quit_result = RK_SUCCESS;
}

// 修改命令行参数，移除loop_count选项
static RK_CHAR optstr[] = "?:w:h:p:d:r:e:s:o:t:n:l:";
static const struct option long_options[] = {
    {"path", required_argument, NULL, 'p'},
    {"directory", required_argument, NULL, 'd'},
    {"width", required_argument, NULL, 'w'},
    {"height", required_argument, NULL, 'h'},
    {"framerate", optional_argument, NULL, 'r'},
    {"model_path", required_argument, NULL, 'l'},
    {"model_name", required_argument, NULL, 'n'},
    {"detectrate", optional_argument, NULL, 't'},
    // 添加结果输出文件选项
    {"result_output", required_argument, NULL, 'o'},
    {"help", optional_argument, NULL, '?'},
    {NULL, 0, NULL, 0},
};

/******************************************************************************
 * function : show usage
 ******************************************************************************/
static void print_usage(const RK_CHAR *name) {
	printf("usage example:\n");
	printf("\t%s -w 720 -h 480 -p /mnt/sdcard/test_image.yuv -l /tmp/ -n iva_object_detection_v3_pfp_nn_640x384.data -r 10 -o result.txt\n", name);
	printf("\t%s -w 720 -h 480 -d /mnt/sdcard/yuv_images/ -l /tmp/ -n iva_object_detection_v3_pfp_nn_640x384.data -r 10 -o result.txt\n", name);
	printf("\t-w | --width: input image with, Default 720\n");
	printf("\t-h | --height: input image height, Default 480\n");
	printf("\t-t | --detectrate:  Default 10\n");
	printf("\t-p | --path: input image path, Default NULL\n");
	printf("\t-l | --model_path: model path, Default /tmp/\n");
	printf("\t-n | --model_name: model name, Default iva_object_detection_v3_pfp_nn_640x384.data\n");
	printf("\t-d | --directory: input images directory, Default NULL\n");
	printf("\t-r | --framerate: iva detect framerate, Default 10\n");
	// 添加结果输出选项说明
	printf("\t-o | --result_output: output result file path, Default NULL\n");
}

// 添加函数：检查文件扩展名是否为YUV
static int is_yuv_file(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) return 0;
    return (strcmp(dot, ".yuv") == 0 || strcmp(dot, ".YUV") == 0);
}

// 扫描指定目录下的所有YUV文件
static int scan_yuv_directory(const char *dir_path) {
    DIR *dir;
    struct dirent *entry;
    struct stat statbuf;
    char full_path[512];
    
    // 打开目录
    dir = opendir(dir_path);
    if (!dir) {
        RK_LOGE("Cannot open directory: %s", dir_path);
        return -1;
    }
    
    // 先计算YUV文件数量
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        if (stat(full_path, &statbuf) == 0 && S_ISREG(statbuf.st_mode)) {
            if (is_yuv_file(entry->d_name)) {
                count++;
            }
        }
    }
    
    closedir(dir);
    
    if (count == 0) {
        RK_LOGE("No YUV files found in directory: %s", dir_path);
        return -1;
    }
    
    // 分配内存存储文件路径
    yuv_files = (char **)malloc(count * sizeof(char *));
    if (!yuv_files) {
        RK_LOGE("Failed to allocate memory for file list");
        return -1;
    }
    
    // 重新扫描并存储文件路径
    dir = opendir(dir_path);
    if (!dir) {
        free(yuv_files);
        yuv_files = NULL;
        return -1;
    }
    

    int index = 0;
    while ((entry = readdir(dir)) != NULL && index < count) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        if (stat(full_path, &statbuf) == 0 && S_ISREG(statbuf.st_mode)) {
            if (is_yuv_file(entry->d_name)) {
                yuv_files[index] = strdup(full_path);
                if (!yuv_files[index]) {
                    // 清理已分配的内存
                    for (int i = 0; i < index; i++) {
                        free(yuv_files[i]);
                    }
                    free(yuv_files);
                    yuv_files = NULL;
                    closedir(dir);
                    return -1;
                }
                index++;
            }
        }
    }
    
    closedir(dir);
    yuv_file_count = count;
    RK_LOGI("Found %d YUV files in directory %s", yuv_file_count, dir_path);
    return 0;
}

// 添加函数：释放文件列表内存
static void free_yuv_files() {
    if (yuv_files) {
        for (int i = 0; i < yuv_file_count; i++) {
            if (yuv_files[i]) {
                free(yuv_files[i]);
            }
        }
        free(yuv_files);
        yuv_files = NULL;
        yuv_file_count = 0;
    }
}

// IVA检测结果回调函数，当IVA完成一帧图像处理后调用此函数
static void rkIvaEvent_callback(const RockIvaDetectResult *result,
                                const RockIvaExecuteStatus status, void *userData) {

	RK_LOGI("objnum %d, status %d", result->objNum, status);

	// 如果设置了结果输出文件，则将结果写入文件
	if (result_output_file) {
		char *current_file = NULL;
		if (yuv_files && yuv_file_count > 0) {
			// 目录模式
			int prev_index = (current_file_index == 0) ? (yuv_file_count - 1) : (current_file_index - 1);
			current_file = yuv_files[prev_index];
		} else if (path) {
			// 单文件模式
			current_file = path;
		}
		
		if (current_file) {
			fprintf(result_output_file, "File: %s\n", current_file);
			if (result->objNum > 0) {
				fprintf(result_output_file, "Object count: %d, detected\n", result->objNum);
			} else {
				fprintf(result_output_file, "Object count: %d  not detect\n", result->objNum);
			}
			for (int i = 0; i < result->objNum; i++) {
				// 添加类型字符串映射
				const char* type_str[] = {
					"NONE",     // ROCKIVA_OBJECT_TYPE_NONE = 0
					"PERSON",   // ROCKIVA_OBJECT_TYPE_PERSON = 1
					"VEHICLE",  // ROCKIVA_OBJECT_TYPE_VEHICLE = 2
					"NON_VEHICLE", // ROCKIVA_OBJECT_TYPE_NON_VEHICLE = 3
					"FACE",     // ROCKIVA_OBJECT_TYPE_FACE = 4
					"HEAD",     // ROCKIVA_OBJECT_TYPE_HEAD = 5
					"PET",      // ROCKIVA_OBJECT_TYPE_PET = 6
					"MOTORCYCLE", // ROCKIVA_OBJECT_TYPE_MOTORCYCLE = 7
					"BICYCLE",  // ROCKIVA_OBJECT_TYPE_BICYCLE = 8
					"PLATE",    // ROCKIVA_OBJECT_TYPE_PLATE = 9
					"BABY",     // ROCKIVA_OBJECT_TYPE_BABY = 10
					"PACKAGE"   // ROCKIVA_OBJECT_TYPE_PACKAGE = 11
				};
				const char* type_name = (result->objInfo[i].type < ROCKIVA_OBJECT_TYPE_MAX) ? 
					type_str[result->objInfo[i].type] : "UNKNOWN";
				RK_LOGI("Object %d: topLeft:[%d,%d], bottomRight:[%d,%d],"
				        "objId: %d, frameId: %d, score: %d, type: %d(%s)\n",
				        i,
				        result->objInfo[i].rect.topLeft.x, result->objInfo[i].rect.topLeft.y,
				        result->objInfo[i].rect.bottomRight.x,
				        result->objInfo[i].rect.bottomRight.y, result->objInfo[i].objId,
				        result->objInfo[i].frameId, result->objInfo[i].score,
				        result->objInfo[i].type, type_name);

				fprintf(result_output_file, "Object %d: topLeft:[%d,%d], bottomRight:[%d,%d],"
				        "objId: %d, frameId: %d, score: %d, type: %d(%s)\n",
				        i,
				        result->objInfo[i].rect.topLeft.x, result->objInfo[i].rect.topLeft.y,
				        result->objInfo[i].rect.bottomRight.x,
				        result->objInfo[i].rect.bottomRight.y, result->objInfo[i].objId,
				        result->objInfo[i].frameId, result->objInfo[i].score,
				        result->objInfo[i].type, type_name);
			}
			fprintf(result_output_file, "\n");
			fflush(result_output_file);
		}
	}
}

// IVA帧释放回调函数，当IVA处理完一帧图像后调用此函数通知可以继续处理下一帧
static void rkIvaFrame_releaseCallBack(const RockIvaReleaseFrames *releaseFrames,
                                       void *userdata) {
	/* 当IVA处理完视频帧时，此函数会被调用 */
	RK_LOGD("release iva frame success!");
	sem_post(&g_iva_semaphore); // 发送信号量，通知可以继续处理下一帧
}

// 向IVA发送帧数据的线程函数
static void *send_frame_to_iva_thread(void *pArgs) {
	prctl(PR_SET_NAME, "send_frame_to_iva_thread");
	RK_S32 s32Ret = RK_FAILURE;
	RK_S32 input_file_fd = -1;
	RockIvaImage input_image;
	RK_U32 size = iva_ctx.u32ImageWidth * iva_ctx.u32ImageHeight * 3 / 2;
	RK_S32 i = 0;
	RK_S32 pool_id;
	MB_POOL_CONFIG_S pool_cfg;
	MB_BLK blk;
	void *input_image_vaddr;
	RK_S32 input_image_fd;
	struct timespec iva_start_time, iva_end_time;
	long delay_time = (1000 / iva_ctx.u32IvaDetectFrameRate);
	long cost_time = 0;

	// 初始化内存池配置
	memset(&pool_cfg, 0, sizeof(MB_POOL_CONFIG_S));
	pool_cfg.u64MBSize = size;
	pool_cfg.u32MBCnt = 1;
	pool_cfg.enAllocType = MB_ALLOC_TYPE_DMA;
	pool_cfg.bPreAlloc = RK_FALSE;
	
	// 创建内存池
	pool_id = RK_MPI_MB_CreatePool(&pool_cfg);
	if (pool_id == MB_INVALID_POOLID) {
		RK_LOGE("create mb pool failed");
		program_handle_error(__func__, __LINE__);
		return NULL;
	}
	
	// 获取内存块
	blk = RK_MPI_MB_GetMB(pool_id, size, RK_TRUE);
	if (blk == MB_INVALID_HANDLE) {
		RK_LOGE("get mb block failed");
		program_handle_error(__func__, __LINE__);
		return NULL;
	}
	
	// 获取虚拟地址和文件描述符
	input_image_vaddr = RK_MPI_MB_Handle2VirAddr(blk);
	input_image_fd = RK_MPI_MB_Handle2Fd(blk);

	// 初始化信号量
	sem_init(&g_iva_semaphore, 0, 0);
	RK_LOGI(" while outside loop count %d\n", loop_count);	

	// 主循环：向IVA发送图像帧进行处理
	while (!quit && (loop_count < 0 || i < loop_count)) { // 修改循环条件
		RK_LOGI("loop count %d", i++);
		clock_gettime(CLOCK_MONOTONIC, &iva_start_time);
		
		// 根据模式选择文件路径
		char *current_file_path = NULL;
		if (yuv_files && yuv_file_count > 0) {
		    // 目录模式：循环使用目录中的文件
		    current_file_path = yuv_files[current_file_index];
		    current_file_index = (current_file_index + 1) % yuv_file_count;
		    RK_LOGI("Processing file: %s", current_file_path);
		} else if (path) {
		    // 单文件模式
		    current_file_path = path;
		}
		
		// 打开并读取当前图像文件
		if (current_file_path) {
		    if (input_file_fd >= 0) {
		        close(input_file_fd);
		        input_file_fd = -1;
		    }
		    input_file_fd = open(current_file_path, O_RDONLY);
		}
		
		// 读取图像数据到内存
		if (input_file_fd < 0) {
			RK_LOGE("open %s failed because %s, use empty image as input", 
			        current_file_path ? current_file_path : "null", strerror(errno));
			memset(input_image_vaddr, 0, size);
			RK_MPI_SYS_MmzFlushCache(blk, RK_FALSE);
		} else {
			s32Ret = read(input_file_fd, input_image_vaddr, size);
			RK_LOGI("input image size %d from %s", s32Ret, current_file_path);
			RK_MPI_SYS_MmzFlushCache(blk, RK_FALSE);
		}
		
		// 发送图像帧到IVA进行处理
		input_image.info.transformMode = iva_ctx.eImageTransform;
		input_image.info.width = iva_ctx.u32ImageWidth;
		input_image.info.height = iva_ctx.u32ImageHeight;
		input_image.info.format = iva_ctx.eImageFormat;
		input_image.frameId = i;
		input_image.dataAddr = NULL;
		input_image.dataPhyAddr = NULL;
		input_image.dataFd = input_image_fd;
		s32Ret = ROCKIVA_PushFrame(iva_ctx.ivahandle, &input_image, NULL);
		if (s32Ret < 0) {
			RK_LOGE("ROCKIVA_PushFrame failed %#X", s32Ret);
			program_handle_error(__func__, __LINE__);
			break;
		}
		
		// 等待IVA处理完成
		sem_wait(&g_iva_semaphore);

		// 计算处理时间和延迟
		clock_gettime(CLOCK_MONOTONIC, &iva_end_time);
		cost_time = (iva_end_time.tv_sec * 1000L + iva_end_time.tv_nsec / 1000000L) -
		            (iva_start_time.tv_sec * 1000L + iva_start_time.tv_nsec / 1000000L);
		RK_LOGI("iva cost time %ld ms, delay for %ld ms", cost_time,
		        delay_time > cost_time ? (delay_time - cost_time) : 0);
		if (delay_time > cost_time)
			usleep((delay_time - cost_time) * 1000);
	}

	// 清理资源
	sem_destroy(&g_iva_semaphore);
	if (input_file_fd >= 0)
		close(input_file_fd);
	RK_MPI_MB_ReleaseMB(blk);
	RK_MPI_MB_DestroyPool(pool_id);
	program_normal_exit(__func__, __LINE__);
	RK_LOGI("send_frame_to_iva_thread exit !!!");
	return RK_NULL;
}

/******************************************************************************
 * function    : main()
 * Description : main
 ******************************************************************************/
int main(int argc, char *argv[]) {
	// 默认配置参数
	RK_U32 u32IvaWidth = 640;
	RK_U32 u32IvaHeight = 360;
	RK_U32 u32IvaDetectFrameRate = 10;

	RK_S32 s32Ret;
	RK_S32 s32SuspendTime = 1000;
	pthread_t iva_thread_id;
	char *directory_path = NULL;  // 添加目录路径变量

	// 检查参数数量
	if (argc < 2) {
		print_usage(argv[0]);
		return 0;
	}

	// 注册信号处理函数
	signal(SIGINT, sigterm_handler);

	printf("%s initial start\n", __func__);
	int c;
	// 解析命令行参数
	while ((c = getopt_long(argc, argv, optstr, long_options, NULL)) != -1) {
		const char *tmp_optarg = optarg;
		switch (c) {
		case 'w':
			u32IvaWidth = atoi(optarg);
			break;
		case 't':
			u32IvaDetectFrameRate = atoi(optarg);
			break;
		case 'h':
			u32IvaHeight = atoi(optarg);
			break;
		case 'r':
			u32IvaDetectFrameRate = atoi(optarg);
			break;
		case 'p':
			path = optarg;
			// 如果是单文件模式且未设置循环次数，则默认为1次
			if (loop_count == -1) {
				loop_count = 1;
			}
			break;
		case 'd':
			directory_path = optarg;
			break;
		case 'l':
			pIvaModelPath = optarg;
			break;
		case 'n':
			pIvaModelName = optarg;
			break;
		// 添加结果输出文件路径处理
		case 'o':
			result_output_path = optarg;
			break;
		case '?':
		default:
			print_usage(argv[0]);
			return 0;
		}
	}
	
	// 处理目录模式
	if (directory_path) {
	    if (scan_yuv_directory(directory_path) < 0) {
	        RK_LOGE("Failed to scan directory: %s", directory_path);
	        return RK_FAILURE;
	    }
	    // 设置循环次数为文件数量
	    if (loop_count == -1) {
	        loop_count = yuv_file_count;
	    }
	} else if (loop_count == -1) {
	    // 默认情况下至少运行一次
	    loop_count = 1;
	}
	
	// 如果指定了结果输出文件路径，则打开文件
	if (result_output_path) {
		result_output_file = fopen(result_output_path, "w");
		if (!result_output_file) {
			RK_LOGE("Failed to open result output file: %s", result_output_path);
			// 清理资源
			free_yuv_files();
			return RK_FAILURE;
		}
		RK_LOGI("Result output file opened: %s", result_output_path);
	}

	// 初始化系统
	RK_MPI_SYS_Init();

	/* 初始化IVA */
	iva_ctx.pModelDataPath = pIvaModelPath;
	iva_ctx.commonParams.detModelName = pIvaModelName;
	iva_ctx.u32ImageWidth = u32IvaWidth;
	iva_ctx.u32ImageHeight = u32IvaHeight;
	iva_ctx.u32DetectStartX = 0;
	iva_ctx.u32DetectStartY = 0;
	iva_ctx.u32DetectWidth = u32IvaWidth;
	iva_ctx.u32DetectHight = u32IvaHeight;
	iva_ctx.eImageTransform = ROCKIVA_IMAGE_TRANSFORM_NONE;
	iva_ctx.eImageFormat = ROCKIVA_IMAGE_FORMAT_YUV420SP_NV12;
	iva_ctx.eModeType = ROCKIVA_DET_MODEL_PFP;
	iva_ctx.u32IvaDetectFrameRate = u32IvaDetectFrameRate;
	iva_ctx.detectResultCallback = rkIvaEvent_callback;
	iva_ctx.releaseCallback = rkIvaFrame_releaseCallBack;
	iva_ctx.eIvaMode = ROCKIVA_MODE_DETECT;
	s32Ret = SAMPLE_COMM_IVA_Create(&iva_ctx);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("SAMPLE_COMM_IVA_Create failure:%#X", s32Ret);
		// 清理资源
		free_yuv_files();
		return RK_FAILURE;
	}
	
	// 创建发送帧到IVA的线程
	pthread_create(&iva_thread_id, 0, send_frame_to_iva_thread, NULL);

	printf("%s initial finish\n", __func__);
	// 主线程等待退出信号
	while (!quit) {
		sleep(1);
	}

	printf("%s exit!\n", __func__);
	/* 销毁IVA */
	pthread_join(iva_thread_id, RK_NULL);
	SAMPLE_COMM_IVA_Destroy(&iva_ctx);

	// 退出系统
	RK_MPI_SYS_Exit();
	
	// 清理资源
	free_yuv_files();
	
	// 关闭结果输出文件
	if (result_output_file) {
		fclose(result_output_file);
		result_output_file = NULL;
	}

	return quit_result;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */