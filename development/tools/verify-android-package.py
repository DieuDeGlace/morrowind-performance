from pathlib import Path
from zipfile import ZipFile
import argparse
import hashlib
import json
import struct

parser=argparse.ArgumentParser()
parser.add_argument('folder',type=Path)
parser.add_argument('--app',required=True)
parser.add_argument('--engine',required=True)
parser.add_argument('--archive',required=True)
parser.add_argument('--report',default='ANDROID-VALIDATION.json')
args=parser.parse_args()
folder=args.folder
apk=folder/'OpenMW-Android-0.51.0-ragdoll-preview-arm64.apk'
digest=hashlib.sha256(apk.read_bytes()).hexdigest()
assert digest==(folder/(apk.name+'.sha256')).read_text(encoding='utf-8-sig').split()[0]
info=(folder/'APK-INFO.txt').read_text(encoding='utf-8-sig')
assert "name='com.dieudeglace.openmw.ragdolltest'" in info
assert "versionCode='5130'" in info and 'ragdoll-preview.11' in info
certificate='67405aa299809d192cf174807b364c853b1c6357860a025b9d465648475baf62'
signature=(folder/'SIGNATURE.txt').read_text(encoding='utf-8-sig').lower()
assert certificate in signature and 'verified using v2 scheme (apk signature scheme v2): true' in signature
assert args.app in (folder/'BUILD-INFO.txt').read_text(encoding='utf-8-sig')

def elf(data):
    assert data[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',data,18)[0]==183
    start=struct.unpack_from('<Q',data,40)[0]
    size,count,string_index=struct.unpack_from('<HHH',data,58)
    sections=[struct.unpack_from('<IIQQQQIIQQ',data,start+i*size) for i in range(count)]
    names=data[sections[string_index][4]:sections[string_index][4]+sections[string_index][5]]
    def string(table,offset):return table[offset:table.find(b'\0',offset)].decode()
    labels=[string(names,section[0]) for section in sections]
    assert not any(label.startswith('.debug_') for label in labels)
    symbols={};needed=[]
    for section in sections:
        strings=sections[section[6]]
        table=data[strings[4]:strings[4]+strings[5]]
        if section[1]==11:
            for pos in range(section[4],section[4]+section[5],section[9]):
                name,kind,visibility,index,value,length=struct.unpack_from('<IBBHQQ',data,pos)
                if name:
                    symbols[string(table,name)]={'binding':kind>>4,'visibility':visibility&3,'section':index}
        elif section[1]==6:
            for pos in range(section[4],section[4]+section[5],16):
                tag,value=struct.unpack_from('<QQ',data,pos)
                if tag==1:needed.append(string(table,value))
    return symbols,needed

with ZipFile(apk) as zipped:
    abis={name.split('/')[1] for name in zipped.namelist() if name.startswith('lib/')}
    assert abis=={'arm64-v8a'}
    pin=json.loads(zipped.read('assets/libopenmw/openmw/performance-engine.json'))
    assert pin['engine_commit']==args.engine and pin['engine_archive_sha256']==args.archive
    assert json.loads((folder/'ENGINE-PROVENANCE.json').read_text())==pin
    native=zipped.read('lib/arm64-v8a/libopenmw.so')
    backend=zipped.read('lib/arm64-v8a/libGL.so')
    symbols,needed=elf(native)
    backend_symbols,_=elf(backend)
    reference=symbols['gl4es_getDepthCopySupport']
    definition=backend_symbols['gl4es_getDepthCopySupport']
    assert reference=={'binding':2,'visibility':0,'section':0},reference
    assert definition['binding']==1 and definition['visibility']==0 and definition['section']>0,definition
    assert 'libGL.so' in needed,needed
report={'result':'passed','apk':apk.name,'sha256':digest,'application_id':'com.dieudeglace.openmw.ragdolltest',
    'version_code':5130,'version_name':info.split("versionName='")[1].split("'")[0],
    'certificate_sha256':certificate,'same_signing_key_as_preview10':True,'app_commit':args.app,
    'engine_commit':args.engine,'engine_archive_sha256':args.archive,'native_sha256':hashlib.sha256(native).hexdigest(),
    'backend_sha256':hashlib.sha256(backend).hexdigest(),'accurate_depth_copy_query':{'engine_reference':reference,'backend_definition':definition,'explicit_backend_dependency':True},
    'native_debug_sections':False,'abi':'arm64-v8a','device_gameplay_tested':False}
(folder/args.report).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
