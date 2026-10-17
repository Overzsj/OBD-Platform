"""USB management protocol client.
Uses pyserial, already used by the bench. 
"""
import argparse
import concurrent.futures
import json
from pathlib import Path
import time
import serial
from serial.tools import list_ports

KEYS=('rpm_warn','rpm_crit','temp_warn','temp_crit','muted','boot_animation','sound_level')
DEFAULTS=(5000,6000,100,108,0,1,1)
PAGE_COMMANDS={'dashboard':'VIEW MAIN','data':'VIEW DATA','diagnostics':'VIEW DIAG','next':'VIEW NEXT','previous':'VIEW PREV','view':'VIEW GET'}

def validate_config(data):
    if not isinstance(data,dict) or any(k not in data for k in KEYS):raise ValueError('配置字段不完整')
    v=[data[k] for k in KEYS]
    if any(type(x) is not int for x in v):raise ValueError('设置必须是整数')
    a,b,c,d,e,f,g=v
    if not (2000<=a and a+100<=b<=9999 and 80<=c and c+2<=d<=125 and e in (0,1) and f in (0,1) and 0<=g<=3):
        raise ValueError('阈值或开关超出允许范围')
    return dict(zip(KEYS,v))

def setting_command(data):
    c=validate_config(data)
    return 'SET '+' '.join(str(c[k]) for k in KEYS)

def parse_response(line,request_id):
    at=line.find('USB|')
    if at<0:return None
    try:r=json.loads(line[at+4:])
    except (ValueError,TypeError):return None
    if not isinstance(r,dict) or r.get('id')!=request_id:return None
    if not r.get('ok'):raise RuntimeError(r.get('message','设备拒绝请求'))
    return r

def request(port,command,timeout=20,serial_factory=serial.Serial):
    if '\n' in command or '\r' in command:raise ValueError('命令不能包含换行')
    request_id=time.monotonic_ns()%1000000000
    wire=f'Q {request_id} {command}\r\n'.encode('ascii')
    if len(wire)>383:raise ValueError('命令太长')
    s=serial_factory();s.port=port;s.baudrate=115200;s.timeout=.2;s.write_timeout=2;s.dtr=False;s.rts=False
    try:
        s.open();s.reset_input_buffer();s.write(wire)
        end=time.monotonic()+timeout;pending=bytearray()
        while time.monotonic()<end:
            pending.extend(s.read(4096))
            while b'\n' in pending:
                line,_,rest=pending.partition(b'\n');pending=bytearray(rest)
                r=parse_response(line.decode('utf-8',errors='replace').strip(),request_id)
                if r is not None:return r
            if len(pending)>16384:pending.clear()
        raise TimeoutError('未收到设备管理协议回复；请确认固件版本、串口占用及供电。')
    finally:
        s.close()

