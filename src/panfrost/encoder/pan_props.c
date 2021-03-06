/*
 * Copyright (C) 2019 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include <xf86drm.h>

#include "util/u_math.h"
#include "util/macros.h"
#include "util/hash_table.h"
#include "util/u_thread.h"
#include "drm-uapi/panfrost_drm.h"
#include "pan_encoder.h"
#include "pan_device.h"
#include "panfrost-quirks.h"
#include "pan_bo.h"

/* Abstraction over the raw drm_panfrost_get_param ioctl for fetching
 * information about devices */

static __u64
panfrost_query_raw(
                int fd,
                enum drm_panfrost_param param,
                bool required,
                unsigned default_value)
{
        struct drm_panfrost_get_param get_param = {0,};
        ASSERTED int ret;

        get_param.param = param;
        ret = drmIoctl(fd, DRM_IOCTL_PANFROST_GET_PARAM, &get_param);

        if (ret) {
                assert(!required);
                return default_value;
        }

        return get_param.value;
}

unsigned
panfrost_query_gpu_version(int fd)
{
        return panfrost_query_raw(fd, DRM_PANFROST_PARAM_GPU_PROD_ID, true, 0);
}

unsigned
panfrost_query_core_count(int fd)
{
        /* On older kernels, worst-case to 16 cores */

        unsigned mask = panfrost_query_raw(fd,
                        DRM_PANFROST_PARAM_SHADER_PRESENT, false, 0xffff);

        return util_bitcount(mask);
}

unsigned
panfrost_query_thread_tls_alloc(int fd)
{
        /* On older kernels, we worst-case to 256 threads, the architectural
         * maximum for Midgard. On my current kernel/hardware, I'm seeing this
         * readback as 0, so we'll worst-case there too */

        unsigned tls = panfrost_query_raw(fd,
                        DRM_PANFROST_PARAM_THREAD_TLS_ALLOC, false, 256);

        if (tls)
                return tls;
        else
                return 256;
}

static uint32_t
panfrost_query_compressed_formats(int fd)
{
        /* If unspecified, assume ASTC/ETC only. Factory default for Juno, and
         * should exist on any Mali configuration. All hardware should report
         * these texture formats but the kernel might not be new enough. */

        uint32_t default_set =
                (1 << MALI_ETC2_RGB8) |
                (1 << MALI_ETC2_R11_UNORM) |
                (1 << MALI_ETC2_RGBA8) |
                (1 << MALI_ETC2_RG11_UNORM) |
                (1 << MALI_ETC2_R11_SNORM) |
                (1 << MALI_ETC2_RG11_SNORM) |
                (1 << MALI_ETC2_RGB8A1) |
                (1 << MALI_ASTC_3D_LDR) |
                (1 << MALI_ASTC_3D_HDR) |
                (1 << MALI_ASTC_2D_LDR) |
                (1 << MALI_ASTC_2D_HDR);

        return panfrost_query_raw(fd, DRM_PANFROST_PARAM_TEXTURE_FEATURES0,
                        false, default_set);
}

/* DRM_PANFROST_PARAM_TEXTURE_FEATURES0 will return a bitmask of supported
 * compressed formats, so we offer a helper to test if a format is supported */

bool
panfrost_supports_compressed_format(struct panfrost_device *dev, unsigned fmt)
{
        if (MALI_EXTRACT_TYPE(fmt) != MALI_FORMAT_COMPRESSED)
                return true;

        unsigned idx = fmt & ~MALI_FORMAT_COMPRESSED;
        assert(idx < 32);

        return dev->compressed_formats & (1 << idx);
}

/* Given a GPU ID like 0x860, return a prettified model name */

const char *
panfrost_model_name(unsigned gpu_id)
{
        switch (gpu_id) {
        case 0x600: return "Mali T600 (Panfrost)";
        case 0x620: return "Mali T620 (Panfrost)";
        case 0x720: return "Mali T720 (Panfrost)";
        case 0x820: return "Mali T820 (Panfrost)";
        case 0x830: return "Mali T830 (Panfrost)";
        case 0x750: return "Mali T760 (Panfrost)";
        case 0x860: return "Mali T860 (Panfrost)";
        case 0x880: return "Mali T880 (Panfrost)";
        case 0x7093: return "Mali G31 (Panfrost)";
        case 0x7212: return "Mali G52 (Panfrost)";
        default:
                    unreachable("Invalid GPU ID");
        }
}

void
panfrost_open_device(void *memctx, int fd, struct panfrost_device *dev)
{
        dev->fd = fd;
        dev->memctx = memctx;
        dev->gpu_id = panfrost_query_gpu_version(fd);
        dev->core_count = panfrost_query_core_count(fd);
        dev->thread_tls_alloc = panfrost_query_thread_tls_alloc(fd);
        dev->kernel_version = drmGetVersion(fd);
        dev->quirks = panfrost_get_quirks(dev->gpu_id);
        dev->compressed_formats = panfrost_query_compressed_formats(fd);

        util_sparse_array_init(&dev->bo_map, sizeof(struct panfrost_bo), 512);

        pthread_mutex_init(&dev->bo_cache.lock, NULL);
        list_inithead(&dev->bo_cache.lru);

        for (unsigned i = 0; i < ARRAY_SIZE(dev->bo_cache.buckets); ++i)
                list_inithead(&dev->bo_cache.buckets[i]);
}

void
panfrost_close_device(struct panfrost_device *dev)
{
        panfrost_bo_unreference(dev->blit_shaders.bo);
        panfrost_bo_cache_evict_all(dev);
        pthread_mutex_destroy(&dev->bo_cache.lock);
        drmFreeVersion(dev->kernel_version);
        util_sparse_array_finish(&dev->bo_map);

}
