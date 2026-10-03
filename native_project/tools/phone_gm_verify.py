import urllib.request,json,subprocess,pathlib
status=json.loads(urllib.request.urlopen('http://127.0.0.1:19999/gm/status').read())
checks={'status':status,'catalogs':{}}
for kind in ['hero','skin','jewel','item','equip']:
    result=json.loads(urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:19999/gm/catalog',data=('kind='+kind+'&page=0').encode(),method='POST')).read())
    assert result['ok'] and len(result['entries'])<=40
    checks['catalogs'][kind]={'total':result['total'],'returned':len(result['entries'])}
pid=subprocess.check_output(['adb','-s','BH9065XZB5','shell','pidof','com.siva.project.x2']).decode().strip()
log=subprocess.check_output(['adb','-s','BH9065XZB5','logcat','-d','--pid='+pid]).decode('utf-8',errors='replace')
checks['client_property_errors']=log.count('ClientProperty.GetCombat')
checks['fatal_exceptions']=log.count('FATAL EXCEPTION')
checks['null_reference_exceptions']=log.count('NullReferenceException')
pathlib.Path('inspection/gm8_phone_verification.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2),encoding='utf-8')
pathlib.Path('inspection/gm8_runtime.log').write_text(log,encoding='utf-8')
print(checks['catalogs'], 'combat errors',checks['client_property_errors'],'fatal',checks['fatal_exceptions'])
