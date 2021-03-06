/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef TU_PRIVATE_H
#define TU_PRIVATE_H

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_VALGRIND
#include <memcheck.h>
#include <valgrind.h>
#define VG(x) x
#else
#define VG(x) ((void)0)
#endif

#include "c11/threads.h"
#include "main/macros.h"
#include "util/list.h"
#include "util/macros.h"
#include "util/u_atomic.h"
#include "vk_alloc.h"
#include "vk_object.h"
#include "vk_debug_report.h"
#include "wsi_common.h"

#include "drm-uapi/msm_drm.h"
#include "ir3/ir3_compiler.h"
#include "ir3/ir3_shader.h"

#include "adreno_common.xml.h"
#include "adreno_pm4.xml.h"
#include "a6xx.xml.h"
#include "fdl/freedreno_layout.h"

#include "tu_descriptor_set.h"
#include "tu_extensions.h"
#include "tu_util.h"

/* Pre-declarations needed for WSI entrypoints */
struct wl_surface;
struct wl_display;
typedef struct xcb_connection_t xcb_connection_t;
typedef uint32_t xcb_visualid_t;
typedef uint32_t xcb_window_t;

#include <vulkan/vk_android_native_buffer.h>
#include <vulkan/vk_icd.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_intel.h>

#include "tu_entrypoints.h"

#include "vk_format.h"

#define MAX_VBS 32
#define MAX_VERTEX_ATTRIBS 32
#define MAX_RTS 8
#define MAX_VSC_PIPES 32
#define MAX_VIEWPORTS 1
#define MAX_SCISSORS 16
#define MAX_DISCARD_RECTANGLES 4
#define MAX_PUSH_CONSTANTS_SIZE 128
#define MAX_PUSH_DESCRIPTORS 32
#define MAX_DYNAMIC_UNIFORM_BUFFERS 16
#define MAX_DYNAMIC_STORAGE_BUFFERS 8
#define MAX_DYNAMIC_BUFFERS                                                  \
   (MAX_DYNAMIC_UNIFORM_BUFFERS + MAX_DYNAMIC_STORAGE_BUFFERS)
#define TU_MAX_DRM_DEVICES 8
#define MAX_VIEWS 8
#define MAX_BIND_POINTS 2 /* compute + graphics */
/* The Qualcomm driver exposes 0x20000058 */
#define MAX_STORAGE_BUFFER_RANGE 0x20000000
/* We use ldc for uniform buffer loads, just like the Qualcomm driver, so
 * expose the same maximum range.
 * TODO: The SIZE bitfield is 15 bits, and in 4-dword units, so the actual
 * range might be higher.
 */
#define MAX_UNIFORM_BUFFER_RANGE 0x10000

#define A6XX_TEX_CONST_DWORDS 16
#define A6XX_TEX_SAMP_DWORDS 4

#define tu_printflike(a, b) __attribute__((__format__(__printf__, a, b)))

static inline uint32_t
tu_minify(uint32_t n, uint32_t levels)
{
   if (unlikely(n == 0))
      return 0;
   else
      return MAX2(n >> levels, 1);
}

#define for_each_bit(b, dword)                                               \
   for (uint32_t __dword = (dword);                                          \
        (b) = __builtin_ffs(__dword) - 1, __dword; __dword &= ~(1 << (b)))

#define typed_memcpy(dest, src, count)                                       \
   ({                                                                        \
      STATIC_ASSERT(sizeof(*src) == sizeof(*dest));                          \
      memcpy((dest), (src), (count) * sizeof(*(src)));                       \
   })

#define COND(bool, val) ((bool) ? (val) : 0)
#define BIT(bit) (1u << (bit))

/* Whenever we generate an error, pass it through this function. Useful for
 * debugging, where we can break on it. Only call at error site, not when
 * propagating errors. Might be useful to plug in a stack trace here.
 */

struct tu_instance;

VkResult
__vk_errorf(struct tu_instance *instance,
            VkResult error,
            const char *file,
            int line,
            const char *format,
            ...);

#define vk_error(instance, error)                                            \
   __vk_errorf(instance, error, __FILE__, __LINE__, NULL);
#define vk_errorf(instance, error, format, ...)                              \
   __vk_errorf(instance, error, __FILE__, __LINE__, format, ##__VA_ARGS__);

void
__tu_finishme(const char *file, int line, const char *format, ...)
   tu_printflike(3, 4);
void
tu_loge(const char *format, ...) tu_printflike(1, 2);
void
tu_logi(const char *format, ...) tu_printflike(1, 2);

/**
 * Print a FINISHME message, including its source location.
 */
#define tu_finishme(format, ...)                                             \
   do {                                                                      \
      static bool reported = false;                                          \
      if (!reported) {                                                       \
         __tu_finishme(__FILE__, __LINE__, format, ##__VA_ARGS__);           \
         reported = true;                                                    \
      }                                                                      \
   } while (0)

#define tu_stub()                                                            \
   do {                                                                      \
      tu_finishme("stub %s", __func__);                                      \
   } while (0)

void *
tu_lookup_entrypoint_unchecked(const char *name);
void *
tu_lookup_entrypoint_checked(
   const char *name,
   uint32_t core_version,
   const struct tu_instance_extension_table *instance,
   const struct tu_device_extension_table *device);

struct tu_physical_device
{
   struct vk_object_base base;

   struct tu_instance *instance;

   char path[20];
   char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
   uint8_t driver_uuid[VK_UUID_SIZE];
   uint8_t device_uuid[VK_UUID_SIZE];
   uint8_t cache_uuid[VK_UUID_SIZE];

   struct wsi_device wsi_device;

   int local_fd;
   int master_fd;

   unsigned gpu_id;
   uint32_t gmem_size;
   uint64_t gmem_base;
   uint32_t ccu_offset_gmem;
   uint32_t ccu_offset_bypass;
   /* alignment for size of tiles */
   uint32_t tile_align_w;
#define TILE_ALIGN_H 16
   /* gmem store/load granularity */
#define GMEM_ALIGN_W 16
#define GMEM_ALIGN_H 4

   struct {
      uint32_t PC_UNKNOWN_9805;
      uint32_t SP_UNKNOWN_A0F8;
   } magic;

   int msm_major_version;
   int msm_minor_version;

   /* This is the drivers on-disk cache used as a fallback as opposed to
    * the pipeline cache defined by apps.
    */
   struct disk_cache *disk_cache;

   struct tu_device_extension_table supported_extensions;
};

enum tu_debug_flags
{
   TU_DEBUG_STARTUP = 1 << 0,
   TU_DEBUG_NIR = 1 << 1,
   TU_DEBUG_IR3 = 1 << 2,
   TU_DEBUG_NOBIN = 1 << 3,
   TU_DEBUG_SYSMEM = 1 << 4,
   TU_DEBUG_FORCEBIN = 1 << 5,
   TU_DEBUG_NOUBWC = 1 << 6,
};

struct tu_instance
{
   struct vk_object_base base;

   VkAllocationCallbacks alloc;

   uint32_t api_version;
   int physical_device_count;
   struct tu_physical_device physical_devices[TU_MAX_DRM_DEVICES];

   enum tu_debug_flags debug_flags;

   struct vk_debug_report_instance debug_report_callbacks;

   struct tu_instance_extension_table enabled_extensions;
};

VkResult
tu_wsi_init(struct tu_physical_device *physical_device);
void
tu_wsi_finish(struct tu_physical_device *physical_device);

bool
tu_instance_extension_supported(const char *name);
uint32_t
tu_physical_device_api_version(struct tu_physical_device *dev);
bool
tu_physical_device_extension_supported(struct tu_physical_device *dev,
                                       const char *name);

struct cache_entry;

struct tu_pipeline_cache
{
   struct vk_object_base base;

   struct tu_device *device;
   pthread_mutex_t mutex;

   uint32_t total_size;
   uint32_t table_size;
   uint32_t kernel_count;
   struct cache_entry **hash_table;
   bool modified;

   VkAllocationCallbacks alloc;
};

struct tu_pipeline_key
{
};


/* queue types */
#define TU_QUEUE_GENERAL 0

#define TU_MAX_QUEUE_FAMILIES 1

struct tu_fence
{
   struct vk_object_base base;
   struct wsi_fence *fence_wsi;
   bool signaled;
   int fd;
};

void
tu_fence_init(struct tu_fence *fence, bool signaled);
void
tu_fence_finish(struct tu_fence *fence);
void
tu_fence_update_fd(struct tu_fence *fence, int fd);
void
tu_fence_copy(struct tu_fence *fence, const struct tu_fence *src);
void
tu_fence_signal(struct tu_fence *fence);
void
tu_fence_wait_idle(struct tu_fence *fence);

struct tu_queue
{
   struct vk_object_base base;

   struct tu_device *device;
   uint32_t queue_family_index;
   int queue_idx;
   VkDeviceQueueCreateFlags flags;

   uint32_t msm_queue_id;
   struct tu_fence submit_fence;
};

struct tu_bo
{
   uint32_t gem_handle;
   uint64_t size;
   uint64_t iova;
   void *map;
};

enum global_shader {
   GLOBAL_SH_VS,
   GLOBAL_SH_FS_BLIT,
   GLOBAL_SH_FS_CLEAR0,
   GLOBAL_SH_FS_CLEAR_MAX = GLOBAL_SH_FS_CLEAR0 + MAX_RTS,
   GLOBAL_SH_COUNT,
};

/* This struct defines the layout of the global_bo */
struct tu6_global
{
   /* 6 bcolor_entry entries, one for each VK_BORDER_COLOR */
   uint8_t border_color[128 * 6];

   /* clear/blit shaders, all <= 16 instrs (16 instr = 1 instrlen unit) */
   instr_t shaders[GLOBAL_SH_COUNT][16];

   uint32_t seqno_dummy;          /* dummy seqno for CP_EVENT_WRITE */
   uint32_t _pad0;
   volatile uint32_t vsc_draw_overflow;
   uint32_t _pad1;
   volatile uint32_t vsc_prim_overflow;
   uint32_t _pad2[3];

   /* scratch space for VPC_SO[i].FLUSH_BASE_LO/HI, start on 32 byte boundary. */
   struct {
      uint32_t offset;
      uint32_t pad[7];
   } flush_base[4];
};
#define gb_offset(member) offsetof(struct tu6_global, member)
#define global_iova(cmd, member) ((cmd)->device->global_bo.iova + gb_offset(member))

void tu_init_clear_blit_shaders(struct tu6_global *global);

/* extra space in vsc draw/prim streams */
#define VSC_PAD 0x40

struct tu_device
{
   struct vk_device vk;
   struct tu_instance *instance;

   struct tu_queue *queues[TU_MAX_QUEUE_FAMILIES];
   int queue_count[TU_MAX_QUEUE_FAMILIES];

   struct tu_physical_device *physical_device;
   int _lost;

   struct ir3_compiler *compiler;

   /* Backup in-memory cache to be used if the app doesn't provide one */
   struct tu_pipeline_cache *mem_cache;

#define MIN_SCRATCH_BO_SIZE_LOG2 12 /* A page */

   /* Currently the kernel driver uses a 32-bit GPU address space, but it
    * should be impossible to go beyond 48 bits.
    */
   struct {
      struct tu_bo bo;
      mtx_t construct_mtx;
      bool initialized;
   } scratch_bos[48 - MIN_SCRATCH_BO_SIZE_LOG2];

   struct tu_bo global_bo;

   struct tu_device_extension_table enabled_extensions;

   uint32_t vsc_draw_strm_pitch;
   uint32_t vsc_prim_strm_pitch;
   mtx_t vsc_pitch_mtx;
};

VkResult _tu_device_set_lost(struct tu_device *device,
                             const char *file, int line,
                             const char *msg, ...) PRINTFLIKE(4, 5);
#define tu_device_set_lost(dev, ...) \
   _tu_device_set_lost(dev, __FILE__, __LINE__, __VA_ARGS__)

static inline bool
tu_device_is_lost(struct tu_device *device)
{
   return unlikely(p_atomic_read(&device->_lost));
}

VkResult
tu_bo_init_new(struct tu_device *dev, struct tu_bo *bo, uint64_t size);
VkResult
tu_bo_init_dmabuf(struct tu_device *dev,
                  struct tu_bo *bo,
                  uint64_t size,
                  int fd);
int
tu_bo_export_dmabuf(struct tu_device *dev, struct tu_bo *bo);
void
tu_bo_finish(struct tu_device *dev, struct tu_bo *bo);
VkResult
tu_bo_map(struct tu_device *dev, struct tu_bo *bo);

/* Get a scratch bo for use inside a command buffer. This will always return
 * the same bo given the same size or similar sizes, so only one scratch bo
 * can be used at the same time. It's meant for short-lived things where we
 * need to write to some piece of memory, read from it, and then immediately
 * discard it.
 */
VkResult
tu_get_scratch_bo(struct tu_device *dev, uint64_t size, struct tu_bo **bo);

struct tu_cs_entry
{
   /* No ownership */
   const struct tu_bo *bo;

   uint32_t size;
   uint32_t offset;
};

struct tu_cs_memory {
   uint32_t *map;
   uint64_t iova;
};

struct tu_draw_state {
   uint64_t iova : 48;
   uint32_t size : 16;
};

enum tu_dynamic_state
{
   /* re-use VK_DYNAMIC_STATE_ enums for non-extended dynamic states */
   TU_DYNAMIC_STATE_SAMPLE_LOCATIONS = VK_DYNAMIC_STATE_STENCIL_REFERENCE + 1,
   TU_DYNAMIC_STATE_COUNT,
};

enum tu_draw_state_group_id
{
   TU_DRAW_STATE_PROGRAM,
   TU_DRAW_STATE_PROGRAM_BINNING,
   TU_DRAW_STATE_TESS,
   TU_DRAW_STATE_VB,
   TU_DRAW_STATE_VI,
   TU_DRAW_STATE_VI_BINNING,
   TU_DRAW_STATE_RAST,
   TU_DRAW_STATE_DS,
   TU_DRAW_STATE_BLEND,
   TU_DRAW_STATE_VS_CONST,
   TU_DRAW_STATE_HS_CONST,
   TU_DRAW_STATE_DS_CONST,
   TU_DRAW_STATE_GS_CONST,
   TU_DRAW_STATE_FS_CONST,
   TU_DRAW_STATE_DESC_SETS,
   TU_DRAW_STATE_DESC_SETS_LOAD,
   TU_DRAW_STATE_VS_PARAMS,
   TU_DRAW_STATE_INPUT_ATTACHMENTS_GMEM,
   TU_DRAW_STATE_INPUT_ATTACHMENTS_SYSMEM,

   /* dynamic state related draw states */
   TU_DRAW_STATE_DYNAMIC,
   TU_DRAW_STATE_COUNT = TU_DRAW_STATE_DYNAMIC + TU_DYNAMIC_STATE_COUNT,
};

enum tu_cs_mode
{

   /*
    * A command stream in TU_CS_MODE_GROW mode grows automatically whenever it
    * is full.  tu_cs_begin must be called before command packet emission and
    * tu_cs_end must be called after.
    *
    * This mode may create multiple entries internally.  The entries must be
    * submitted together.
    */
   TU_CS_MODE_GROW,

   /*
    * A command stream in TU_CS_MODE_EXTERNAL mode wraps an external,
    * fixed-size buffer.  tu_cs_begin and tu_cs_end are optional and have no
    * effect on it.
    *
    * This mode does not create any entry or any BO.
    */
   TU_CS_MODE_EXTERNAL,

   /*
    * A command stream in TU_CS_MODE_SUB_STREAM mode does not support direct
    * command packet emission.  tu_cs_begin_sub_stream must be called to get a
    * sub-stream to emit comamnd packets to.  When done with the sub-stream,
    * tu_cs_end_sub_stream must be called.
    *
    * This mode does not create any entry internally.
    */
   TU_CS_MODE_SUB_STREAM,
};

struct tu_cs
{
   uint32_t *start;
   uint32_t *cur;
   uint32_t *reserved_end;
   uint32_t *end;

   struct tu_device *device;
   enum tu_cs_mode mode;
   uint32_t next_bo_size;

   struct tu_cs_entry *entries;
   uint32_t entry_count;
   uint32_t entry_capacity;

   struct tu_bo **bos;
   uint32_t bo_count;
   uint32_t bo_capacity;

   /* state for cond_exec_start/cond_exec_end */
   uint32_t cond_flags;
   uint32_t *cond_dwords;
};

struct tu_device_memory
{
   struct vk_object_base base;

   struct tu_bo bo;
   VkDeviceSize size;

   /* for dedicated allocations */
   struct tu_image *image;
   struct tu_buffer *buffer;

   uint32_t type_index;
   void *map;
   void *user_ptr;
};

struct tu_descriptor_range
{
   uint64_t va;
   uint32_t size;
};

struct tu_descriptor_set
{
   struct vk_object_base base;

   const struct tu_descriptor_set_layout *layout;
   struct tu_descriptor_pool *pool;
   uint32_t size;

   uint64_t va;
   uint32_t *mapped_ptr;

   uint32_t *dynamic_descriptors;

   struct tu_bo *buffers[0];
};

struct tu_push_descriptor_set
{
   struct tu_descriptor_set set;
   uint32_t capacity;
};

struct tu_descriptor_pool_entry
{
   uint32_t offset;
   uint32_t size;
   struct tu_descriptor_set *set;
};

struct tu_descriptor_pool
{
   struct vk_object_base base;

   struct tu_bo bo;
   uint64_t current_offset;
   uint64_t size;

   uint8_t *host_memory_base;
   uint8_t *host_memory_ptr;
   uint8_t *host_memory_end;

   uint32_t entry_count;
   uint32_t max_entry_count;
   struct tu_descriptor_pool_entry entries[0];
};

struct tu_descriptor_update_template_entry
{
   VkDescriptorType descriptor_type;

   /* The number of descriptors to update */
   uint32_t descriptor_count;

   /* Into mapped_ptr or dynamic_descriptors, in units of the respective array
    */
   uint32_t dst_offset;

   /* In dwords. Not valid/used for dynamic descriptors */
   uint32_t dst_stride;

   uint32_t buffer_offset;

   /* Only valid for combined image samplers and samplers */
   uint16_t has_sampler;

   /* In bytes */
   size_t src_offset;
   size_t src_stride;

   /* For push descriptors */
   const uint32_t *immutable_samplers;
};

struct tu_descriptor_update_template
{
   struct vk_object_base base;

   uint32_t entry_count;
   struct tu_descriptor_update_template_entry entry[0];
};

struct tu_buffer
{
   struct vk_object_base base;

   VkDeviceSize size;

   VkBufferUsageFlags usage;
   VkBufferCreateFlags flags;

   struct tu_bo *bo;
   VkDeviceSize bo_offset;
};

static inline uint64_t
tu_buffer_iova(struct tu_buffer *buffer)
{
   return buffer->bo->iova + buffer->bo_offset;
}

struct tu_vertex_binding
{
   struct tu_buffer *buffer;
   VkDeviceSize offset;
};

const char *
tu_get_debug_option_name(int id);

const char *
tu_get_perftest_option_name(int id);

struct tu_descriptor_state
{
   struct tu_descriptor_set *sets[MAX_SETS];
   uint32_t dynamic_descriptors[MAX_DYNAMIC_BUFFERS * A6XX_TEX_CONST_DWORDS];
};

enum tu_cmd_dirty_bits
{
   TU_CMD_DIRTY_VERTEX_BUFFERS = 1 << 2,
   TU_CMD_DIRTY_DESC_SETS_LOAD = 1 << 3,
   TU_CMD_DIRTY_COMPUTE_DESC_SETS_LOAD = 1 << 4,
   TU_CMD_DIRTY_SHADER_CONSTS = 1 << 5,
   /* all draw states were disabled and need to be re-enabled: */
   TU_CMD_DIRTY_DRAW_STATE = 1 << 7,
};

/* There are only three cache domains we have to care about: the CCU, or
 * color cache unit, which is used for color and depth/stencil attachments
 * and copy/blit destinations, and is split conceptually into color and depth,
 * and the universal cache or UCHE which is used for pretty much everything
 * else, except for the CP (uncached) and host. We need to flush whenever data
 * crosses these boundaries.
 */

enum tu_cmd_access_mask {
   TU_ACCESS_UCHE_READ = 1 << 0,
   TU_ACCESS_UCHE_WRITE = 1 << 1,
   TU_ACCESS_CCU_COLOR_READ = 1 << 2,
   TU_ACCESS_CCU_COLOR_WRITE = 1 << 3,
   TU_ACCESS_CCU_DEPTH_READ = 1 << 4,
   TU_ACCESS_CCU_DEPTH_WRITE = 1 << 5,

   /* Experiments have shown that while it's safe to avoid flushing the CCU
    * after each blit/renderpass, it's not safe to assume that subsequent
    * lookups with a different attachment state will hit unflushed cache
    * entries. That is, the CCU needs to be flushed and possibly invalidated
    * when accessing memory with a different attachment state. Writing to an
    * attachment under the following conditions after clearing using the
    * normal 2d engine path is known to have issues:
    *
    * - It isn't the 0'th layer.
    * - There are more than one attachment, and this isn't the 0'th attachment
    *   (this seems to also depend on the cpp of the attachments).
    *
    * Our best guess is that the layer/MRT state is used when computing
    * the location of a cache entry in CCU, to avoid conflicts. We assume that
    * any access in a renderpass after or before an access by a transfer needs
    * a flush/invalidate, and use the _INCOHERENT variants to represent access
    * by a transfer.
    */
   TU_ACCESS_CCU_COLOR_INCOHERENT_READ = 1 << 6,
   TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE = 1 << 7,
   TU_ACCESS_CCU_DEPTH_INCOHERENT_READ = 1 << 8,
   TU_ACCESS_CCU_DEPTH_INCOHERENT_WRITE = 1 << 9,

   /* Accesses by the host */
   TU_ACCESS_HOST_READ = 1 << 10,
   TU_ACCESS_HOST_WRITE = 1 << 11,

   /* Accesses by a GPU engine which bypasses any cache. e.g. writes via
    * CP_EVENT_WRITE::BLIT and the CP are SYSMEM_WRITE.
    */
   TU_ACCESS_SYSMEM_READ = 1 << 12,
   TU_ACCESS_SYSMEM_WRITE = 1 << 13,

   /* Set if a WFI is required. This can be required for:
    * - 2D engine which (on some models) doesn't wait for flushes to complete
    *   before starting
    * - CP draw indirect opcodes, where we need to wait for any flushes to
    *   complete but the CP implicitly waits for WFI's to complete and
    *   therefore we only need a WFI after the flushes.
    */
   TU_ACCESS_WFI_READ = 1 << 14,

   /* Set if a CP_WAIT_FOR_ME is required due to the data being read by the CP
    * without it waiting for any WFI.
    */
   TU_ACCESS_WFM_READ = 1 << 15,

   /* Memory writes from the CP start in-order with draws and event writes,
    * but execute asynchronously and hence need a CP_WAIT_MEM_WRITES if read.
    */
   TU_ACCESS_CP_WRITE = 1 << 16,

   TU_ACCESS_READ =
      TU_ACCESS_UCHE_READ |
      TU_ACCESS_CCU_COLOR_READ |
      TU_ACCESS_CCU_DEPTH_READ |
      TU_ACCESS_CCU_COLOR_INCOHERENT_READ |
      TU_ACCESS_CCU_DEPTH_INCOHERENT_READ |
      TU_ACCESS_HOST_READ |
      TU_ACCESS_SYSMEM_READ |
      TU_ACCESS_WFI_READ |
      TU_ACCESS_WFM_READ,

   TU_ACCESS_WRITE =
      TU_ACCESS_UCHE_WRITE |
      TU_ACCESS_CCU_COLOR_WRITE |
      TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE |
      TU_ACCESS_CCU_DEPTH_WRITE |
      TU_ACCESS_CCU_DEPTH_INCOHERENT_WRITE |
      TU_ACCESS_HOST_WRITE |
      TU_ACCESS_SYSMEM_WRITE |
      TU_ACCESS_CP_WRITE,

   TU_ACCESS_ALL =
      TU_ACCESS_READ |
      TU_ACCESS_WRITE,
};

enum tu_cmd_flush_bits {
   TU_CMD_FLAG_CCU_FLUSH_DEPTH = 1 << 0,
   TU_CMD_FLAG_CCU_FLUSH_COLOR = 1 << 1,
   TU_CMD_FLAG_CCU_INVALIDATE_DEPTH = 1 << 2,
   TU_CMD_FLAG_CCU_INVALIDATE_COLOR = 1 << 3,
   TU_CMD_FLAG_CACHE_FLUSH = 1 << 4,
   TU_CMD_FLAG_CACHE_INVALIDATE = 1 << 5,
   TU_CMD_FLAG_WAIT_MEM_WRITES = 1 << 6,
   TU_CMD_FLAG_WAIT_FOR_IDLE = 1 << 7,
   TU_CMD_FLAG_WAIT_FOR_ME = 1 << 8,

   TU_CMD_FLAG_ALL_FLUSH =
      TU_CMD_FLAG_CCU_FLUSH_DEPTH |
      TU_CMD_FLAG_CCU_FLUSH_COLOR |
      TU_CMD_FLAG_CACHE_FLUSH |
      /* Treat the CP as a sort of "cache" which may need to be "flushed" via
       * waiting for writes to land with WAIT_FOR_MEM_WRITES.
       */
      TU_CMD_FLAG_WAIT_MEM_WRITES,

   TU_CMD_FLAG_GPU_INVALIDATE =
      TU_CMD_FLAG_CCU_INVALIDATE_DEPTH |
      TU_CMD_FLAG_CCU_INVALIDATE_COLOR |
      TU_CMD_FLAG_CACHE_INVALIDATE,

   TU_CMD_FLAG_ALL_INVALIDATE =
      TU_CMD_FLAG_GPU_INVALIDATE |
      /* Treat the CP as a sort of "cache" which may need to be "invalidated"
       * via waiting for UCHE/CCU flushes to land with WFI/WFM.
       */
      TU_CMD_FLAG_WAIT_FOR_IDLE |
      TU_CMD_FLAG_WAIT_FOR_ME,
};

/* Changing the CCU from sysmem mode to gmem mode or vice-versa is pretty
 * heavy, involving a CCU cache flush/invalidate and a WFI in order to change
 * which part of the gmem is used by the CCU. Here we keep track of what the
 * state of the CCU.
 */
enum tu_cmd_ccu_state {
   TU_CMD_CCU_SYSMEM,
   TU_CMD_CCU_GMEM,
   TU_CMD_CCU_UNKNOWN,
};

struct tu_cache_state {
   /* Caches which must be made available (flushed) eventually if there are
    * any users outside that cache domain, and caches which must be
    * invalidated eventually if there are any reads.
    */
   enum tu_cmd_flush_bits pending_flush_bits;
   /* Pending flushes */
   enum tu_cmd_flush_bits flush_bits;
};

struct tu_cmd_state
{
   uint32_t dirty;

   struct tu_pipeline *pipeline;
   struct tu_pipeline *compute_pipeline;

   /* Vertex buffers */
   struct
   {
      struct tu_buffer *buffers[MAX_VBS];
      VkDeviceSize offsets[MAX_VBS];
   } vb;

   /* for dynamic states that can't be emitted directly */
   uint32_t dynamic_stencil_mask;
   uint32_t dynamic_stencil_wrmask;
   uint32_t dynamic_stencil_ref;
   uint32_t dynamic_gras_su_cntl;

   /* saved states to re-emit in TU_CMD_DIRTY_DRAW_STATE case */
   struct tu_draw_state dynamic_state[TU_DYNAMIC_STATE_COUNT];
   struct tu_draw_state vertex_buffers;
   struct tu_draw_state shader_const[MESA_SHADER_STAGES];
   struct tu_draw_state desc_sets;

   struct tu_draw_state vs_params;

   /* Index buffer */
   uint64_t index_va;
   uint32_t max_index_count;
   uint8_t index_size;

   /* because streamout base has to be 32-byte aligned
    * there is an extra offset to deal with when it is
    * unaligned
    */
   uint8_t streamout_offset[IR3_MAX_SO_BUFFERS];

   /* Renderpasses are tricky, because we may need to flush differently if
    * using sysmem vs. gmem and therefore we have to delay any flushing that
    * happens before a renderpass. So we have to have two copies of the flush
    * state, one for intra-renderpass flushes (i.e. renderpass dependencies)
    * and one for outside a renderpass.
    */
   struct tu_cache_state cache;
   struct tu_cache_state renderpass_cache;

   enum tu_cmd_ccu_state ccu_state;

   const struct tu_render_pass *pass;
   const struct tu_subpass *subpass;
   const struct tu_framebuffer *framebuffer;
   VkRect2D render_area;

   struct tu_cs_entry tile_store_ib;

   bool xfb_used;
};

struct tu_cmd_pool
{
   struct vk_object_base base;

   VkAllocationCallbacks alloc;
   struct list_head cmd_buffers;
   struct list_head free_cmd_buffers;
   uint32_t queue_family_index;
};

struct tu_cmd_buffer_upload
{
   uint8_t *map;
   unsigned offset;
   uint64_t size;
   struct list_head list;
};

enum tu_cmd_buffer_status
{
   TU_CMD_BUFFER_STATUS_INVALID,
   TU_CMD_BUFFER_STATUS_INITIAL,
   TU_CMD_BUFFER_STATUS_RECORDING,
   TU_CMD_BUFFER_STATUS_EXECUTABLE,
   TU_CMD_BUFFER_STATUS_PENDING,
};

struct tu_bo_list
{
   uint32_t count;
   uint32_t capacity;
   struct drm_msm_gem_submit_bo *bo_infos;
};

#define TU_BO_LIST_FAILED (~0)

void
tu_bo_list_init(struct tu_bo_list *list);
void
tu_bo_list_destroy(struct tu_bo_list *list);
void
tu_bo_list_reset(struct tu_bo_list *list);
uint32_t
tu_bo_list_add(struct tu_bo_list *list,
               const struct tu_bo *bo,
               uint32_t flags);
VkResult
tu_bo_list_merge(struct tu_bo_list *list, const struct tu_bo_list *other);

struct tu_cmd_buffer
{
   struct vk_object_base base;

   struct tu_device *device;

   struct tu_cmd_pool *pool;
   struct list_head pool_link;

   VkCommandBufferUsageFlags usage_flags;
   VkCommandBufferLevel level;
   enum tu_cmd_buffer_status status;

   struct tu_cmd_state state;
   struct tu_vertex_binding vertex_bindings[MAX_VBS];
   uint32_t vertex_bindings_set;
   uint32_t queue_family_index;

   uint32_t push_constants[MAX_PUSH_CONSTANTS_SIZE / 4];
   VkShaderStageFlags push_constant_stages;
   struct tu_descriptor_set meta_push_descriptors;

   struct tu_descriptor_state descriptors[MAX_BIND_POINTS];

   struct tu_cmd_buffer_upload upload;

   VkResult record_result;

   struct tu_bo_list bo_list;
   struct tu_cs cs;
   struct tu_cs draw_cs;
   struct tu_cs draw_epilogue_cs;
   struct tu_cs sub_cs;

   bool has_tess;

   uint32_t vsc_draw_strm_pitch;
   uint32_t vsc_prim_strm_pitch;
};

/* Temporary struct for tracking a register state to be written, used by
 * a6xx-pack.h and tu_cs_emit_regs()
 */
struct tu_reg_value {
   uint32_t reg;
   uint64_t value;
   bool is_address;
   struct tu_bo *bo;
   bool bo_write;
   uint32_t bo_offset;
   uint32_t bo_shift;
};


void tu_emit_cache_flush_renderpass(struct tu_cmd_buffer *cmd_buffer,
                                    struct tu_cs *cs);

void tu_emit_cache_flush_ccu(struct tu_cmd_buffer *cmd_buffer,
                             struct tu_cs *cs,
                             enum tu_cmd_ccu_state ccu_state);

void
tu6_emit_event_write(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     enum vgt_event_type event);

static inline struct tu_descriptor_state *
tu_get_descriptors_state(struct tu_cmd_buffer *cmd_buffer,
                         VkPipelineBindPoint bind_point)
{
   return &cmd_buffer->descriptors[bind_point];
}

struct tu_event
{
   struct vk_object_base base;
   struct tu_bo bo;
};

struct tu_shader_module
{
   struct vk_object_base base;

   unsigned char sha1[20];

   uint32_t code_size;
   const uint32_t *code[0];
};

struct tu_push_constant_range
{
   uint32_t lo;
   uint32_t count;
};

struct tu_shader
{
   struct ir3_shader *ir3_shader;

   struct tu_push_constant_range push_consts;
   uint8_t active_desc_sets;
};

struct tu_shader *
tu_shader_create(struct tu_device *dev,
                 gl_shader_stage stage,
                 const VkPipelineShaderStageCreateInfo *stage_info,
                 struct tu_pipeline_layout *layout,
                 const VkAllocationCallbacks *alloc);

void
tu_shader_destroy(struct tu_device *dev,
                  struct tu_shader *shader,
                  const VkAllocationCallbacks *alloc);

struct tu_program_descriptor_linkage
{
   struct ir3_const_state const_state;

   uint32_t constlen;

   struct tu_push_constant_range push_consts;
};

struct tu_pipeline
{
   struct vk_object_base base;

   struct tu_cs cs;

   struct tu_pipeline_layout *layout;

   bool need_indirect_descriptor_sets;
   VkShaderStageFlags active_stages;
   uint32_t active_desc_sets;

   /* mask of enabled dynamic states
    * if BIT(i) is set, pipeline->dynamic_state[i] is *NOT* used
    */
   uint32_t dynamic_state_mask;
   struct tu_draw_state dynamic_state[TU_DYNAMIC_STATE_COUNT];

   /* gras_su_cntl without line width, used for dynamic line width state */
   uint32_t gras_su_cntl;

   /* draw states for the pipeline */
   struct tu_draw_state load_state, rast_state, ds_state, blend_state;

   struct
   {
      struct tu_draw_state state;
      struct tu_draw_state binning_state;

      struct tu_program_descriptor_linkage link[MESA_SHADER_STAGES];
   } program;

   struct
   {
      struct tu_draw_state state;
      struct tu_draw_state binning_state;
      uint32_t bindings_used;
   } vi;

   struct
   {
      enum pc_di_primtype primtype;
      bool primitive_restart;
   } ia;

   struct
   {
      uint32_t patch_type;
      uint32_t param_stride;
      uint32_t hs_bo_regid;
      uint32_t ds_bo_regid;
      bool upper_left_domain_origin;
   } tess;

   struct
   {
      uint32_t local_size[3];
   } compute;
};

void
tu6_emit_viewport(struct tu_cs *cs, const VkViewport *viewport);

void
tu6_emit_scissor(struct tu_cs *cs, const VkRect2D *scissor);

void
tu6_emit_sample_locations(struct tu_cs *cs, const VkSampleLocationsInfoEXT *samp_loc);

void
tu6_emit_depth_bias(struct tu_cs *cs,
                    float constant_factor,
                    float clamp,
                    float slope_factor);

void tu6_emit_msaa(struct tu_cs *cs, VkSampleCountFlagBits samples);

void tu6_emit_window_scissor(struct tu_cs *cs, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2);

void tu6_emit_window_offset(struct tu_cs *cs, uint32_t x1, uint32_t y1);

void
tu6_emit_xs_config(struct tu_cs *cs,
                   gl_shader_stage stage,
                   const struct ir3_shader_variant *xs,
                   uint64_t binary_iova);

void
tu6_emit_vpc(struct tu_cs *cs,
             const struct ir3_shader_variant *vs,
             const struct ir3_shader_variant *hs,
             const struct ir3_shader_variant *ds,
             const struct ir3_shader_variant *gs,
             const struct ir3_shader_variant *fs,
             uint32_t patch_control_points,
             bool vshs_workgroup);

void
tu6_emit_fs_inputs(struct tu_cs *cs, const struct ir3_shader_variant *fs);

struct tu_image_view;

void
tu_resolve_sysmem(struct tu_cmd_buffer *cmd,
                  struct tu_cs *cs,
                  struct tu_image_view *src,
                  struct tu_image_view *dst,
                  uint32_t layers,
                  const VkRect2D *rect);

void
tu_clear_sysmem_attachment(struct tu_cmd_buffer *cmd,
                           struct tu_cs *cs,
                           uint32_t a,
                           const VkRenderPassBeginInfo *info);

void
tu_clear_gmem_attachment(struct tu_cmd_buffer *cmd,
                         struct tu_cs *cs,
                         uint32_t a,
                         const VkRenderPassBeginInfo *info);

void
tu_load_gmem_attachment(struct tu_cmd_buffer *cmd,
                        struct tu_cs *cs,
                        uint32_t a,
                        bool force_load);

/* expose this function to be able to emit load without checking LOAD_OP */
void
tu_emit_load_gmem_attachment(struct tu_cmd_buffer *cmd, struct tu_cs *cs, uint32_t a);

/* note: gmem store can also resolve */
void
tu_store_gmem_attachment(struct tu_cmd_buffer *cmd,
                         struct tu_cs *cs,
                         uint32_t a,
                         uint32_t gmem_a);

enum tu_supported_formats {
   FMT_VERTEX = 1,
   FMT_TEXTURE = 2,
   FMT_COLOR = 4,
};

struct tu_native_format
{
   enum a6xx_format fmt : 8;
   enum a3xx_color_swap swap : 8;
   enum a6xx_tile_mode tile_mode : 8;
   enum tu_supported_formats supported : 8;
};

struct tu_native_format tu6_format_vtx(VkFormat format);
struct tu_native_format tu6_format_color(VkFormat format, enum a6xx_tile_mode tile_mode);
struct tu_native_format tu6_format_texture(VkFormat format, enum a6xx_tile_mode tile_mode);

static inline enum a6xx_format
tu6_base_format(VkFormat format)
{
   /* note: tu6_format_color doesn't care about tiling for .fmt field */
   return tu6_format_color(format, TILE6_LINEAR).fmt;
}

struct tu_image
{
   struct vk_object_base base;

   VkImageType type;
   /* The original VkFormat provided by the client.  This may not match any
    * of the actual surface formats.
    */
   VkFormat vk_format;
   VkImageAspectFlags aspects;
   VkImageUsageFlags usage;  /**< Superset of VkImageCreateInfo::usage. */
   VkImageTiling tiling;     /** VkImageCreateInfo::tiling */
   VkImageCreateFlags flags; /** VkImageCreateInfo::flags */
   VkExtent3D extent;
   uint32_t level_count;
   uint32_t layer_count;
   VkSampleCountFlagBits samples;

   struct fdl_layout layout[3];
   uint32_t total_size;

   unsigned queue_family_mask;
   bool exclusive;
   bool shareable;

   /* For VK_ANDROID_native_buffer, the WSI image owns the memory, */
   VkDeviceMemory owned_memory;

   /* Set when bound */
   struct tu_bo *bo;
   VkDeviceSize bo_offset;
};

static inline uint32_t
tu_get_layerCount(const struct tu_image *image,
                  const VkImageSubresourceRange *range)
{
   return range->layerCount == VK_REMAINING_ARRAY_LAYERS
             ? image->layer_count - range->baseArrayLayer
             : range->layerCount;
}

static inline uint32_t
tu_get_levelCount(const struct tu_image *image,
                  const VkImageSubresourceRange *range)
{
   return range->levelCount == VK_REMAINING_MIP_LEVELS
             ? image->level_count - range->baseMipLevel
             : range->levelCount;
}

struct tu_image_view
{
   struct vk_object_base base;

   struct tu_image *image; /**< VkImageViewCreateInfo::image */

   uint64_t base_addr;
   uint64_t ubwc_addr;
   uint32_t layer_size;
   uint32_t ubwc_layer_size;

   /* used to determine if fast gmem store path can be used */
   VkExtent2D extent;
   bool need_y2_align;

   bool ubwc_enabled;

   uint32_t descriptor[A6XX_TEX_CONST_DWORDS];

   /* Descriptor for use as a storage image as opposed to a sampled image.
    * This has a few differences for cube maps (e.g. type).
    */
   uint32_t storage_descriptor[A6XX_TEX_CONST_DWORDS];

   /* pre-filled register values */
   uint32_t PITCH;
   uint32_t FLAG_BUFFER_PITCH;

   uint32_t RB_MRT_BUF_INFO;
   uint32_t SP_FS_MRT_REG;

   uint32_t SP_PS_2D_SRC_INFO;
   uint32_t SP_PS_2D_SRC_SIZE;

   uint32_t RB_2D_DST_INFO;

   uint32_t RB_BLIT_DST_INFO;
};

struct tu_sampler_ycbcr_conversion {
   struct vk_object_base base;

   VkFormat format;
   VkSamplerYcbcrModelConversion ycbcr_model;
   VkSamplerYcbcrRange ycbcr_range;
   VkComponentMapping components;
   VkChromaLocation chroma_offsets[2];
   VkFilter chroma_filter;
};

struct tu_sampler {
   struct vk_object_base base;

   uint32_t descriptor[A6XX_TEX_SAMP_DWORDS];
   struct tu_sampler_ycbcr_conversion *ycbcr_sampler;
};

void
tu_cs_image_ref(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer);

void
tu_cs_image_ref_2d(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer, bool src);

void
tu_cs_image_flag_ref(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer);

VkResult
tu_image_create(VkDevice _device,
                const VkImageCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *alloc,
                VkImage *pImage,
                uint64_t modifier,
                const VkSubresourceLayout *plane_layouts);

VkResult
tu_image_from_gralloc(VkDevice device_h,
                      const VkImageCreateInfo *base_info,
                      const VkNativeBufferANDROID *gralloc_info,
                      const VkAllocationCallbacks *alloc,
                      VkImage *out_image_h);

void
tu_image_view_init(struct tu_image_view *view,
                   const VkImageViewCreateInfo *pCreateInfo);

struct tu_buffer_view
{
   struct vk_object_base base;

   uint32_t descriptor[A6XX_TEX_CONST_DWORDS];

   struct tu_buffer *buffer;
};
void
tu_buffer_view_init(struct tu_buffer_view *view,
                    struct tu_device *device,
                    const VkBufferViewCreateInfo *pCreateInfo);

struct tu_attachment_info
{
   struct tu_image_view *attachment;
};

struct tu_framebuffer
{
   struct vk_object_base base;

   uint32_t width;
   uint32_t height;
   uint32_t layers;

   /* size of the first tile */
   VkExtent2D tile0;
   /* number of tiles */
   VkExtent2D tile_count;

   /* size of the first VSC pipe */
   VkExtent2D pipe0;
   /* number of VSC pipes */
   VkExtent2D pipe_count;

   /* pipe register values */
   uint32_t pipe_config[MAX_VSC_PIPES];
   uint32_t pipe_sizes[MAX_VSC_PIPES];

   uint32_t attachment_count;
   struct tu_attachment_info attachments[0];
};

void
tu_framebuffer_tiling_config(struct tu_framebuffer *fb,
                             const struct tu_device *device,
                             const struct tu_render_pass *pass);

struct tu_subpass_barrier {
   VkPipelineStageFlags src_stage_mask;
   VkAccessFlags src_access_mask;
   VkAccessFlags dst_access_mask;
   bool incoherent_ccu_color, incoherent_ccu_depth;
};

struct tu_subpass_attachment
{
   uint32_t attachment;
};

struct tu_subpass
{
   uint32_t input_count;
   uint32_t color_count;
   struct tu_subpass_attachment *input_attachments;
   struct tu_subpass_attachment *color_attachments;
   struct tu_subpass_attachment *resolve_attachments;
   struct tu_subpass_attachment depth_stencil_attachment;

   VkSampleCountFlagBits samples;

   uint32_t srgb_cntl;

   struct tu_subpass_barrier start_barrier;
};

struct tu_render_pass_attachment
{
   VkFormat format;
   uint32_t samples;
   uint32_t cpp;
   VkImageAspectFlags clear_mask;
   bool load;
   bool store;
   int32_t gmem_offset;
};

struct tu_render_pass
{
   struct vk_object_base base;

   uint32_t attachment_count;
   uint32_t subpass_count;
   uint32_t gmem_pixels;
   uint32_t tile_align_w;
   struct tu_subpass_attachment *subpass_attachments;
   struct tu_render_pass_attachment *attachments;
   struct tu_subpass_barrier end_barrier;
   struct tu_subpass subpasses[0];
};

struct tu_query_pool
{
   struct vk_object_base base;

   VkQueryType type;
   uint32_t stride;
   uint64_t size;
   uint32_t pipeline_statistics;
   struct tu_bo bo;
};

enum tu_semaphore_kind
{
   TU_SEMAPHORE_NONE,
   TU_SEMAPHORE_SYNCOBJ,
};

struct tu_semaphore_part
{
   enum tu_semaphore_kind kind;
   union {
      uint32_t syncobj;
   };
};

struct tu_semaphore
{
   struct vk_object_base base;

   struct tu_semaphore_part permanent;
   struct tu_semaphore_part temporary;
};

void
tu_set_descriptor_set(struct tu_cmd_buffer *cmd_buffer,
                      VkPipelineBindPoint bind_point,
                      struct tu_descriptor_set *set,
                      unsigned idx);

void
tu_update_descriptor_sets(struct tu_device *device,
                          struct tu_cmd_buffer *cmd_buffer,
                          VkDescriptorSet overrideSet,
                          uint32_t descriptorWriteCount,
                          const VkWriteDescriptorSet *pDescriptorWrites,
                          uint32_t descriptorCopyCount,
                          const VkCopyDescriptorSet *pDescriptorCopies);

void
tu_update_descriptor_set_with_template(
   struct tu_device *device,
   struct tu_cmd_buffer *cmd_buffer,
   struct tu_descriptor_set *set,
   VkDescriptorUpdateTemplate descriptorUpdateTemplate,
   const void *pData);

int
tu_drm_get_gpu_id(const struct tu_physical_device *dev, uint32_t *id);

int
tu_drm_get_gmem_size(const struct tu_physical_device *dev, uint32_t *size);

int
tu_drm_get_gmem_base(const struct tu_physical_device *dev, uint64_t *base);

int
tu_drm_submitqueue_new(const struct tu_device *dev,
                       int priority,
                       uint32_t *queue_id);

void
tu_drm_submitqueue_close(const struct tu_device *dev, uint32_t queue_id);

uint32_t
tu_gem_new(const struct tu_device *dev, uint64_t size, uint32_t flags);
uint32_t
tu_gem_import_dmabuf(const struct tu_device *dev,
                     int prime_fd,
                     uint64_t size);
int
tu_gem_export_dmabuf(const struct tu_device *dev, uint32_t gem_handle);
void
tu_gem_close(const struct tu_device *dev, uint32_t gem_handle);
uint64_t
tu_gem_info_offset(const struct tu_device *dev, uint32_t gem_handle);
uint64_t
tu_gem_info_iova(const struct tu_device *dev, uint32_t gem_handle);

#define TU_DEFINE_HANDLE_CASTS(__tu_type, __VkType)                          \
                                                                             \
   static inline struct __tu_type *__tu_type##_from_handle(__VkType _handle) \
   {                                                                         \
      return (struct __tu_type *) _handle;                                   \
   }                                                                         \
                                                                             \
   static inline __VkType __tu_type##_to_handle(struct __tu_type *_obj)      \
   {                                                                         \
      return (__VkType) _obj;                                                \
   }

#define TU_DEFINE_NONDISP_HANDLE_CASTS(__tu_type, __VkType)                  \
                                                                             \
   static inline struct __tu_type *__tu_type##_from_handle(__VkType _handle) \
   {                                                                         \
      return (struct __tu_type *) (uintptr_t) _handle;                       \
   }                                                                         \
                                                                             \
   static inline __VkType __tu_type##_to_handle(struct __tu_type *_obj)      \
   {                                                                         \
      return (__VkType)(uintptr_t) _obj;                                     \
   }

#define TU_FROM_HANDLE(__tu_type, __name, __handle)                          \
   struct __tu_type *__name = __tu_type##_from_handle(__handle)

TU_DEFINE_HANDLE_CASTS(tu_cmd_buffer, VkCommandBuffer)
TU_DEFINE_HANDLE_CASTS(tu_device, VkDevice)
TU_DEFINE_HANDLE_CASTS(tu_instance, VkInstance)
TU_DEFINE_HANDLE_CASTS(tu_physical_device, VkPhysicalDevice)
TU_DEFINE_HANDLE_CASTS(tu_queue, VkQueue)

TU_DEFINE_NONDISP_HANDLE_CASTS(tu_cmd_pool, VkCommandPool)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_buffer, VkBuffer)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_buffer_view, VkBufferView)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_descriptor_pool, VkDescriptorPool)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_descriptor_set, VkDescriptorSet)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_descriptor_set_layout,
                               VkDescriptorSetLayout)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_descriptor_update_template,
                               VkDescriptorUpdateTemplate)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_device_memory, VkDeviceMemory)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_fence, VkFence)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_event, VkEvent)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_framebuffer, VkFramebuffer)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_image, VkImage)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_image_view, VkImageView);
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_pipeline_cache, VkPipelineCache)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_pipeline, VkPipeline)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_pipeline_layout, VkPipelineLayout)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_query_pool, VkQueryPool)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_render_pass, VkRenderPass)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_sampler, VkSampler)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_sampler_ycbcr_conversion, VkSamplerYcbcrConversion)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_shader_module, VkShaderModule)
TU_DEFINE_NONDISP_HANDLE_CASTS(tu_semaphore, VkSemaphore)

#endif /* TU_PRIVATE_H */
