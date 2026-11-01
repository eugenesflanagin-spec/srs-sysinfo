#!/usr/bin/env python3
"""make-multi.py - build tests/gpu_top_fixtures/multi: a made-up system with five GPUs for tests/gpu-top-test.sh
and for running the GPU Top window without hardware (SRS_TEST_GPU_ROOT=<abs>/tests/gpu_top_fixtures/multi):

  0000:01:00.0  NVIDIA (vendor 0x10de) - data from the NVML stub (tests/nvml_stub.c)
  0000:03:00.0  AMD Radeon RX 9070 XT  - the real capture in ../rx9070xt (gpu_metrics v1.3)
  0000:0a:00.0  AMD Navi 21 (RX 6800)  - made up: gpu_metrics v1.3, pp_table smu_11_0_7, SMU 11.0.7
  0000:0c:00.0  AMD Rembrandt APU      - made up: gpu_metrics v2.2
  0000:0e:00.0  AMD Strix Point APU    - made up: gpu_metrics v3.0 (with NPU fields)

Every value is invented except the RX 9070 XT's. Run from anywhere: python3 -I make-multi.py
"""
import os, re, shutil, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "multi")
INC = os.path.join(HERE, "..", "..", "gpu_top_metrics.inc")


def layouts():
    """the gpu_metrics field tables of gpu_top_metrics.inc: {(fmt, cnt): (size, [(name, off, size, count)])}"""
    text = open(INC).read()
    fields = {}
    for m in re.finditer(r"static const gt_mfield gm_(v\d_\d)\[\] = \{(.*?)\n\};", text, re.S):
        fields[m.group(1)] = [(n, int(o), int(s), int(c)) for n, o, s, c in
                              re.findall(r'\{"(\w+)", (\d+), (\d+), (\d+)\}', m.group(2))]
    out = {}
    for f, c, size, name in re.findall(r"\{(\d), (\d), (\d+), gm_(v\d_\d)\}", text):
        out[(int(f), int(c))] = (int(size), fields[name])
    return out


def metrics_blob(fmt, cnt, values):
    size, fl = layouts()[(fmt, cnt)]
    b = bytearray(size)
    struct.pack_into("<HBB", b, 0, size, fmt, cnt)
    fmtc = {1: "B", 2: "H", 4: "I", 8: "Q"}
    for name, off, sz, n in fl:
        v = values.get(name)
        if v is None:   # not given: "not supported"
            v = [(1 << (8 * sz)) - 1] * n if sz < 8 else [0] * n
        if not isinstance(v, list):
            v = [v] * n
        for i in range(n):
            struct.pack_into("<" + fmtc[sz], b, off + i * sz, v[i] if i < len(v) else v[-1])
    return bytes(b)


def w(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb" if isinstance(data, bytes) else "w") as f:
        f.write(data)


def amd_device(pci, port, did, rid, info, metrics, extra):
    dev = os.path.join(OUT, "sys/devices/pci0000:00", port, pci)
    for k, v in {"vendor": "0x1002", "device": "0x%04x" % did, "revision": "0x%02x" % rid, "class": "0x030000",
                 "subsystem_vendor": "0x1002", "subsystem_device": "0x0b36", "power/runtime_status": "active",
                 "srs-driver": "amdgpu"}.items():
        w(os.path.join(dev, k), v + "\n")
    w(os.path.join(port_dir(port), "vendor"), "0x1022\n")
    w(os.path.join(port_dir(port), "class"), "0x060400\n")
    w(os.path.join(port_dir(port), "max_link_speed"), "16.0 GT/s PCIe\n")
    w(os.path.join(port_dir(port), "max_link_width"), "16\n")
    for k, v in extra.items():
        w(os.path.join(dev, k), v)
    w(os.path.join(dev, "srs-amdgpu-info.txt"), info)
    if metrics:
        w(os.path.join(dev, "gpu_metrics"), metrics)
    link = os.path.join(OUT, "sys/bus/pci/devices", pci)
    os.makedirs(os.path.dirname(link), exist_ok=True)
    os.symlink(os.path.join("../../../../sys/devices/pci0000:00", port, pci), link)
    return dev


def port_dir(port):
    return os.path.join(OUT, "sys/devices/pci0000:00", port)


def info_text(family, ext_rev, did, rid, cu, se, sa, rb, eng_khz, mem_khz, vram_type, width, tcc, apu, vram, gtt, fw_smc):
    flags = 1 if apu else 0
    t = ["# made up for tests"]
    for k, v in [("device_id", did), ("chip_rev", 0), ("external_rev", ext_rev), ("pci_rev", rid), ("family", family),
                 ("num_shader_engines", se), ("num_shader_arrays_per_engine", sa), ("max_engine_clock", eng_khz),
                 ("max_memory_clock", mem_khz), ("cu_active_number", cu), ("num_rb_pipes", rb), ("ids_flags", flags),
                 ("vram_type", vram_type), ("vram_bit_width", width), ("num_tcc_blocks", tcc), ("tcc_disabled_mask", 0),
                 ("tcp_cache_size", 16), ("gl1c_cache_size", 128), ("gl2c_cache_size", 4096), ("mall_size", 0)]:
        t.append("dev.%s %d" % (k, v))
    t.append("mem.vram %d %d %d %d" % (vram, vram - (64 << 20), 512 << 20, vram * 3 // 4))
    t.append("mem.vis %d %d %d %d" % (256 << 20 if not apu else vram, (256 << 20) - (16 << 20), 128 << 20, 200 << 20))
    t.append("mem.gtt %d %d %d %d" % (gtt, gtt - (32 << 20), 256 << 20, gtt * 3 // 4))
    t += ["hwip 0 1 10 3 1", "hwip 1 1 10 3 15", "hwip 2 2 5 2 3", "hwip 6 1 3 0 1", "hwip 7 1 3 0 3", "hwip 8 1 3 0 1"]
    t += ["fw 4 0 0 120 40", "fw 10 0 0 %d 0" % fw_smc]
    t += ["vcap 0 3 1 4096 4096", "vcap 0 4 1 8192 4352", "vcap 1 3 1 4096 2304", "vcap 1 4 1 4096 2304"]
    t += ["vbios.name MADE UP TEST VBIOS", "vbios.pn 113-TEST-000", "vbios.ver 000.000.000.000.000000", "vbios.date 2026/10/31 00:00"]
    t.append("drm 3 64 0")
    return "\n".join(t) + "\n"


def ip_disc(dev, hwid, ver):
    d = os.path.join(dev, "ip_discovery/die/0/%d/0" % hwid)
    for k, v in zip(("major", "minor", "revision"), ver):
        w(os.path.join(d, k), "%d\n" % v)
    for k, v in (("hw_id", hwid), ("num_instance", 0), ("harvest", "0x0"), ("num_base_addresses", 1)):
        w(os.path.join(d, k), "%s\n" % v)
    w(os.path.join(d, "base_addr"), "0x00012000\n")


def pptable_11_0_7():
    """a made-up smu_11_0_7_powerplay_table (packed; only the parts GPU Top decodes)"""
    b = bytearray(800)
    struct.pack_into("<HBB", b, 0, 800, 15, 0)            # header: size, format 15 (Navi 21), content 0
    b[4] = 7                                               # table_revision
    struct.pack_into("<HIIHIB", b, 5, 800, 12345, 2, 8, 0x1F, 19)
    struct.pack_into("<HHHH", b, 22, 203, 203, 255, 118)  # small_power_limit1/2, boost, shutdown temp
    b[46] = 1
    struct.pack_into("<I", b, 50, 2)                       # power_saving_clock count
    struct.pack_into("<II", b, 54, 2105, 1000)             # max[0..1]
    struct.pack_into("<II", b, 118, 500, 97)               # min[0..1]
    od = 182
    b[od] = 1
    struct.pack_into("<II", b, od + 4, 2, 9)               # feature_count, setting_count
    b[od + 12] = 1; b[od + 13] = 1
    maxv = [2800, 2700, 0, 0, 0, 0, 1075, 1150, 15]
    minv = [500, 500, 0, 0, 0, 0, 674, 674, 0]
    for i in range(9):
        struct.pack_into("<I", b, 226 + 4 * i, maxv[i])
        struct.pack_into("<I", b, 482 + 4 * i, minv[i])
    return bytes(b)


def hwmon(dev, edge, power_w, cap_w):
    h = os.path.join(dev, "hwmon/hwmon7")
    w(os.path.join(h, "name"), "amdgpu\n")
    w(os.path.join(h, "temp1_label"), "edge\n")
    w(os.path.join(h, "temp1_input"), "%d\n" % (edge * 1000))
    w(os.path.join(h, "temp1_crit"), "100000\n")
    w(os.path.join(h, "power1_average"), "%d\n" % (power_w * 1000000))
    if cap_w:
        w(os.path.join(h, "power1_cap"), "%d\n" % (cap_w * 1000000))
        w(os.path.join(h, "power1_cap_min"), "%d\n" % (cap_w * 500000))
        w(os.path.join(h, "power1_cap_max"), "%d\n" % (cap_w * 1000000))


def main():
    if os.path.exists(OUT):
        shutil.rmtree(OUT)
    shutil.copytree(os.path.join(HERE, "rx9070xt"), OUT, symlinks=True)

    # Navi 21: dGPU, gpu_metrics v1.3, pp_table 11.0.7
    m = metrics_blob(1, 3, {"temperature_edge": 61, "temperature_hotspot": 75, "temperature_mem": 70,
                            "average_gfx_activity": 88, "average_umc_activity": 35, "average_mm_activity": 0,
                            "average_socket_power": 231, "average_gfxclk_frequency": 2250, "current_gfxclk": 2255,
                            "average_uclk_frequency": 1000, "current_uclk": 1000, "voltage_gfx": 1093,
                            "current_fan_speed": 1650, "throttle_status": 0,
                            "indep_throttle_status": (1 << 0) | (1 << 36) | (1 << 16),   # PPT0, TEMP_HOTSPOT (dropped < 90 C), TDC_GFX
                            "system_clock_counter": 123456789})
    dev = amd_device("0000:0a:00.0", "0000:00:03.1", 0x73bf, 0xc3,
                     info_text(143, 0x28, 0x73bf, 0xc3, 60, 4, 2, 16, 2105000, 1000000, 9, 256, 16, False, 16 << 30, 16 << 30, 0x3a5300),
                     m, {"pp_dpm_sclk": "0: 500Mhz \n1: 2250Mhz *\n2: 2475Mhz \n", "pp_dpm_mclk": "0: 96Mhz \n1: 1000Mhz *\n",
                         "pp_dpm_pcie": "0: 2.5GT/s, x1 310Mhz \n1: 16.0GT/s, x16 619Mhz *\n",
                         "mem_info_vram_used": "%d\n" % (3 << 30), "mem_info_gtt_used": "%d\n" % (300 << 20),
                         "pp_table": pptable_11_0_7(), "pp_od_clk_voltage": "OD_RANGE:\nSCLK:     500Mhz       2800Mhz\nMCLK:     674Mhz       1150Mhz\n"})
    ip_disc(dev, 1, (11, 0, 7))
    ip_disc(dev, 11, (10, 3, 0))
    hwmon(dev, 61, 231, 255)
    os.makedirs(os.path.join(dev, "drm/card2")); os.makedirs(os.path.join(dev, "drm/renderD130"))
    w(os.path.join(dev, "srs-grbm.txt"), "".join("%08x %08x\n" % ((1 << 31) | (1 << 14) if i % 2 else 0, 1 << 30 if i < 25 else 0)
                                                for i in range(100)))

    # Rembrandt APU: gpu_metrics v2.2 (activity in 0.01 %)
    m = metrics_blob(2, 2, {"temperature_gfx": 5200, "temperature_soc": 5400, "temperature_core": [6100, 6200, 6000, 5900, 6300, 6400, 6000, 6100],
                            "temperature_l3": [5500, 5600], "average_gfx_activity": 4200, "average_mm_activity": 700,
                            "average_socket_power": 28, "average_cpu_power": 15, "average_soc_power": 6, "average_gfx_power": 7,
                            "average_core_power": [2, 2, 2, 2, 1, 1, 1, 1], "average_gfxclk_frequency": 1800,
                            "current_coreclk": [4500, 4400, 4300, 4200, 4100, 4000, 3900, 3800], "current_l3clk": [4000, 4000],
                            "throttle_status": 0, "indep_throttle_status": 1 << 5, "fan_pwm": 0})
    dev = amd_device("0000:0c:00.0", "0000:00:08.1", 0x1681, 0xc1,
                     info_text(146, 0x01, 0x1681, 0xc1, 12, 1, 2, 4, 2400000, 3200000, 12, 128, 4, True, 2 << 30, 16 << 30, 0x450000),
                     m, {"pp_dpm_sclk": "0: 200Mhz \n1: 1800Mhz *\n2: 2400Mhz \n"})
    hwmon(dev, 52, 28, 0)
    os.makedirs(os.path.join(dev, "drm/card3")); os.makedirs(os.path.join(dev, "drm/renderD131"))

    # Strix Point APU: gpu_metrics v3.0 (mW, NPU fields)
    m = metrics_blob(3, 0, {"temperature_gfx": 4800, "temperature_soc": 5000, "temperature_core": [5500] * 16,
                            "temperature_skin": 3900, "average_gfx_activity": 1500, "average_vcn_activity": 200,
                            "average_ipu_activity": [10, 20, 30, 40, 0, 0, 0, 0], "average_core_c0_activity": [50] * 16,
                            "average_dram_reads": 2500, "average_dram_writes": 1200, "average_ipu_reads": 300, "average_ipu_writes": 100,
                            "system_clock_counter": 42, "average_socket_power": 21000, "average_ipu_power": 900,
                            "average_apu_power": 19000, "average_gfx_power": 4000, "average_dgpu_power": 0, "average_all_core_power": 9000,
                            "average_core_power": [600] * 16, "average_sys_power": 22000, "stapm_power_limit": 28000,
                            "current_stapm_power_limit": 28000, "average_gfxclk_frequency": 1200, "average_ipuclk_frequency": 1800,
                            "average_mpipu_frequency": 1000, "current_coreclk": [4000] * 16, "current_core_maxfreq": 5100,
                            "current_gfx_maxfreq": 2900})
    dev = amd_device("0000:0e:00.0", "0000:00:08.2", 0x150e, 0xc1,
                     info_text(150, 0x01, 0x150e, 0xc1, 16, 1, 2, 4, 2900000, 3750000, 12, 128, 4, True, 1 << 30, 16 << 30, 0x5d4500),
                     m, {})
    os.makedirs(os.path.join(dev, "drm/card4")); os.makedirs(os.path.join(dev, "drm/renderD132"))

    # NVIDIA (data from the NVML stub)
    nv = os.path.join(OUT, "sys/devices/pci0000:00/0000:00:01.2/0000:01:00.0")
    for k, v in {"vendor": "0x10de", "device": "0x2684", "revision": "0xa1", "class": "0x030000",
                 "subsystem_vendor": "0x10de", "subsystem_device": "0x167c", "power/runtime_status": "active",
                 "srs-driver": "nvidia"}.items():
        w(os.path.join(nv, k), v + "\n")
    os.makedirs(os.path.join(nv, "drm/card1")); os.makedirs(os.path.join(nv, "drm/renderD129"))
    os.symlink("../../../../sys/devices/pci0000:00/0000:00:01.2/0000:01:00.0", os.path.join(OUT, "sys/bus/pci/devices/0000:01:00.0"))

    # a process that uses the Navi 21 (render node renderD130), with a second fdinfo sample: 30 % GFX, 10 % compute
    p = os.path.join(OUT, "proc/1004")
    w(os.path.join(p, "comm"), "glxgears-test\n")
    w(os.path.join(p, "cmdline"), "glxgears-test\0")
    w(os.path.join(p, "stat"), "1004 (glxgears-test) S 1 1 1 0 -1 0 0 0 0 0 100 50 0 0 20 0 1 0 0 0 0\n")
    base = ("drm-driver:\tamdgpu\ndrm-client-id:\t900\ndrm-pdev:\t0000:0a:00.0\ndrm-memory-vram:\t%d KiB\ndrm-memory-gtt: \t2048 KiB\n"
            "drm-memory-cpu: \t0 KiB\ndrm-engine-gfx:\t%d ns\ndrm-engine-compute:\t%d ns\n")
    w(os.path.join(p, "fdinfo/5"), base % (1048576, 5000000000, 1000000000))
    w(os.path.join(p, "fdinfo/5.next"), base % (1048576, 5300000000, 1100000000))
    os.makedirs(os.path.join(p, "fd"))
    os.symlink("/dev/dri/renderD130", os.path.join(p, "fd/5"))
    # the RX 9070 XT's first process works too (+25 % GFX in the next sample)
    q = os.path.join(OUT, "proc/1001/fdinfo")
    f = [x for x in os.listdir(q) if not x.endswith(".next")][0]
    t = open(os.path.join(q, f)).read()
    g = int(re.search(r"drm-engine-gfx:\t(\d+) ns", t).group(1))
    w(os.path.join(q, f + ".next"), t.replace("drm-engine-gfx:\t%d ns" % g, "drm-engine-gfx:\t%d ns" % (g + 250000000)))
    print("made", OUT)


if __name__ == "__main__":
    sys.exit(main())
