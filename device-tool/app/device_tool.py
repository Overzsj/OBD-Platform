"""Neutral device tool: firmware and theme are separate operations."""
from pathlib import Path
import concurrent.futures,json,queue,subprocess,sys
import flash_service as flash
APP=Path(__file__).resolve().parent
ROOT=APP.parent

def query_device(port):
 from usb_console import request
 status=request(port,'STATUS',timeout=3);result={'STATUS':status}
 for key in ['HARDWARE GET','THEME GET']:
  try:result[key]=request(port,key,timeout=3)
  except (RuntimeError,TimeoutError):pass
 return result

def gui():
 import tkinter as tk
 from tkinter import ttk,filedialog
 from serial.tools import list_ports
 root=tk.Tk();root.title('OBD 设备工具');root.geometry('940x740');root.minsize(880,680)
 style=ttk.Style(root);style.configure('TLabel',font=('Microsoft YaHei UI',10));style.configure('TButton',font=('Microsoft YaHei UI',10),padding=(10,6));style.configure('Title.TLabel',font=('Microsoft YaHei UI',17,'bold'));style.configure('Hint.TLabel',foreground='#666666')
 pool=concurrent.futures.ThreadPoolExecutor(max_workers=1);events=queue.Queue();busy=False;caps=None;fw_path=None;theme_path=None;closing=False
 port=tk.StringVar();status=tk.StringVar(value='连接 USB，选择设备。空白板也可以直接烧录固件。');device_text=tk.StringVar(value='尚未读取设备信息');fw_text=tk.StringVar(value='尚未选择固件');theme_text=tk.StringVar(value='尚未选择主题');keys={};range_keys={};writes=[]
 ttk.Label(root,text='OBD 设备工具',style='Title.TLabel',padding=(20,12)).pack(anchor='w')
 top=ttk.Frame(root,padding=(20,0));top.pack(fill='x');ttk.Label(top,text='USB 设备').pack(side='left');ports=ttk.Combobox(top,textvariable=port,state='readonly',width=13);ports.pack(side='left',padx=8)
 body=ttk.Frame(root,padding=20);body.pack(fill='both',expand=True);nav=ttk.Frame(body);nav.pack(side='left',fill='y',padx=(0,20));area=ttk.Frame(body);area.pack(side='left',fill='both',expand=True)
 panels={k:ttk.Frame(area) for k in ['device','firmware','theme','settings','backup']}
 def page(key):
  for f in panels.values():f.pack_forget()
  panels[key].pack(fill='both',expand=True)
 for k,label in [('device','设备信息'),('firmware','烧录固件'),('theme','烧录主题'),('settings','设备参数'),('backup','备份与恢复')]:ttk.Button(nav,text=label,command=lambda key=k:page(key)).pack(fill='x',pady=5)
 footer=ttk.Frame(root,padding=(20,8));footer.pack(fill='x');progress=ttk.Progressbar(footer,mode='indeterminate');progress.pack(fill='x');ttk.Label(footer,textvariable=status,wraplength=880).pack(anchor='w',pady=8)
 def controls():
  ports.configure(state='disabled' if busy else 'readonly')
  for b,kind in writes:
   enabled=bool(port.get()) and not busy
   if kind=='theme':enabled=enabled and caps is not None and 'HARDWARE GET' in caps and 'THEME GET' in caps
   b.configure(state='normal' if enabled else 'disabled')
 def error(exc):
  from easy_settings import friendly
  return friendly(exc)
 def submit(fn,done,text):
  nonlocal busy
  if busy:return
  if not port.get():status.set('请先选择 USB 设备。');return
  busy=True;controls();progress.start(12);status.set(text);future=pool.submit(fn,port.get())
  def poll():
   nonlocal busy
   if closing:return
   while not events.empty():status.set(events.get())
   if not future.done():root.after(100,poll);return
   busy=False;progress.stop()
   try:done(future.result())
   except Exception as exc:status.set(error(exc))
   controls()
  root.after(100,poll)
 def refresh():
  nonlocal caps
  if busy:return
  found=list(list_ports.comports());ports['values']=[p.device for p in found]
  if port.get() not in ports['values']:port.set(found[0].device if len(found)==1 else '')
  caps=None;controls();status.set('已找到 USB 设备；可读取信息，或直接选择固件烧录。' if port.get() else '请选择对应的 USB 端口。没有设备时请检查数据线。')
 def changed(_):
  nonlocal caps
  caps=None;device_text.set('设备已切换，请重新读取信息。');controls()
 ports.bind('<<ComboboxSelected>>',changed)
 ttk.Button(top,text='刷新设备',command=refresh).pack(side='left')
 def detected(data):
  nonlocal caps
  caps=data;h=data.get('HARDWARE GET',{});t=data.get('THEME GET',{});s=data['STATUS']
  device_text.set('固件：'+s.get('build','未知')+'\n屏幕：'+(f"{h['width']} × {h['height']} / {h.get('driver','未知')}" if 'width' in h else '未声明')+'\n主题：'+t.get('theme','未声明')+'\n主题接口：'+t.get('engine','未声明'))
  status.set('设备信息已读取。固件与主题可分别烧录。')
 def detect():submit(query_device,detected,'正在读取固件声明的信息；读取失败也不影响固件烧录。')
 ttk.Button(top,text='读取设备信息',command=detect).pack(side='left',padx=6)
 f=panels['device'];ttk.Label(f,text='连接设备，按需要选择功能',style='Title.TLabel').pack(anchor='w');ttk.Label(f,text='本工具管理兼容的硬件、固件和主题包，不绑定某一套主题外观。\n当前烧录硬件配置：经典 ESP32 · 4 MB Flash。',wraplength=650,style='Hint.TLabel').pack(anchor='w',pady=15);ttk.Label(f,textvariable=device_text,wraplength=650,justify='left').pack(anchor='w',pady=12)
 for text,key in [('只安装或更新程序 → 烧录固件','firmware'),('只替换画面资源 → 烧录主题','theme')]:ttk.Button(f,text=text,command=lambda k=key:page(k)).pack(fill='x',pady=8)
 def complete(result):
  nonlocal caps
  caps=None;status.set('写入与校验完成。备份位于：'+result['backup']+'。设备重启后可重新读取信息。');device_text.set('设备已更新，请重新读取信息。')
 def session(p):return flash.FlashSession(p,ROOT/'备份',progress=events.put)
 f=panels['firmware'];ttk.Label(f,text='烧录固件',style='Title.TLabel').pack(anchor='w');ttk.Label(f,text='固件是设备运行的程序。此操作不要求同时选择主题。\n完整安装请选择固件包清单；已安装设备也可用应用 .bin 更新。',wraplength=650,style='Hint.TLabel').pack(anchor='w',pady=12)
 def choose_fw(path=None):
  nonlocal fw_path
  if busy:return
  path=path or filedialog.askopenfilename(initialdir=ROOT/'固件',title='选择固件包或应用镜像',filetypes=[('固件包清单','*.json'),('应用镜像','*.bin')])
  if not path:return
  try:
   plan=flash.firmware_plan(path);fw_path=path
   fw_text.set('已选择：'+plan['name']+'\n'+plan['note']+'\n'+f"共 {len(plan['images'])} 个镜像，{sum(i['size'] for i in plan['images']):,} 字节")
   status.set('文件检查通过。核对说明后，点击“备份并烧录固件”。')
  except Exception as exc:fw_path=None;fw_text.set('固件不可用，请重新选择。');status.set(error(exc))
 ttk.Button(f,text='选择固件文件…',command=choose_fw).pack(anchor='w')
 bundled=ttk.Frame(f);bundled.pack(anchor='w',pady=8)
 for sub,label in [('simulation','选择内置模拟固件'),('vehicle','选择内置实车固件')]:ttk.Button(bundled,text=label,command=lambda s=sub:choose_fw(ROOT/'固件'/s/'firmware-package.json')).pack(side='left',padx=(0,8))
 ttk.Label(f,textvariable=fw_text,wraplength=650,justify='left').pack(anchor='w',pady=15)
 def burn_fw():
  if not fw_path:status.set('请先选择固件文件。');return
  path=fw_path;submit(lambda p:session(p).firmware(path),complete,'正在开始固件备份与烧录；请保持 USB 连接。')
 b=ttk.Button(f,text='备份并烧录固件',command=burn_fw);b.pack(anchor='w',pady=15);writes.append((b,'firmware'))
 ttk.Label(f,text='应用单文件不会改分区或主题。完整包若改变分区，按清单初始化数据区；\n原数据会保存在完整备份中。工具不会把普通固件当作主题写入。',wraplength=650,style='Hint.TLabel').pack(anchor='w')
 f=panels['theme'];ttk.Label(f,text='烧录主题',style='Title.TLabel').pack(anchor='w');ttk.Label(f,text='主题是画面资源和布局数据。此操作只写独立主题分区。\n先读取设备信息，工具再按设备声明的引擎和屏幕检查兼容性。',wraplength=650,style='Hint.TLabel').pack(anchor='w',pady=12)
 def choose_theme():
  nonlocal theme_path
  if busy:return
  if caps is None or 'THEME GET' not in caps:status.set('请先点上方“读取设备信息”。设备必须提供独立主题接口。');return
  path=filedialog.askopenfilename(initialdir=ROOT/'主题',title='选择主题资源',filetypes=[('主题资源','*.sukitheme *.bin')])
  if not path:return
  try:
   plan=flash.theme_plan(path,caps);theme_path=path;theme_text.set('已选择：'+plan['name']+f"\n目标：{plan['width']} × {plan['height']}\n资源大小：{plan['size']:,} 字节");status.set('主题与设备声明匹配，可以烧录。')
  except Exception as exc:theme_path=None;theme_text.set('主题不兼容或文件无效。');status.set(error(exc))
 ttk.Button(f,text='选择主题文件…',command=choose_theme).pack(anchor='w');ttk.Label(f,textvariable=theme_text,wraplength=650).pack(anchor='w',pady=18)
 def burn_theme():
  if not theme_path:status.set('请先选择兼容的主题文件。');return
  path=theme_path;metadata=caps
  submit(lambda p:session(p).theme(path,metadata),complete,'正在开始主题备份与烧录；不更新固件。')
 b=ttk.Button(f,text='备份并烧录主题',command=burn_theme);b.pack(anchor='w',pady=12);writes.append((b,'theme'))
 ttk.Label(f,text='不同引擎的主题不能混刷。目前安装了仪表布局 v1 的编码器，\n支持其 .sukitheme 源文件与 SUKITH29 独立主题镜像；其他引擎需增加编码器。\n主题能否自由排版由固件引擎决定，不由工具假设固定五格。',wraplength=650,style='Hint.TLabel').pack(anchor='w')
 f=panels['settings'];ttk.Label(f,text='设备参数',style='Title.TLabel').pack(anchor='w');ttk.Label(f,text='只有兼容的参数接口才可编辑。当前提供 v30 通用固件参数适配器。\n其他固件仍可通过“烧录固件”安装，不强制使用本工具的参数界面。',wraplength=650,style='Hint.TLabel').pack(anchor='w',pady=14)
 def settings():
  if busy:status.set('请先等待当前操作完成。');return
  exe=Path(sys.executable).with_name('pythonw.exe');subprocess.Popen([str(exe if exe.exists() else Path(sys.executable)),str(APP/'parameter_settings.py'),'--port',port.get()],cwd=APP)
 ttk.Button(f,text='打开报警、声音和量程设置',command=settings).pack(anchor='w',pady=10)
 ttk.Label(f,text='开发者的屏幕驱动、引脚和布局编译工具保存在开发资料中，\n不混入日常烧录操作。原版工具已归档在历史版本。',wraplength=650,style='Hint.TLabel').pack(anchor='w')
 f=panels['backup'];ttk.Label(f,text='备份与恢复',style='Title.TLabel').pack(anchor='w');ttk.Label(f,text='每次烧录前都会保存完整 4 MB 备份，并在写入后校验。\n恢复整板会恢复当时的固件、主题和设置；恢复前也会备份当前设备。',wraplength=650,style='Hint.TLabel').pack(anchor='w',pady=12)
 def open_backups():
  import os
  folder=ROOT/'备份';folder.mkdir(exist_ok=True);os.startfile(folder)
 ttk.Button(f,text='打开备份文件夹',command=open_backups).pack(anchor='w',pady=10)
 def restore():
  path=filedialog.askopenfilename(initialdir=ROOT/'备份',title='选择完整备份恢复',filetypes=[('完整 Flash 备份','*.bin')])
  if path:submit(lambda p:session(p).restore(path),complete,'正在备份当前设备，再恢复所选整板状态…')
 b=ttk.Button(f,text='选择备份并恢复整板…',command=restore);b.pack(anchor='w',pady=12);writes.append((b,'restore'))
 def close():
  nonlocal closing
  if busy:status.set('请等待当前操作完成后关闭。');return
  closing=True;pool.shutdown(wait=False);root.destroy()
 root.protocol('WM_DELETE_WINDOW',close);page('device');refresh();root.mainloop()
if __name__=='__main__':gui()
