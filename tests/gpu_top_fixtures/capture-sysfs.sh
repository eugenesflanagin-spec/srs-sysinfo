#!/bin/sh
# capture-sysfs.sh PCI OUTDIR - copy the read-only sysfs files GPU Top uses for one GPU into a fixture tree
# (OUTDIR/sys/..., OUTDIR/usr/share/libdrm/amdgpu.ids). Reads files only; never writes to sysfs and never
# opens the GPU's device nodes. Process data (proc/) and the ioctl answers are added separately
# (sysreport-search gpu-top --capture-info). Run as your own user; review the result before committing it.
set -eu
pci=$1; out=$2
real=$(readlink -f /sys/bus/pci/devices/$pci)
cp_files() {   # src_dir dst_dir file...
    s=$1; d=$2; shift 2
    mkdir -p "$d"
    for f in "$@"; do [ -f "$s/$f" ] && [ -r "$s/$f" ] && cat "$s/$f" > "$d/$f" 2>/dev/null || rm -f "$d/$f"; done
    return 0
}
dev="$out$real"
cp_files "$real" "$dev" vendor device revision subsystem_vendor subsystem_device class current_link_speed current_link_width \
    max_link_speed max_link_width gpu_metrics pp_dpm_sclk pp_dpm_mclk pp_dpm_fclk pp_dpm_socclk pp_dpm_pcie pp_dpm_dcefclk \
    pp_power_profile_mode pp_od_clk_voltage pp_table mem_info_vram_vendor mem_info_vram_total mem_info_vram_used \
    mem_info_vis_vram_total mem_info_vis_vram_used mem_info_gtt_total mem_info_gtt_used gpu_busy_percent mem_busy_percent \
    vcn_busy_percent power_dpm_force_performance_level vbios_version power_state boot_vga
cp_files "$real/power" "$dev/power" runtime_status control
[ -d "$real/ras" ] && cp_files "$real/ras" "$dev/ras" umc_err_count
for h in "$real"/hwmon/hwmon*; do
    [ -d "$h" ] || continue
    files=$(cd "$h" && ls | grep -E '^(name|temp[0-9]+_(input|label|crit|emergency)|power[0-9]_(average|input|cap|cap_min|cap_max|cap_default|label)|fan[0-9]_(input|max|min)|pwm[0-9](_max|_min)?|in[0-9]+_(input|label)|freq[0-9]+_(input|label))$' || true)
    cp_files "$h" "$dev/hwmon/$(basename "$h")" $files
done
for n in "$real"/drm/card* "$real"/drm/renderD*; do [ -d "$n" ] && mkdir -p "$dev/drm/$(basename "$n")"; done
if [ -d "$real/ip_discovery/die" ]; then
    for die in "$real"/ip_discovery/die/*; do
        for ip in "$die"/[0-9]*; do
            for inst in "$ip"/*; do
                [ -d "$inst" ] || continue
                cp_files "$inst" "$dev/ip_discovery/die/$(basename "$die")/$(basename "$ip")/$(basename "$inst")" \
                    hw_id num_instance major minor revision harvest num_base_addresses base_addr
            done
        done
    done
fi
# the PCIe ports above the GPU (amdgpu_top reads their link and power state)
p=$(dirname "$real"); i=0
while [ $i -lt 3 ] && [ -f "$p/vendor" ]; do
    cp_files "$p" "$out$p" vendor device class current_link_speed current_link_width max_link_speed max_link_width power_state
    p=$(dirname "$p"); i=$((i+1))
done
mkdir -p "$out/sys/bus/pci/devices"
ln -sfn "../../../..$real" "$out/sys/bus/pci/devices/$pci"
if [ -d /sys/class/kfd/kfd/topology/nodes ]; then
    for n in /sys/class/kfd/kfd/topology/nodes/*; do cp_files "$n" "$out$n" properties name; done
    # the GPU serial number (unique_id) stays out of fixtures that may be published
    for f in "$out"/sys/class/kfd/kfd/topology/nodes/*/properties; do [ -f "$f" ] && sed -i 's/^unique_id .*/unique_id 0/' "$f"; done
fi
[ -r /sys/module/amdgpu/parameters/ppfeaturemask ] && mkdir -p "$out/sys/module/amdgpu/parameters" && cat /sys/module/amdgpu/parameters/ppfeaturemask > "$out/sys/module/amdgpu/parameters/ppfeaturemask"
[ -f /usr/share/libdrm/amdgpu.ids ] && mkdir -p "$out/usr/share/libdrm" && cp /usr/share/libdrm/amdgpu.ids "$out/usr/share/libdrm/"
echo "captured $pci into $out"
