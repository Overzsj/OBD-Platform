"""Versioned hardware/vehicle/display profiles for the SUKI platform."""
from pathlib import Path
import copy,json,hashlib,shutil,subprocess,sys
from datetime import datetime
BASE=Path(__file__).resolve().parent
SIGNALS=['iat','coolant','maf','load','throttle','rpm','speed','voltage','oil_temp','oil_pressure','afr']
NAMES=['进气温度 C','水温 C','空气流量 g/s','发动机负荷 %','节气门 %','转速灯条 rpm','车速 km/h','ECU 电压 V','油温 C','油压 bar','实际空燃比']
LOW=[10,40,1,1,1,1000,10,1,40,1,1];HIGH=[255,255,655,100,100,9999,255,66,255,20,40]
MAXIMUM=[120,120,200,100,100,7000,240,16,150,10,25]
HARDWARE={'board':'esp32-classic-4mb','driver':'ILI9488','width':480,'height':320,'engine':'suki-layout-v1','rotation':1,'spi_hz':40000000,'pins':{'sclk':18,'mosi':23,'cs':27,'dc':16,'rst':17,'buzzer':25}}
SAFE_PINS={4,13,14,16,17,18,19,21,22,23,25,26,27,32,33}
def defaults():
 return {'schema':1,'hardware':copy.deepcopy(HARDWARE),'vehicle':{'name':'未命名车辆','cutoff_reference':0,'rpm_warn':5000,'rpm_crit':6000,'temp_warn':100,'temp_crit':108},'display':{'maximum':dict(zip(SIGNALS,MAXIMUM))},'theme':{'slots':SIGNALS[:5]}}
def validate(profile):
 p=copy.deepcopy(profile)
 if not isinstance(p,dict) or set(p)!={'schema','hardware','vehicle','display','theme'} or type(p['schema']) is not int or p['schema']!=1:raise ValueError('不支持的配置版本或字段')
 h=p['hardware']
 if not isinstance(h,dict) or set(h)!=set(HARDWARE):raise ValueError('硬件字段不完整')
 for k in ['board','driver','width','height','engine']:
  if type(h[k]) is not type(HARDWARE[k]) or h[k]!=HARDWARE[k]:raise ValueError('当前仅支持经典 ESP32 4MB / ILI9488 / 480×320；此屏幕需要新增驱动或布局适配')
 if type(h['rotation']) is not int or h['rotation'] not in [1,3]:raise ValueError('只支持横屏方向 1 或倒置横屏 3')
 if type(h['spi_hz']) is not int or h['spi_hz'] not in [10000000,20000000,40000000]:raise ValueError('SPI 频率只能选 10 / 20 / 40 MHz')
 pins=h['pins']
 if not isinstance(pins,dict) or set(pins)!=set(HARDWARE['pins']):raise ValueError('引脚字段不完整')
 if any(type(v) is not int or v not in SAFE_PINS for v in pins.values()):raise ValueError('引脚必须可输出，且避开串口、Flash、输入专用及启动配置引脚')
 if len(set(pins.values()))!=len(pins):raise ValueError('引脚重复占用')
 v=p['vehicle']
 if not isinstance(v,dict) or set(v)!=set(defaults()['vehicle']):raise ValueError('车辆字段不完整')
 if not isinstance(v['name'],str) or not 1<=len(v['name'])<=60 or any(ord(c)<32 for c in v['name']):raise ValueError('车型名称无效')
 if any(type(v[k]) is not int for k in v if k!='name'):raise ValueError('车辆参数必须为整数')
 if not 2000<=v['rpm_warn']<=v['rpm_crit']-100 or not v['rpm_crit']<=9999:raise ValueError('报警转速范围无效，红线提示至少高于高转提示 100 转')
 if not 80<=v['temp_warn']<=v['temp_crit']-2 or v['temp_crit']>125:raise ValueError('水温报警范围无效')
 if not 0<=v['cutoff_reference']<=16383:raise ValueError('断油参考范围无效；0 表示未知')
 if v['cutoff_reference'] and v['rpm_crit']>=v['cutoff_reference']:raise ValueError('红线提示应提前于填写的断油参考点；未知请填 0')
 d=p['display']
 if not isinstance(d,dict) or set(d)!={'maximum'} or not isinstance(d['maximum'],dict) or set(d['maximum'])!=set(SIGNALS):raise ValueError('显示量程字段不完整')
 for i,key in enumerate(SIGNALS):
  n=d['maximum'][key]
  if type(n) is not int or not LOW[i]<=n<=HIGH[i]:raise ValueError(f'{NAMES[i]} 量程必须为 {LOW[i]}–{HIGH[i]}')
 t=p['theme']
 if not isinstance(t,dict) or set(t)!={'slots'} or not isinstance(t['slots'],list) or len(t['slots'])!=5 or any(s not in SIGNALS for s in t['slots']):raise ValueError('主题必须绑定五个已支持的数据名称')
 return p
