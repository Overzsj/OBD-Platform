"""Local accounts, hashed passwords, expiring server-side sessions and reactions."""
import hashlib,hmac,re,secrets,sqlite3,time
from http.cookies import SimpleCookie

ITERATIONS=600000
class AccountError(ValueError):pass
def initialize(store):
    with store.connect() as c:
        c.executescript('''CREATE TABLE IF NOT EXISTS users
          (id TEXT PRIMARY KEY, username TEXT UNIQUE COLLATE NOCASE, display_name TEXT,
           salt TEXT, password_hash TEXT, created INTEGER);
        CREATE TABLE IF NOT EXISTS sessions
          (hash TEXT PRIMARY KEY,user_id TEXT,csrf TEXT,expires INTEGER);
        CREATE TABLE IF NOT EXISTS reactions
          (user_id TEXT,theme_id TEXT,kind TEXT,created INTEGER,
           PRIMARY KEY(user_id,theme_id,kind));
        CREATE TABLE IF NOT EXISTS auth_attempts
          (key TEXT PRIMARY KEY,started INTEGER,count INTEGER);''')
        if 'owner_id' not in [r[1] for r in c.execute('PRAGMA table_info(themes)')]:
            c.execute('ALTER TABLE themes ADD COLUMN owner_id TEXT')

def password_hash(password,salt):
    return hashlib.pbkdf2_hmac('sha256',password.encode(),bytes.fromhex(salt),ITERATIONS).hex()

def throttle(store,key,limit=15):
    now=int(time.time());key=hashlib.sha256(key.encode()).hexdigest()
    with store.connect() as c:
        c.execute('DELETE FROM auth_attempts WHERE started<?',(now-900,))
        c.execute('INSERT INTO auth_attempts VALUES (?,?,1) ON CONFLICT(key) DO UPDATE SET count=count+1',(key,now))
        if c.execute('SELECT count FROM auth_attempts WHERE key=?',(key,)).fetchone()[0]>limit:
            raise AccountError('尝试过于频繁，请 15 分钟后再试')

def authenticate(store,body,register=False):
    username=body.get('username','');password=body.get('password','')
    if not isinstance(username,str) or not re.fullmatch(r'[A-Za-z0-9_]{3,24}',username):raise AccountError('用户名需为 3–24 位字母、数字或下划线')
    if not isinstance(password,str) or not 10<=len(password)<=128:raise AccountError('密码需为 10–128 个字符')
    if register:
        name=body.get('display_name','')
        if not isinstance(name,str) or not 1<=len(name.strip())<=40 or any(ord(c)<32 for c in name):raise AccountError('昵称需为 1–40 个可显示字符')
        uid=secrets.token_hex(16);salt=secrets.token_hex(16)
        hashed=password_hash(password,salt)
        try:
            with store.connect() as c:c.execute('INSERT INTO users VALUES (?,?,?,?,?,?)',(uid,username,name.strip(),salt,hashed,int(time.time())))
        except sqlite3.IntegrityError:raise AccountError('该用户名已被使用')
    else:
        with store.connect() as c:row=c.execute('SELECT * FROM users WHERE username=?',(username,)).fetchone()
        # Do the same expensive hash for missing usernames.
        hashed=password_hash(password,row['salt'] if row else '00'*16)
        if not row or not hmac.compare_digest(hashed,row['password_hash']):raise AccountError('用户名或密码错误')
        uid=row['id']
    raw=secrets.token_urlsafe(32);csrf=secrets.token_urlsafe(32);expires=int(time.time())+7*86400
    with store.connect() as c:
        c.execute('DELETE FROM sessions WHERE expires<?',(int(time.time()),))
        c.execute('INSERT INTO sessions VALUES (?,?,?,?)',(hashlib.sha256(raw.encode()).hexdigest(),uid,csrf,expires))
    return raw,session(store,'obd_session='+raw)

def session(store,cookie):
    try:
        jar=SimpleCookie();jar.load(cookie or '');raw=jar['obd_session'].value
    except (KeyError,ValueError):return None
    hashed=hashlib.sha256(raw.encode()).hexdigest()
    with store.connect() as c:
        row=c.execute('SELECT u.id,u.username,u.display_name,s.csrf,s.expires FROM sessions s JOIN users u ON u.id=s.user_id WHERE s.hash=? AND s.expires>?',(hashed,int(time.time()))).fetchone()
    return dict(row) if row else None

def logout(store,cookie):
    try:
        jar=SimpleCookie();jar.load(cookie or '');raw=jar['obd_session'].value
    except (KeyError,ValueError):return
    with store.connect() as c:c.execute('DELETE FROM sessions WHERE hash=?',(hashlib.sha256(raw.encode()).hexdigest(),))

def decorate(store,items,user):
    uid=user['id'] if user else ''
    with store.connect() as c:
        for item in items:
            rows=c.execute('SELECT kind,COUNT(*) AS n,MAX(user_id=?) AS mine FROM reactions WHERE theme_id=? GROUP BY kind',(uid,item['id'])).fetchall()
            item.update(likes=0,favorites=0,liked=False,favorited=False)
            for row in rows:
                item['likes' if row['kind']=='like' else 'favorites']=row['n']
                item['liked' if row['kind']=='like' else 'favorited']=bool(row['mine'])
    return items

def reaction(store,user,body):
    identity=body.get('id');kind=body.get('kind');active=body.get('active')
    if kind not in ('like','favorite') or type(active) is not bool:raise AccountError('操作参数错误')
    with store.connect() as c:
        if not c.execute("SELECT 1 FROM themes WHERE id=? AND status='published'",(identity,)).fetchone():raise AccountError('作品不存在或尚未发布')
        if active:c.execute('INSERT OR IGNORE INTO reactions VALUES (?,?,?,?)',(user['id'],identity,kind,int(time.time())))
        else:c.execute('DELETE FROM reactions WHERE user_id=? AND theme_id=? AND kind=?',(user['id'],identity,kind))
    return decorate(store,[{'id':identity}],user)[0]

def library(store,user,kind):
    if kind=='works':
        with store.connect() as c:rows=c.execute('SELECT id,theme_id,version,name,description,author,license,status,created,downloads FROM themes WHERE owner_id=? ORDER BY created DESC',(user['id'],)).fetchall()
        return decorate(store,[dict(r) for r in rows],user)
    return [t for t in decorate(store,store.listing(),user) if t['favorited']]
