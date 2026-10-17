"""Run actual firmware policy/client code with a deterministic host clock and IO.
Usage: python tests/run_host.py [--compiler PATH_TO_ZIG]
"""
import argparse
import pathlib
import subprocess
import shutil
import sys

root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--compiler',default=shutil.which('g++') or shutil.which('clang++') or shutil.which('zig'))
args=p.parse_args()
if not args.compiler:p.error('Install g++/clang++ or pass --compiler /path/to/zig')
out=root/'tests/artifacts'
out.mkdir(exist_ok=True)
for mode in ('vehicle','bench','round2','data_lab','pages_sim','pages_vehicle','shift_lights','adapters','boot','journal','character','rpm_curve','reconnect','support_probe','buzzer','multiframe','can_query','sim_main_metrics','bar_motion','auto_discovery','runtime_theme','display_settings'):
    suite='pages' if mode.startswith('pages_') else mode if mode in ('round2','data_lab','shift_lights','adapters','boot','journal','character','rpm_curve','reconnect','support_probe','buzzer','multiframe','can_query','sim_main_metrics','bar_motion','auto_discovery','runtime_theme','display_settings') else 'round1'
    exe=out/f'{suite}-{mode}.exe'
    cmd=[args.compiler]+(['c++'] if 'zig' in pathlib.Path(args.compiler).name else [])+['-std=c++11','-fno-exceptions','-fno-rtti',
         '-I',str(root/'tests/host'),'-I',str(root/'src'),
         str(root/f'tests/host/{suite}.cpp'),'-o',str(exe)]
    if mode in ('auto_discovery','display_settings'):cmd+=['-DOBD_AUTO_ADAPT=1']
    if mode=='bench':cmd+=['-DOBD_BT_PC_SIM=1']
    if mode in ('data_lab','sim_main_metrics','bar_motion','runtime_theme'):cmd+=['-DOBD_DATA_TEST=1','-DOBD_TRANSPORT=1']
    if mode=='pages_sim':cmd+=['-DOBD_DATA_TEST=1','-DOBD_TRANSPORT=1']
    if mode.startswith('pages_') or mode=='shift_lights':cmd+=['-DTFT_CHARACTER_ANIMATION=0']
    subprocess.run(cmd,check=True)
    extra=[]
    if mode=='runtime_theme':
        image=out/'default.theme.bin'
        subprocess.run([sys.executable,str(root/'tools/runtime_theme.py'),str(root/'themes/open-instrument-1.0.0.sukitheme'),'--output',str(image)],check=True)
        extra=[str(image)]
    result=subprocess.run([str(exe)]+extra,text=True,encoding='utf-8',capture_output=True,cwd=root)
    (out/f'{mode}.log').write_text(result.stdout,encoding='utf-8')
    print(result.stdout,end='')
    result.check_returncode()
