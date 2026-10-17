"""Standalone community catalog. Python 3.10+, SQLite, no agent service."""
from pathlib import Path
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
import argparse, base64, hashlib, hmac, json, os, sqlite3, sys, time
from contextlib import contextmanager
from urllib.parse import urlsplit, parse_qs
ROOT = Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT))
import accounts
sys.path.insert(0, str(ROOT/'schema'))
from theme_codec import validate, canonical, MAX_FILE_BYTES

class Store:
    def __init__(self, directory):
        self.directory = Path(directory); self.directory.mkdir(parents=True, exist_ok=True)
        self.db = self.directory/'community.sqlite3'
        with self.connect() as c:
            c.execute('''CREATE TABLE IF NOT EXISTS themes
                (id TEXT PRIMARY KEY, theme_id TEXT, version TEXT, name TEXT,
                 description TEXT, author TEXT, license TEXT, status TEXT,
                 created INTEGER, downloads INTEGER DEFAULT 0, payload BLOB,
                 UNIQUE(theme_id,version))''')
        accounts.initialize(self)
        sample = ROOT/'themes/open-instrument-1.0.0.sukitheme'
        if sample.exists():
            try: self.submit(json.loads(sample.read_text(encoding='utf-8')), 'OBD Community', 'GPL-3.0-only; font OFL-1.1', 'published')
            except sqlite3.IntegrityError: pass
    @contextmanager
    def connect(self):
        c=sqlite3.connect(self.db, timeout=10); c.row_factory=sqlite3.Row
        try:
            with c: yield c
        finally: c.close()
    def submit(self, theme, author, license_name, status='pending', owner_id=None):
        theme=validate(theme); payload=canonical(theme)
        if not isinstance(author,str) or not 1<=len(author.strip())<=60: raise ValueError('作者名称需为 1–60 个字符')
        if not isinstance(license_name,str) or not 1<=len(license_name.strip())<=100: raise ValueError('请填写主题许可')
        identity=hashlib.sha256(payload).hexdigest()
        with self.connect() as c:
            c.execute('INSERT INTO themes (id,theme_id,version,name,description,author,license,status,created,payload,owner_id) VALUES (?,?,?,?,?,?,?,?,?,?,?)',
                (identity,theme['id'],theme['version'],theme['name'],theme['description'],author.strip(),license_name.strip(),status,int(time.time()),payload,owner_id))
        return identity
    def listing(self, pending=False):
        with self.connect() as c:
            rows=c.execute('SELECT id,theme_id,version,name,description,author,license,status,created,downloads FROM themes WHERE status=? ORDER BY created DESC,id',('pending' if pending else 'published',)).fetchall()
        return [dict(x) for x in rows]
    def moderate(self, identity, action):
        if action not in ('publish','reject'): raise ValueError('未知审核操作')
        with self.connect() as c:
            n=c.execute('UPDATE themes SET status=? WHERE id=? AND status=?',('published' if action=='publish' else 'rejected',identity,'pending')).rowcount
        if not n: raise ValueError('待审核主题不存在')
    def download(self, identity, count=True):
        with self.connect() as c:
            row=c.execute('SELECT payload,theme_id,version FROM themes WHERE id=? AND status=?',(identity,'published')).fetchone()
            if not row: return None
            if hashlib.sha256(row['payload']).hexdigest()!=identity: raise ValueError('存储校验失败')
            if count:c.execute('UPDATE themes SET downloads=downloads+1 WHERE id=?',(identity,))
        return row

def make_server(store, host='127.0.0.1', port=5190, token='', allow_upload=True):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args): pass  # Never log admin headers or submitted files.
        def send(self, status, data, content_type='application/json; charset=utf-8', extra=None):
            if isinstance(data,(dict,list)): data=json.dumps(data,ensure_ascii=False).encode('utf-8')
            elif isinstance(data,str): data=data.encode('utf-8')
            self.send_response(status)
            self.send_header('Content-Type',content_type); self.send_header('Content-Length',str(len(data)))
            self.send_header('X-Content-Type-Options','nosniff'); self.send_header('Cache-Control','no-store')
            self.send_header('Content-Security-Policy',"default-src 'self'; img-src 'self' data:; style-src 'self'; script-src 'self'; frame-ancestors 'none'; base-uri 'none'")
            if extra:
                for k,v in extra.items(): self.send_header(k,v)
            self.end_headers(); self.wfile.write(data)
        def authorized(self):
            return bool(token) and hmac.compare_digest(self.headers.get('X-Admin-Token',''),token)
        def do_GET(self):
            try:
                url=urlsplit(self.path)
                user=accounts.session(store,self.headers.get('Cookie'))
                if url.path=='/api/me':return self.send(200,{'user':user})
                if url.path=='/api/library':
                    if not user:return self.send(401,{'error':'请先登录'})
                    kind=parse_qs(url.query).get('kind',['favorites'])[0]
                    return self.send(200,accounts.library(store,user,kind))
                if url.path=='/api/themes':
                    pending=parse_qs(url.query).get('status')==['pending']
                    if pending and not self.authorized(): return self.send(401,{'error':'需要管理员密钥'})
                    return self.send(200,accounts.decorate(store,store.listing(pending),user))
                if url.path.startswith('/api/preview/'):
                    row=store.download(url.path.split('/')[-1],count=False)
                    if not row:return self.send(404,{'error':'主题不存在或尚未发布'})
                    return self.send(200,row['payload'])
                if url.path.startswith('/download/'):
                    row=store.download(url.path.split('/')[-1])
                    if not row:return self.send(404,{'error':'主题不存在或尚未发布'})
                    return self.send(200,row['payload'],'application/octet-stream',{'Content-Disposition':f'attachment; filename="{row["theme_id"]}-{row["version"]}.sukitheme"'})
                names={'/':'index.html','/app.js':'app.js','/style.css':'style.css','/open-instrument.png':'open-instrument.png','/preview.js':'preview.js','/firmware':'firmware.html'}
                if url.path not in names:return self.send(404,{'error':'页面不存在'})
                file=ROOT/'static'/names[url.path]
                content_type={'html':'text/html; charset=utf-8','js':'text/javascript; charset=utf-8','css':'text/css; charset=utf-8','png':'image/png'}[file.suffix[1:]]
                self.send(200,file.read_bytes(),content_type)
            except Exception: self.send(500,{'error':'读取失败，请检查服务端数据'})
        def do_POST(self):
            try:
                # No cross-origin mutation; host must match the browser's origin.
                origin=self.headers.get('Origin')
                if origin and urlsplit(origin).netloc!=self.headers.get('Host'):return self.send(403,{'error':'来源不匹配'})
                n=int(self.headers.get('Content-Length','0'))
                if not 0<n<=MAX_FILE_BYTES*2:return self.send(413,{'error':'提交内容超过限制'})
                if self.headers.get_content_type()!='application/json':return self.send(415,{'error':'仅接受 JSON'})
                self.connection.settimeout(15)
                body=json.loads(self.rfile.read(n))
                if not isinstance(body,dict):raise ValueError('请求必须为 JSON 对象')
                user=accounts.session(store,self.headers.get('Cookie'))
                if self.path in ('/api/register','/api/login'):
                    accounts.throttle(store,self.client_address[0]+':'+self.path)
                    accounts.throttle(store,'username:'+str(body.get('username','')).lower())
                    raw,user=accounts.authenticate(store,body,self.path=='/api/register')
                    secure='; Secure' if os.environ.get('OBD_COOKIE_SECURE')=='1' else ''
                    return self.send(200,{'user':user},'application/json; charset=utf-8',{'Set-Cookie':'obd_session='+raw+'; HttpOnly; SameSite=Strict; Path=/; Max-Age=604800'+secure})
                if self.path in ('/api/logout','/api/reaction','/api/submit'):
                    if not user:return self.send(401,{'error':'请先登录'})
                    if not hmac.compare_digest(self.headers.get('X-CSRF-Token',''),user['csrf']):return self.send(403,{'error':'会话校验失败，请刷新页面'})
                if self.path=='/api/logout':
                    accounts.logout(store,self.headers.get('Cookie'));return self.send(200,{'ok':True},extra={'Set-Cookie':'obd_session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0'})
                if self.path=='/api/reaction':return self.send(200,accounts.reaction(store,user,body))
                if self.path=='/api/submit':
                    if not allow_upload:return self.send(403,{'error':'此站点已关闭投稿'})
                    if body.get('rights') is not True:raise ValueError('请确认你有权发布主题内的全部资源')
                    data=base64.b64decode(body.get('file',''),validate=True)
                    if len(data)>MAX_FILE_BYTES:raise ValueError('主题文件超过 2 MiB')
                    theme=json.loads(data); identity=store.submit(theme,user['display_name'],body.get('license'),owner_id=user['id'])
                    return self.send(201,{'id':identity,'status':'pending','message':'已提交，审核通过后公开'})
                if self.path=='/api/moderate':
                    if not self.authorized():return self.send(401,{'error':'管理员密钥错误或未启用审核'})
                    store.moderate(body.get('id'),body.get('action'));return self.send(200,{'ok':True})
                self.send(404,{'error':'接口不存在'})
            except accounts.AccountError as e:self.send(400,{'error':str(e)})
            except sqlite3.IntegrityError:self.send(409,{'error':'该主题 ID 与版本已存在，请提升版本号'})
            except (ValueError,TypeError,KeyError,UnicodeError):self.send(400,{'error':'主题或表单校验失败，请检查格式、完整性、作者和许可'})
            except Exception:self.send(500,{'error':'提交失败，请检查服务端存储'})
    class Server(ThreadingHTTPServer):
        daemon_threads=True
        def get_request(self):
            sock,addr=super().get_request();sock.settimeout(15);return sock,addr
    return Server((host,port),Handler)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--host',default='127.0.0.1');p.add_argument('--port',type=int,default=5190)
    p.add_argument('--data',type=Path,default=ROOT/'data');p.add_argument('--read-only',action='store_true');a=p.parse_args()
    server=make_server(Store(a.data),a.host,a.port,os.environ.get('OBD_COMMUNITY_ADMIN_TOKEN',''),not a.read_only)
    print(f'Theme community: http://{a.host}:{server.server_port}',flush=True)
    try:server.serve_forever()
    except KeyboardInterrupt:pass
    finally:server.server_close()
