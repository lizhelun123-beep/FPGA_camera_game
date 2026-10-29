"""Portable application-only build using the shipped version14 frozen platform.

No Vivado, hardware connection, reset, download or SD write.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / 'color_test/software/vision_ps_baremetal'
FROZEN = ROOT / 'firmware/version14'
NAMES = ('game_beam ray_game game_scene_adapter vision_stabilizer vision_ps '
         'green_target_detector vision_overlay game_linux_capture game_linux_display '
         'ov5640 beam_pose_protocol beam_control_protocol beam_control_state '
         'beam_uart_log beam_wire_protocol').split()


def module(name):
    spec = importlib.util.spec_from_file_location(name, APP / 'tools' / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def check_frozen():
    for row in (FROZEN / 'SHA256SUMS').read_text(encoding='utf-8').splitlines():
        expected, relative = row.split('  ', 1)
        target = (FROZEN / relative).resolve()
        if not target.is_relative_to(FROZEN.resolve()):
            raise ValueError('Invalid frozen manifest path')
        if hashlib.sha256(target.read_bytes()).hexdigest() != expected:
            raise ValueError('Frozen input changed: ' + relative)
    inputs = FROZEN / 'inputs'
    declared = json.loads((inputs / 'inputs.json').read_text(encoding='utf-8'))
    actual, bit = module('prepare_game_beam').inspect_xsa(
        inputs / 'hardware.xsa', allow_historical_live=True)
    if any(actual[key] != declared[key] for key in actual):
        raise ValueError('XSA identity differs from the frozen manifest')
    if bit != (inputs / 'system_wrapper.bit').read_bytes():
        raise ValueError('XSA/BIT mismatch')
    verify_image(FROZEN / 'output/beam_standalone.elf', FROZEN / 'output/BOOT.BIN')
    return inputs


def verify_image(elf, boot=None):
    verifier = module('verify_game_beam')
    identity = verifier.verify_elf(elf)
    if boot is not None:
        partitions, _ = module('package_live_boot').verify_boot(
            boot, FROZEN / 'inputs/fsbl.elf', FROZEN / 'inputs/system_wrapper.bit',
            elf, required_symbols=verifier.SYMBOLS)
        end = partitions[0]['load_address'] + partitions[0]['memory_bytes']
        if end > identity['load_address']:
            raise ValueError('FSBL/application memory overlap')
    return identity


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Offline identities and BOOT partitions only')
    parser.add_argument('--vitis-root', type=Path, help='Installed Vitis 2023.1 directory')
    parser.add_argument('--tag', default='local_board')
    parser.add_argument('--boot', action='store_true')
    parser.add_argument('--regenerate-config', action='store_true',
                        help='Generate beam C from the unique current JSON; default preserves frozen C')
    args = parser.parse_args()
    inputs = check_frozen()
    if args.check:
        print('PASS_FROZEN_VERSION14_XSA_BIT_BSP_ELF_BOOT NO_HARDWARE')
        return
    if args.vitis_root is None or not re.fullmatch(r'[A-Za-z0-9_-]+', args.tag):
        parser.error('Build requires --vitis-root and a simple fresh --tag')
    vitis = args.vitis_root.resolve()
    cc = vitis / 'gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe'
    bootgen = vitis / 'bin/bootgen.bat'
    if not cc.is_file() or (args.boot and not bootgen.is_file()):
        parser.error('Missing Vitis compiler or bootgen')
    work = ROOT / 'build/board' / args.tag
    if work.exists():
        parser.error('Output already exists; use another --tag')
    work.mkdir(parents=True)
    generated = work / 'generated'
    generated.mkdir()
    shutil.copy2(inputs / 'game_beam_config.h', generated / 'game_beam_config.h')
    config = generated / 'game_beam_config.c'
    if args.regenerate_config:
        data = json.loads((ROOT / 'pc_vision/game_config.json').read_text(encoding='utf-8'))
        config.write_text(module('generate_game_config').generate_beam(data), encoding='utf-8')
    else:
        shutil.copy2(inputs / 'game_beam_config.c', config)
    src, bsp = APP / 'src', inputs / 'bsp'
    flags = ['-std=gnu99', '-mcpu=cortex-a9', '-mfpu=vfpv3', '-mfloat-abi=hard',
             '-O2', '-g3', '-ffunction-sections', '-fdata-sections', '-DGAME_BEAM_STANDALONE=1',
             '-I' + str(bsp / 'include'), '-I' + str(src), '-I' + str(src / 'standalone'),
             '-I' + str(generated)]
    sources = [src / (name + '.c') for name in NAMES]
    sources += [src / 'i2c/PS_i2c.c', config, src / 'standalone/game_beam_standalone.c',
                src / 'standalone/game_baremetal_io.c']
    objects = []
    for index, source in enumerate(sources, 1):
        obj = work / ('app_' + str(index) + '.o')
        subprocess.run([str(cc), *flags, '-Wall', '-Wextra', '-Werror', '-c', str(source), '-o', str(obj)],
                       cwd=work, check=True)
        objects.append(str(obj))
    elf = work / 'beam_standalone.elf'
    subprocess.run([str(cc), *flags, '-L' + str(bsp / 'lib'), '-specs=' + str(src / 'Xilinx.spec'),
                    '-T' + str(src / 'lscript.ld'), '-Wl,--defsym=_HEAP_SIZE=0x08000000',
                    '-Wl,--defsym=_STACK_SIZE=0x00040000', '-Wl,--gc-sections',
                    '-Wl,-Map=' + str(work / 'beam_standalone.map'), *objects,
                    '-Wl,--start-group', '-lxil', '-lgcc', '-lc', '-lm', '-Wl,--end-group',
                    '-o', str(elf)], cwd=work, check=True)
    verify_image(elf)
    boot = None
    if args.boot:
        bif = work / 'beam_standalone.bif'
        paths = [(inputs / 'fsbl.elf').as_posix(), (inputs / 'system_wrapper.bit').as_posix(), elf.as_posix()]
        bif.write_text('the_ROM_image:\n{\n  [bootloader] "' + paths[0] + '"\n  "' +
                       paths[1] + '"\n  "' + paths[2] + '"\n}\n', encoding='utf-8')
        boot = work / 'BOOT.BIN'
        # Vitis batch setup supplies its own loader/runtime environment.
        command = subprocess.list2cmdline([str(bootgen), '-arch', 'zynq', '-image', str(bif), '-o', str(boot)])
        subprocess.run(['cmd.exe', '/d', '/s', '/c', '"' + command + '"'], cwd=work, check=True)
        verify_image(elf, boot)
    print('PASS_APPLICATION_BUILD', elf, 'NO_HARDWARE_NO_SD_WRITE')


if __name__ == '__main__':
    main()
