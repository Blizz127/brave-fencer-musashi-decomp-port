from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def test_gpu_copy_controller(tmp_path):
    exe=tmp_path/'copy'
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Wconversion','-Wsign-conversion','-Werror','-I',str(ROOT/'pc_port/include'),str(ROOT/'tests/gpu_copy_controller_probe.c'),str(ROOT/'pc_port/gpu_controller.c'),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],capture_output=True,text=True)
    assert result.returncode==0,result.stdout+result.stderr
    assert 'GPU_COPY_CONTROLLER_PASS' in result.stdout
