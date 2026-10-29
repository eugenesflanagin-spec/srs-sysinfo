/* gpu_kernels.h - the compute kernels that HIP (hipRTC), CUDA (NVRTC) and OpenCL (clBuildProgram) compile
 * at run time (gpuburn_rt.c, gpuburn_cl.c). One source text, small dialect preambles; the same work as
 * the Vulkan shaders in shaders/gpu/. The CUDA back end normally does not compile at run time: it loads
 * the same kernels precompiled to PTX (gpu_kernels_ptx.inc, made from this text with the GKD_PTX preamble
 * by tools/gen-cuda-ptx.sh = make cuda-ptx). Internal to SRS. */
#ifndef GPU_KERNELS_H
#define GPU_KERNELS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { GKD_CUDA_HIP, GKD_OPENCL, GKD_PTX };

static const char gk_pre_cuda[] =
    "#define KERNEL extern \"C\" __global__\n#define GLOBAL\n#define LOCAL __shared__\n"
    "#define LID_X threadIdx.x\n#define LID_Y threadIdx.y\n"
    "#define GID_X (blockIdx.x * blockDim.x + threadIdx.x)\n#define GID_Y (blockIdx.y * blockDim.y + threadIdx.y)\n"
    "#define GSIZE_X (gridDim.x * blockDim.x)\n#define BARRIER() __syncthreads()\n#define ATOMIC_ADD(p, v) atomicAdd(p, v)\n"
    "#define FMA32(a, b, c) fmaf(a, b, c)\n#define FMA64(a, b, c) fma(a, b, c)\n";

static const char gk_pre_cl[] =
    "#define KERNEL __kernel\n#define GLOBAL __global\n#define LOCAL __local\n"
    "#define LID_X get_local_id(0)\n#define LID_Y get_local_id(1)\n"
    "#define GID_X get_global_id(0)\n#define GID_Y get_global_id(1)\n"
    "#define GSIZE_X get_global_size(0)\n#define BARRIER() barrier(CLK_LOCAL_MEM_FENCE)\n#define ATOMIC_ADD(p, v) atomic_add(p, v)\n"
    "#define FMA32(a, b, c) fma(a, b, c)\n#define FMA64(a, b, c) fma(a, b, c)\n";

/* CUDA for clang without the CUDA headers (tools/gen-cuda-ptx.sh only): clang's own NVPTX builtins in place
   of threadIdx / atomicAdd / fmaf, and uint4 as a 16-byte vector so copy_u4 moves 16 bytes per load like NVRTC's */
static const char gk_pre_ptx[] =
    "#define KERNEL extern \"C\" __attribute__((global))\n#define GLOBAL\n#define LOCAL __attribute__((shared))\n"
    "#define LID_X ((unsigned)__nvvm_read_ptx_sreg_tid_x())\n#define LID_Y ((unsigned)__nvvm_read_ptx_sreg_tid_y())\n"
    "#define GID_X ((unsigned)__nvvm_read_ptx_sreg_ctaid_x() * (unsigned)__nvvm_read_ptx_sreg_ntid_x() + (unsigned)__nvvm_read_ptx_sreg_tid_x())\n"
    "#define GID_Y ((unsigned)__nvvm_read_ptx_sreg_ctaid_y() * (unsigned)__nvvm_read_ptx_sreg_ntid_y() + (unsigned)__nvvm_read_ptx_sreg_tid_y())\n"
    "#define GSIZE_X ((unsigned)__nvvm_read_ptx_sreg_nctaid_x() * (unsigned)__nvvm_read_ptx_sreg_ntid_x())\n"
    "#define BARRIER() __syncthreads()\n#define ATOMIC_ADD(p, v) __nvvm_atom_add_gen_i((int *)(p), (int)(v))\n"
    "#define FMA32(a, b, c) __builtin_fmaf(a, b, c)\n#define FMA64(a, b, c) __builtin_fma(a, b, c)\n"
    "typedef unsigned uint4 __attribute__((ext_vector_type(4)));\n";

/* REAL / FMAR are set per build: float + FMA32, or double + FMA64 */
static const char gk_body[] =
    "KERNEL void gemm_r(GLOBAL const REAL *A, GLOBAL const REAL *B, GLOBAL REAL *C, int n)\n"
    "{\n"
    "    LOCAL REAL ta[16][16];\n    LOCAL REAL tb[16][16];\n"
    "    int lx = LID_X, ly = LID_Y, col = GID_X, row = GID_Y, t, k;\n"
    "    REAL acc = 0;\n"
    "    for (t = 0; t < n; t += 16) {\n"
    "        ta[ly][lx] = A[row * n + t + lx];\n        tb[ly][lx] = B[(t + ly) * n + col];\n"
    "        BARRIER();\n"
    "        for (k = 0; k < 16; k++) acc = FMAR(ta[ly][k], tb[k][lx], acc);\n"
    "        BARRIER();\n"
    "    }\n"
    "    C[row * n + col] = acc;\n"
    "}\n"
    "KERNEL void fma_r(GLOBAL REAL *o, REAL m1, REAL c1, REAL m2, REAL c2, unsigned iters, unsigned seed, unsigned n)\n"
    "{\n"
    "    unsigned g = GID_X, i;\n"
    "    REAL a0 = (REAL)((g + seed) & 255u), a1 = (REAL)((g * 3u + 1u) & 255u), a2 = (REAL)((g * 5u + 2u) & 255u), a3 = (REAL)((g * 7u + 3u) & 255u);\n"
    "    REAL a4 = (REAL)((g * 11u + 4u) & 255u), a5 = (REAL)((g * 13u + 5u) & 255u), a6 = (REAL)((g * 17u + 6u) & 255u), a7 = (REAL)((g * 19u + 7u) & 255u);\n"
    "    for (i = 0; i < iters; i++) {\n"
    "        a0 = FMAR(a0, m1, c1); a1 = FMAR(a1, m1, c1); a2 = FMAR(a2, m1, c1); a3 = FMAR(a3, m1, c1);\n"
    "        a4 = FMAR(a4, m1, c1); a5 = FMAR(a5, m1, c1); a6 = FMAR(a6, m1, c1); a7 = FMAR(a7, m1, c1);\n"
    "        a0 = FMAR(a0, m2, c2); a1 = FMAR(a1, m2, c2); a2 = FMAR(a2, m2, c2); a3 = FMAR(a3, m2, c2);\n"
    "        a4 = FMAR(a4, m2, c2); a5 = FMAR(a5, m2, c2); a6 = FMAR(a6, m2, c2); a7 = FMAR(a7, m2, c2);\n"
    "    }\n"
    "    if (g < n) o[g] = ((a0 + a1) + (a2 + a3)) + ((a4 + a5) + (a6 + a7));\n"
    "}\n"
    "KERNEL void copy_u4(GLOBAL const uint4 *s, GLOBAL uint4 *d, unsigned n4)\n"
    "{\n"
    "    unsigned i, stride = GSIZE_X;\n"
    "    for (i = GID_X; i < n4; i += stride) d[i] = s[i];\n"
    "}\n"
    "KERNEL void cmp_u32(GLOBAL const unsigned *x, GLOBAL const unsigned *y, GLOBAL unsigned *cnt, unsigned words)\n"
    "{\n"
    "    unsigned i, stride = GSIZE_X, bad = 0u;\n"
    "    for (i = GID_X; i < words; i += stride) bad += (x[i] != y[i]) ? 1u : 0u;\n"
    "    if (bad != 0u) ATOMIC_ADD(cnt, bad);\n"
    "}\n"
    "KERNEL void tick(GLOBAL unsigned *c)\n"
    "{\n"
    "    c[0] += 1u;\n"
    "}\n";

/* malloc'd source text for a dialect and a precision (GP_FP64 = double, anything else float) */
static inline char *gk_source(int dialect, int fp64)
{
    const char *pre = dialect == GKD_OPENCL ? gk_pre_cl : dialect == GKD_PTX ? gk_pre_ptx : gk_pre_cuda;
    const char *def = fp64 ? "#define REAL double\n#define FMAR(a, b, c) FMA64(a, b, c)\n" : "#define REAL float\n#define FMAR(a, b, c) FMA32(a, b, c)\n";
    const char *ext = dialect == GKD_OPENCL && fp64 ? "#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n" : "";
    size_t n = strlen(pre) + strlen(def) + strlen(ext) + sizeof gk_body + 8;
    char *s = (char *)malloc(n);
    if (s) snprintf(s, n, "%s%s%s%s", ext, pre, def, gk_body);
    return s;
}

#endif
