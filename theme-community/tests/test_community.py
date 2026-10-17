import base64, copy, http.client, importlib.util, json, sqlite3, tempfile, threading, unittest
from pathlib import Path
from urllib.request import Request, urlopen
from urllib.error import HTTPError
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('community_server',ROOT/'server.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class CommunityTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.store=module.Store(self.tmp.name)
        self.server=module.make_server(self.store,port=0,token='test-only-token')
        self.worker=threading.Thread(target=self.server.serve_forever,daemon=True);self.worker.start()
        self.url='http://127.0.0.1:'+str(self.server.server_port)
        self.cookie=None;self.csrf=None
        status,data,headers=self.request('/api/register',{'username':'tester','password':'test-password-123','display_name':'Test author'})
        self.cookie=headers['Set-Cookie'].split(';')[0];self.csrf=json.loads(data)['user']['csrf']
        self.theme=json.loads((ROOT.parent/'firmware/themes/open-instrument-1.0.0.sukitheme').read_text(encoding='utf-8'))
    def tearDown(self):self.server.shutdown();self.server.server_close();self.worker.join();self.tmp.cleanup()
    def request(self,path,body=None,token=None,origin=None):
        headers={}
        if getattr(self,'cookie',None):headers['Cookie']=self.cookie
        if body is not None and getattr(self,'csrf',None):headers['X-CSRF-Token']=self.csrf
        if body is not None:headers['Content-Type']='application/json'
        if token:headers['X-Admin-Token']=token
        if origin:headers['Origin']=origin
        r=Request(self.url+path,data=json.dumps(body).encode() if body is not None else None,headers=headers)
        try:
            with urlopen(r) as response:return response.status,response.read(),response.headers
        except HTTPError as response:return response.code,response.read(),response.headers
    def submission(self):
        t=copy.deepcopy(self.theme);t['id']='community-test';t['name']='Test theme';t=module.validate(__import__('theme_codec').sign(t))
        return {'file':base64.b64encode(module.canonical(t)).decode(),'author':'Test author','license':'GPL-3.0-only','rights':True}
    def test_preview_is_separate_and_does_not_increment_download(self):
        item=self.store.listing()[0]
        status,data,_=self.request('/api/preview/'+item['id']);self.assertEqual(status,200)
        self.assertEqual(json.loads(data)['id'],'open-instrument');self.assertEqual(self.store.listing()[0]['downloads'],0)
        self.assertEqual(self.request('/api/preview/not-found')[0],404)
        for path in ('/preview.js','/firmware'):self.assertEqual(self.request(path)[0],200)
        identity=json.loads(self.request('/api/submit',self.submission())[1])['id']
        self.assertEqual(self.request('/api/preview/'+identity)[0],404)
    def test_real_catalog_and_download(self):
        status,data,headers=self.request('/api/themes');self.assertEqual(status,200);items=json.loads(data);self.assertEqual(len(items),1)
        self.assertEqual(items[0]['downloads'],0);identity=items[0]['id']
        status,payload,headers=self.request('/download/'+identity);self.assertEqual(status,200);self.assertEqual(module.hashlib.sha256(payload).hexdigest(),identity)
        self.assertIn('attachment',headers['Content-Disposition']);self.assertEqual(json.loads(self.request('/api/themes')[1])[0]['downloads'],1)
    def test_submit_review_publish_persistence(self):
        status,data,_=self.request('/api/submit',self.submission());self.assertEqual(status,201);identity=json.loads(data)['id']
        self.assertEqual(len(json.loads(self.request('/api/themes')[1])),1);self.assertEqual(self.request('/download/'+identity)[0],404)
        self.assertEqual(self.request('/api/themes?status=pending')[0],401)
        self.assertEqual(len(json.loads(self.request('/api/themes?status=pending',token='test-only-token')[1])),1)
        self.assertEqual(self.request('/api/moderate',{'id':identity,'action':'publish'},token='wrong')[0],401)
        self.assertEqual(self.request('/api/moderate',{'id':identity,'action':'publish'},token='test-only-token')[0],200)
        self.assertEqual(self.request('/download/'+identity)[0],200)
        reopened=module.Store(self.tmp.name);self.assertEqual(len(reopened.listing()),2)
    def test_duplicate_rights_integrity_and_origin(self):
        body=self.submission();body['rights']=False;self.assertEqual(self.request('/api/submit',body)[0],400)
        body=self.submission();self.assertEqual(self.request('/api/submit',body,origin='https://evil.example')[0],403)
        self.assertEqual(self.request('/api/submit',body)[0],201);self.assertEqual(self.request('/api/submit',body)[0],409)
        t=copy.deepcopy(self.theme);t['name']='tampered';body['file']=base64.b64encode(json.dumps(t).encode()).decode();self.assertEqual(self.request('/api/submit',body)[0],400)
    def test_reject_does_not_publish(self):
        identity=json.loads(self.request('/api/submit',self.submission())[1])['id']
        self.assertEqual(self.request('/api/moderate',{'id':identity,'action':'reject'},token='test-only-token')[0],200)
        self.assertEqual(self.request('/download/'+identity)[0],404)
    def test_static_allowlist_and_security_headers(self):
        for p in ('/','/app.js','/style.css','/open-instrument.png'):self.assertEqual(self.request(p)[0],200)
        self.assertEqual(self.request('/../server.py')[0],404);self.assertEqual(self.request('/data/community.sqlite3')[0],404)
        headers=self.request('/')[2];self.assertEqual(headers['X-Content-Type-Options'],'nosniff');self.assertIn("frame-ancestors 'none'",headers['Content-Security-Policy'])
    def test_oversized_body_and_bad_formats(self):
        self.assertEqual(self.request('/api/submit',{'file':'not-base64','rights':True})[0],400)
        # Verify rejection from headers before sending a large body. Sending all
        # bytes can race Windows socket reset after the intentional early close.
        c=http.client.HTTPConnection('127.0.0.1',self.server.server_port)
        try:
            c.putrequest('POST','/api/submit');c.putheader('Content-Type','application/json')
            c.putheader('Content-Length',str(module.MAX_FILE_BYTES*2+1));c.endheaders()
            response=c.getresponse();self.assertEqual(response.status,413);response.read()
        finally:c.close()
if __name__=='__main__':unittest.main()
