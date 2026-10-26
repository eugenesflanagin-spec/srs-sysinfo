/* gpu_backend.c - names, hints and tables shared by the GPU compute back ends (see gpu_backend.h). */
#include "gpu_backend.h"

#include <stdio.h>
#include <string.h>

const char *const gb_id[GB_N] = { "vulkan", "hip", "cuda", "opencl" };
const char *const gb_name[GB_N] = { "Vulkan", "ROCm / HIP", "CUDA", "OpenCL" };
const char *const gb_hint[GB_N] = {
    "not installed - install the Vulkan loader and your GPU's Vulkan driver",
    "not installed - install ROCm (the HIP runtime)",
    "not installed - install the NVIDIA driver and CUDA",
    "not installed - install an OpenCL driver (ROCm, Mesa Rusticl, NVIDIA, PoCL ...)"
};
const gb_ops *const gb_table[GB_N] = { &gb_vulkan, &gb_hip, &gb_cuda, &gb_opencl };

int gb_bind_check(int backend, char *detail, size_t dn)
{
    if (detail && dn) detail[0] = 0;
    switch (backend) {
    case GB_HIP: return gb_rt_bind_check(0, detail, dn);
    case GB_CUDA: return gb_rt_bind_check(1, detail, dn);
    case GB_OPENCL: return gb_cl_bind_check(detail, dn);
    default: return gb_vulkan.available(detail, dn) ? 0 : -1;
    }
}

int gb_find_by_pci(const gb_info *l, int n, const char *pci)
{
    int i;
    if (!pci || !pci[0]) return -1;
    for (i = 0; i < n; i++) if (l[i].pci[0] && !strcmp(l[i].pci, pci)) return i;
    return -1;
}
