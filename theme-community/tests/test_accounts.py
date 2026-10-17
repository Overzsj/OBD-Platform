import json,time,unittest
from test_community import CommunityTests,module
class AccountTests(CommunityTests):
    def test_password_storage_session_logout(self):
        with self.store.connect() as c:
            user=c.execute('SELECT * FROM users WHERE username=?',('tester',)).fetchone()
            self.assertNotEqual(user['password_hash'],'test-password-123');self.assertEqual(len(user['salt']),32)
            self.assertNotIn(self.cookie.split('=')[1],[r[0] for r in c.execute('SELECT hash FROM sessions')])
        self.assertEqual(json.loads(self.request('/api/me')[1])['user']['username'],'tester')
        old_cookie=self.cookie
        self.assertEqual(self.request('/api/logout',{})[0],200)
        self.assertIsNone(json.loads(self.request('/api/me')[1])['user'])
        self.cookie=None;self.csrf=None
        self.assertEqual(self.request('/api/login',{'username':'tester','password':'wrong-password-123'})[0],400)
        status,data,headers=self.request('/api/login',{'username':'TESTER','password':'test-password-123'})
        self.assertEqual(status,200);self.assertIn('HttpOnly',headers['Set-Cookie']);self.assertIn('SameSite=Strict',headers['Set-Cookie'])
        self.assertNotEqual(old_cookie,headers['Set-Cookie'].split(';')[0])
    def test_reactions_unique_private_and_persistent(self):
        identity=self.store.listing()[0]['id'];body={'id':identity,'kind':'like','active':True}
        for _ in range(3):self.assertEqual(self.request('/api/reaction',body)[0],200)
        item=json.loads(self.request('/api/themes')[1])[0];self.assertEqual(item['likes'],1);self.assertTrue(item['liked'])
        self.assertEqual(self.request('/api/reaction',dict(body,kind='favorite'))[0],200)
        self.assertEqual(len(json.loads(self.request('/api/library?kind=favorites')[1])),1)
        reopened=module.Store(self.tmp.name);self.assertEqual(len(module.accounts.library(reopened,module.accounts.session(reopened,self.cookie),'favorites')),1)
        cookie,csrf=self.cookie,self.csrf;self.cookie=None;self.csrf=None
        self.assertEqual(self.request('/api/reaction',body)[0],401);self.assertEqual(self.request('/api/library?kind=favorites')[0],401)
        item=json.loads(self.request('/api/themes')[1])[0];self.assertEqual(item['likes'],1);self.assertFalse(item['liked'])
        self.cookie,self.csrf=cookie,csrf
        self.assertEqual(self.request('/api/reaction',dict(body,active=False))[0],200)
        self.assertEqual(json.loads(self.request('/api/themes')[1])[0]['likes'],0)
    def test_csrf_ownership_and_expiry(self):
        identity=self.store.listing()[0]['id'];csrf=self.csrf;self.csrf='wrong'
        self.assertEqual(self.request('/api/reaction',{'id':identity,'kind':'like','active':True})[0],403)
        self.csrf=csrf;body=self.submission();body['author']='Impersonated author'
        self.assertEqual(self.request('/api/submit',body)[0],201)
        works=json.loads(self.request('/api/library?kind=works')[1]);self.assertEqual(len(works),1)
        self.assertEqual(works[0]['author'],'Test author');self.assertEqual(works[0]['status'],'pending')
        self.assertEqual(self.request('/api/reaction',{'id':works[0]['id'],'kind':'like','active':True})[0],400)
        with self.store.connect() as c:c.execute('UPDATE sessions SET expires=?',(int(time.time())-1,))
        self.assertIsNone(json.loads(self.request('/api/me')[1])['user'])
        self.assertEqual(self.request('/api/submit',body)[0],401)
    def test_register_validation_and_duplicate(self):
        for username in ('a','bad user','<script>'):
            self.assertEqual(self.request('/api/register',{'username':username,'password':'test-password-123','display_name':'Name'})[0],400)
        self.assertEqual(self.request('/api/register',{'username':'TESTER','password':'test-password-123','display_name':'Name'})[0],400)
        self.assertEqual(self.request('/api/register',{'username':'second','password':'short','display_name':'Name'})[0],400)
    def test_throttle(self):
        for _ in range(2):module.accounts.throttle(self.store,'test-limit',2)
        with self.assertRaises(module.accounts.AccountError):module.accounts.throttle(self.store,'test-limit',2)
del CommunityTests  # Avoid rediscovering the imported base class as another suite.
if __name__=='__main__':unittest.main()
