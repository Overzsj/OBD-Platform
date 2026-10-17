"""Parameter editor, separate from firmware and theme management."""
from pathlib import Path
import argparse,concurrent.futures
import easy_settings as api
def gui(selected_port=''):
 import tkinter as tk
 from tkinter import ttk,filedialog
 from serial.tools import list_ports
 root=tk.Tk();root.title('设备参数');root.geometry('780x650');pool=concurrent.futures.ThreadPoolExecutor(max_workers=1)
 busy=False;device=None;last=None;variables={};ranges={};sounds={};buttons=[]
 port=tk.StringVar(value=selected_port);status=tk.StringVar(value='先读取设备参数。没有点击保存不会修改设备。')
 top=ttk.Frame(root,padding=15);top.pack(fill='x');ttk.Label(top,text='USB 设备').pack(side='left');ports=ttk.Combobox(top,textvariable=port,state='readonly',width=12);ports.pack(side='left',padx=8)
 found=[p.device for p in list_ports.comports()];ports['values']=found
 if port.get() not in found:port.set(found[0] if len(found)==1 else '')
 tabs=ttk.Notebook(root);tabs.pack(fill='both',expand=True,padx=15,pady=10);alarms=ttk.Frame(tabs,padding=15);display=ttk.Frame(tabs,padding=15);tabs.add(alarms,text='报警与声音');tabs.add(display,text='显示量程')
 ttk.Label(root,textvariable=status,wraplength=740,padding=15).pack(fill='x')
 def enabled():
  ports.configure(state='disabled' if busy else 'readonly')
  for button in buttons:button.configure(state='normal' if device is not None and not busy else 'disabled')
 def fill(data):
  nonlocal device
  device=data
  for k,v in variables.items():v.set(str(data['GET'][k]))
  for k,v in ranges.items():v.set(str(data['DISPLAY GET']['maximum'][api.core.SIGNALS.index(k)]))
  if 'OPTIONS GET' in data:
   sounds['rpm_sound'].set(bool(data['OPTIONS GET']['sound_mask']&2));sounds['coolant_sound'].set(bool(data['OPTIONS GET']['sound_mask']&4))
 def submit(fn,callback):
  nonlocal busy
  if busy:return
  if not port.get():status.set('请先选择 USB 设备。');return
  busy=True;enabled();status.set('正在处理，请保持 USB 连接。');future=pool.submit(fn,port.get())
  def poll():
   nonlocal busy
   if not future.done():root.after(100,poll);return
   busy=False
   try:callback(future.result())
   except Exception as exc:status.set(api.friendly(exc))
   enabled()
  root.after(100,poll)
 def read():
  def fetch(port):
   data=api.read_device(port);data['OPTIONS GET']=api.wire(port,'OPTIONS GET');return data
  def received(data):fill(data);status.set('已读取当前设置。报警与量程分别保存，保存前自动备份。')
  submit(fetch,received)
 ttk.Button(top,text='读取当前参数',command=read).pack(side='left')
 def changed(_):
  nonlocal device,last
  device=None;last=None;enabled();status.set('设备已切换，请重新读取参数。')
 ports.bind('<<ComboboxSelected>>',changed)
 labels={'rpm_warn':'高转提示（转/分）','rpm_crit':'红线提示（转/分）','temp_warn':'水温提示（℃）','temp_crit':'水温严重报警（℃）'}
 for i,(key,label) in enumerate(labels.items()):
  variables[key]=tk.StringVar();ttk.Label(alarms,text=label).grid(row=i,column=0,sticky='w',pady=10);ttk.Entry(alarms,textvariable=variables[key],width=13).grid(row=i,column=1,padx=20)
 for i,(key,label) in enumerate([('rpm_sound','转速报警声'),('coolant_sound','水温报警声')],4):sounds[key]=tk.BooleanVar();ttk.Checkbutton(alarms,text=label,variable=sounds[key]).grid(row=i,column=0,sticky='w',pady=6)
 ttk.Label(alarms,text='只修改仪表提示，不改变 ECU 断油点。已有总静音仍然生效。',wraplength=680).grid(row=6,column=0,columnspan=3,sticky='w',pady=10)
 for i,key in enumerate(api.core.SIGNALS):
  ranges[key]=tk.StringVar();ttk.Label(display,text=api.RANGE_LABELS[key]+' 上限').grid(row=i,column=0,sticky='w',pady=4);ttk.Entry(display,textvariable=ranges[key],width=13).grid(row=i,column=1,padx=20)
 ttk.Label(display,text='上限越大，条越矮。只改变显示比例，不改变实际读数或报警。',wraplength=680).grid(row=11,column=0,columnspan=3,sticky='w',pady=10)
 def saved(result):
  nonlocal last
  last=result['backup'];fill(result['device']);status.set('已保存。备份保留在“备份/参数”中。')
 def save(kind):
  try:
   values={k:int(v.get()) for k,v in (variables if kind=='alarms' else ranges).items()}
   if kind=='alarms':values.update({k:v.get() for k,v in sounds.items()})
  except ValueError:status.set('请填写整数，不要留空或填写文字。');return
  submit(lambda port:api.save_choice(port,kind,values),saved)
 for parent,label,kind,row in [(alarms,'保存报警与声音','alarms',7),(display,'保存显示量程','ranges',12)]:
  b=ttk.Button(parent,text=label,command=lambda k=kind:save(k));b.grid(row=row,column=0,sticky='w',pady=10);buttons.append(b)
 def restore():
  path=filedialog.askopenfilename(initialdir=Path(__file__).resolve().parent.parent/'备份/参数',title='选择参数备份',filetypes=[('参数备份','*.json')])
  if path:
   def done(data):fill(data);status.set('参数已恢复。')
   submit(lambda port:api.undo_choice(port,path),done)
 b=ttk.Button(top,text='恢复参数备份…',command=restore);b.pack(side='left',padx=8);buttons.append(b)
 def close():
  if busy:status.set('请等当前操作完成再关闭。');return
  pool.shutdown(wait=False);root.destroy()
 root.protocol('WM_DELETE_WINDOW',close);enabled();root.after(200,read);root.mainloop()
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--port',default='');gui(p.parse_args().port)
