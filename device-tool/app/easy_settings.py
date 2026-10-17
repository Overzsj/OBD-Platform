"""Parameter API adapter for the current management protocol."""
from pathlib import Path
import copy,json,sys,subprocess,concurrent.futures
from datetime import datetime
import platform_configurator as core

BASE=Path(__file__).resolve().parent
LABELS={'iat':'进气温度','coolant':'水温','maf':'空气流量','load':'发动机负荷','throttle':'节气门','rpm':'转速','speed':'车速','voltage':'电压','oil_temp':'油温','oil_pressure':'油压','afr':'实际空燃比'}
RANGE_LABELS={'iat':'进气温度（℃）','coolant':'水温（℃）','maf':'空气流量（克/秒）','load':'发动机负荷（%）','throttle':'节气门（%）','rpm':'转速灯条（转/分）','speed':'车速（公里/小时）','voltage':'电压（伏）','oil_temp':'油温（℃）','oil_pressure':'油压（bar）','afr':'实际空燃比'}
ALARMS=['rpm_warn','rpm_crit','temp_warn','temp_crit']
def wire(port,command):
 from usb_console import request
 return request(port,command)
def read_device(port,request_fn=None):
 request=request_fn or wire
 status=request(port,'STATUS')
 if 'universal-v30' not in status.get('build',''):raise ValueError('这块板子需要 v30 通用固件。请打开高级设置完成安装，再返回这里。')
 data={'STATUS':status}
 for command in ['GET','DISPLAY GET','HARDWARE GET','THEME GET']:data[command]=request(port,command)
 profile_from(data)
 return data
def profile_from(data):
 if not isinstance(data['DISPLAY GET']['maximum'],list) or len(data['DISPLAY GET']['maximum'])!=len(core.SIGNALS):raise ValueError('仪表量程数据不完整，请重新连接。')
 p=core.defaults();p['hardware']={k:data['HARDWARE GET'][k] for k in core.HARDWARE}
 p['vehicle']['name']='当前设备';p['vehicle']['cutoff_reference']=0
 for k in ALARMS:p['vehicle'][k]=data['GET'][k]
 p['display']['maximum']=dict(zip(core.SIGNALS,data['DISPLAY GET']['maximum']));p['theme']['slots']=data['THEME GET']['slots']
 return core.validate(p)
def save_choice(port,category,values,request_fn=None):
 request=request_fn or wire;before=read_device(port,request);p=profile_from(before)
 if category=='alarms':
  if set(values)!=set(ALARMS+['rpm_sound','coolant_sound']):raise ValueError('报警设置不完整，请重新读取设备。')
  for k in ALARMS:
   if type(values[k]) is not int:raise ValueError('请填写整数。')
   p['vehicle'][k]=values[k]
  if any(type(values[k]) is not bool for k in ['rpm_sound','coolant_sound']):raise ValueError('声音开关无效')
  options=request(port,'OPTIONS GET');before['OPTIONS GET']=options
  mask=(options['sound_mask']&1)|(2 if values['rpm_sound'] else 0)|(4 if values['coolant_sound'] else 0)
  keys=['rpm_warn','rpm_crit','temp_warn','temp_crit','muted','boot_animation','sound_level']
  wanted={**before['GET'],**{k:values[k] for k in ALARMS}}
  commands=['SET '+' '.join(str(wanted[k]) for k in keys),f"OPTIONS SET {options['layout']} {mask}"]
  rollback=['SET '+' '.join(str(before['GET'][k]) for k in keys),f"OPTIONS SET {options['layout']} {options['sound_mask']}"]
 elif category=='ranges':
  if not isinstance(values,dict) or not values or any(k not in core.SIGNALS for k in values):raise ValueError('请选择要修改的显示项目。')
  p['display']['maximum'].update(values)
  commands=['DISPLAY SET '+' '.join(str(p['display']['maximum'][k]) for k in core.SIGNALS)]
  rollback=['DISPLAY SET '+' '.join(str(x) for x in before['DISPLAY GET']['maximum'])]
 else:raise ValueError('未知操作')
 core.validate(p) # No write or backup folder before all inputs pass validation.
 backup=BASE.parent/'备份'/'参数'/datetime.now().strftime('%Y%m%d-%H%M%S-%f');backup.mkdir(parents=True)
 (backup/'before.json').write_text(json.dumps(before,ensure_ascii=False,indent=2),encoding='utf-8')
 try:
  for command in commands:request(port,command)
  after=read_device(port,request)
  if category=='ranges' and after['DISPLAY GET']['maximum']!=[p['display']['maximum'][k] for k in core.SIGNALS]:raise RuntimeError('设备读回的量程与设置不一致')
  if category=='alarms':
   if any(after['GET'][k]!=values[k] for k in ALARMS):raise RuntimeError('设备读回的报警与设置不一致')
   after['OPTIONS GET']=request(port,'OPTIONS GET')
   if after['OPTIONS GET']['sound_mask']!=mask:raise RuntimeError('设备读回的声音开关与设置不一致')
 except Exception as error:
  try:
   for command in rollback:request(port,command)
  except Exception as restore_error:raise RuntimeError('保存失败，自动恢复也未完成。请保留备份：'+str(backup/'before.json')) from restore_error
  raise RuntimeError('保存失败，已发送原设置恢复请求。请重新连接确认。原因：'+str(error)) from error
 return {'device':after,'backup':str(backup/'before.json'),'category':category}
def undo_choice(port,path,request_fn=None):
 request=request_fn or wire;read_device(port,request)
 old=json.loads(Path(path).read_text(encoding='utf-8'));p=profile_from(old)
 keys=['rpm_warn','rpm_crit','temp_warn','temp_crit','muted','boot_animation','sound_level']
 from usb_console import validate_config
 config=validate_config(old['GET'])
 if 'OPTIONS GET' in old:
  o=old['OPTIONS GET']
  if type(o.get('layout')) is not int or o['layout'] not in (0,1) or type(o.get('sound_mask')) is not int or not 0<=o['sound_mask']<=7:raise ValueError('备份中的声音设置无效')
 request(port,'SET '+' '.join(str(config[k]) for k in keys));request(port,'DISPLAY SET '+' '.join(str(x) for x in old['DISPLAY GET']['maximum']))
 if 'OPTIONS GET' in old:
  o=old['OPTIONS GET']
  request(port,f"OPTIONS SET {o['layout']} {o['sound_mask']}")
 after=read_device(port,request)
 after['OPTIONS GET']=request(port,'OPTIONS GET')
 if any(after['GET'][k]!=old['GET'][k] for k in keys) or after['DISPLAY GET']['maximum']!=old['DISPLAY GET']['maximum']:raise RuntimeError('恢复后读回不一致，请检查设备连接')
 if 'OPTIONS GET' in old and (after['OPTIONS GET']['sound_mask']!=old['OPTIONS GET']['sound_mask'] or after['OPTIONS GET']['layout']!=old['OPTIONS GET']['layout']):raise RuntimeError('声音设置恢复后读回不一致')
 return after
def friendly(error):
 text=str(error)
 if isinstance(error,KeyError):return '仪表返回的设置不完整，请重新连接。仍失败时可到高级设置检查。'
 if 'could not open port' in text or 'PermissionError' in text or 'Access is denied' in text or '拒绝访问' in text:return 'USB 正被其他工具占用。关闭其他设置窗口或串口工具，再点“重新连接”。'
 if isinstance(error,TimeoutError) or '未收到' in text:return '暂时没收到仪表回复。检查 USB 数据线，等开机动画结束后点“重新连接”。'
 return text

